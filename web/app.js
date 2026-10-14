// The main screen: the map, the exemplar cards either side of it, the sidebar, and the keys, as in
// the window app. The world is the daemon's, shared by every viewer; what this viewer has selected,
// its export list and its exemplar cards stay in this browser.

import { api, binary, element, dialog, copyText, download, remembered, remember, pad3, pad5, plural, upper } from './common.js';
import { drawBitzkey, drawReticle, zoneColour, mapDotRadii, dotRadius, bitzkeyCanvas, iconCanvas, css, pyRound,
         GRASS, GRID, EXPORTING, RING_WIDTH, SPECIES } from './draw.js';
import { details, look } from './inspector.js';
import { openGraph } from './graph.js';
import { openSetup, openCatalog, openRules } from './setup.js';

const MAP_ZOOM_STEP = 1.25;
const MAP_ZOOM_MAX = 12;
const MAP_DRAG_THRESHOLD = 5;
const TRAIL_OPACITY = 0.5;
const POLL_MS = 120;
const SLOW_POLL_MS = 1000;
const FRAME_MS = 500;  // the map and the graph refresh at most twice a second, however fast the world runs
const MESSAGE_MS = 5000;
const SPEED_COUNT = 5;

const $ = id => document.getElementById(id);

export const app = {
  state: null,          // the latest /api/state
  frame: null,          // the latest map frame, parsed
  selection: new Set(), // picked on the map or the graph: one viewer's own
  selectedId: null,     // the one the inspector shows, when one is picked
  showInsights: false,
  exportList: [],       // {identifier, dna, saved}
  overlay: null,        // the graph or a setup screen, while open
  say,
  refresh: () => poll(true),
  select,
  selectAll,
  catalogIdentifiers,
  exportIdentifiers,
  renew,
  browseCatalog,
};

let shownVersion = -1;
let shownWorld = -1;
let mapZoom = 1;
let mapCentre = null;     // [x, y] in cells, or null for the middle
let marquee = null;       // {x, y, adding, nowX, nowY}
let panFrom = null;
let messageTimer = null;
let exemplarChoices = remembered('exemplars', null);
let frameLoading = false;
let lastSlow = 0;
let lastFrame = 0;
let agentCache = null;    // the selected Bitzkey's details, with the version they are of

// ---------------------------------------------------------------------------------------------
// Messages

function say(text) {
  const message = $('message');
  message.textContent = text;
  message.classList.add('note');
  clearTimeout(messageTimer);
  messageTimer = setTimeout(() => {
    message.classList.remove('note');
    message.textContent = HINT;
  }, MESSAGE_MS);
}

const HINT = 'SPACE  pause   -/+  speed   LEFT / RIGHT  rewind / step   WHEEL / Z  zoom   I  insights   B  report   ' +
             'G  graph   E  export   R  restart   ESC  new world';

// ---------------------------------------------------------------------------------------------
// Fetching the world

async function control(action, extra = {}) {
  try {
    const result = await api('/api/control', { action, ...extra });
    if (result.note) say(result.note);
    applyState(result);
    return result;
  } catch (error) {
    say(error.message);
  }
}

let wasEnded = null;

function applyState(state) {
  app.state = state;
  if (wasEnded === false && state.ended && state.world === shownWorld) {  // the run has just ended: show its graph
    say(state.end_note);
    setTimeout(graph, 0);
  }
  wasEnded = state.ended;
  if (state.world !== shownWorld) {  // a new world: what this viewer knew of the last is let go
    if (shownWorld !== -1) {
      app.selection.clear();
      app.selectedId = null;
      mapZoom = 1;
      mapCentre = null;
    }
    shownWorld = state.world;
    app.exportList = remembered(`export.${state.world}`, []);
    updateExportButton();
  }
  drawHeader();
}

async function poll(force = false) {
  try {
    const state = await api('/api/state');
    $('lost').hidden = true;
    applyState(state);
    const now = performance.now();
    if (force || (state.version !== shownVersion && now - lastFrame >= FRAME_MS)) {
      shownVersion = state.version;
      lastFrame = now;
      await loadFrame();
      app.overlay?.update?.(state);
    }
    if (force || now - lastSlow > SLOW_POLL_MS) {
      lastSlow = now;
      await Promise.all([loadExemplars(), drawSidebar()]);
    }
  } catch (error) {
    $('lost').hidden = false;
  }
}

async function loop() {
  await poll();
  setTimeout(loop, POLL_MS);
}

async function loadFrame() {
  if (frameLoading) return;
  frameLoading = true;
  try {
    app.frame = parseFrame(await binary('/api/frame'));
    forgetTheDead();
    drawMap();
    showSquare();  // the list follows the square as Bitzkeys come and go
  } finally {
    frameLoading = false;
  }
}

function parseFrame(view) {
  const width = view.getUint32(4, true), height = view.getUint32(8, true);
  const turn = view.getUint32(12, true), count = view.getUint32(16, true), trailCount = view.getUint32(20, true);
  let offset = 24;
  const temperatures = new Float32Array(height);
  for (let y = 0; y < height; ++y, offset += 4) temperatures[y] = view.getFloat32(offset, true);
  const grass = new Uint8Array(view.buffer, offset, width * height);
  offset += width * height;
  const agents = new Array(count);
  const living = new Map();
  for (let index = 0; index < count; ++index, offset += 33) {
    const agent = {
      identifier: view.getUint32(offset, true),
      x: view.getUint16(offset + 4, true),
      y: view.getUint16(offset + 6, true),
      dot: [view.getUint8(offset + 8), view.getUint8(offset + 9), view.getUint8(offset + 10)],
      ring: [view.getUint8(offset + 11), view.getUint8(offset + 12), view.getUint8(offset + 13)],
      social: [view.getUint8(offset + 14), view.getUint8(offset + 15), view.getUint8(offset + 16)],
      species: view.getUint8(offset + 17),
      size: view.getUint8(offset + 18),
      alive: view.getUint8(offset + 19) === 1,
      opacity: view.getUint8(offset + 20) / 255,
      trail: [],
    };
    agents[index] = agent;
    if (agent.alive) living.set(agent.identifier, agent);
  }
  for (let index = 0; index < trailCount; ++index, offset += 8) {
    agents[view.getUint32(offset, true)]?.trail.push([view.getUint16(offset + 4, true), view.getUint16(offset + 6, true)]);
  }
  return { width, height, turn, temperatures, grass, agents, living };
}

// Let go of any Bitzkey selected or picked that has died, so the sidebar returns to its neutral state.
function forgetTheDead() {
  const living = app.frame.living;
  for (const identifier of [...app.selection]) {
    if (!living.has(identifier)) app.selection.delete(identifier);
  }
  if (app.selectedId !== null && !living.has(app.selectedId)) app.selectedId = null;
}

// ---------------------------------------------------------------------------------------------
// The header and footer

function drawHeader() {
  const state = app.state;
  const status = state.playing ? 'RUNNING' : state.ended ? 'ENDED' : 'PAUSED';
  $('status').textContent = status;
  $('status').style.color = state.playing ? 'var(--green)' : 'var(--amber)';
  $('speed').textContent = `SPEED ${state.speeds[state.speed]}`;
  $('play').innerHTML = state.playing ? '&#10074;&#10074;' : '&#9654;';
  showRulesChanged(state.rules_changed ?? 0);
  const gpu = $('gpu');
  gpu.textContent = upper(state.gpu_status || '');
  gpu.className = `gpu ${state.on_gpu ? 'on' : 'off'}`;
  gpu.title = state.gpu_status || '';
  $('turn').textContent = `TURN  ${pad5(state.turn)}`;
  $('alive').textContent = `${state.alive} ALIVE    ${state.lost} LOST`;
  $('year').textContent = `YEAR ${state.year}`;
  $('season').textContent = upper(state.season);
  $('season').classList.toggle('winter', state.winter);
  const population = $('population');
  if (population.childElementCount !== SPECIES.length) {
    population.replaceChildren(...SPECIES.map(name => element('div', { class: 'row' }, element('span', {}, upper(name)), element('span', {}, '0'))));
  }
  SPECIES.forEach((name, index) => { population.children[index].lastChild.textContent = state.species[index]; });
  let history = `HISTORY  ${state.turn}/${state.frontier}`;
  if (state.earliest) history += `  FROM ${state.earliest}`;
  $('history').textContent = history;
}

function updateExportButton() {
  const button = $('export-all');
  button.textContent = `EXPORT ALL SELECTED (${app.exportList.length})`;
  button.classList.toggle('any', app.exportList.length > 0);
  remember(`export.${shownWorld}`, app.exportList);
}

// ---------------------------------------------------------------------------------------------
// The map

const map = $('map');
const mapContext = map.getContext('2d');
const grassCanvas = document.createElement('canvas');  // the zones and grass, a pixel a cell, for small cells

function viewRect() {
  const scale = window.devicePixelRatio || 1;
  const width = map.clientWidth, height = map.clientHeight;
  if (map.width !== Math.round(width * scale) || map.height !== Math.round(height * scale)) {
    map.width = Math.round(width * scale);
    map.height = Math.round(height * scale);
  }
  return { x: 0, y: 0, w: width, h: height, scale };
}

function clampedCentre([cx, cy], view, scale) {
  const clamp = (middle, cells, pixels) => {
    const half = pixels / 2 / scale;
    return cells <= 2 * half ? cells / 2 : Math.min(cells - half, Math.max(half, middle));
  };
  return [clamp(cx, app.frame.width, view.w), clamp(cy, app.frame.height, view.h)];
}

function board() {
  const view = viewRect();
  const fit = Math.min(view.w / app.frame.width, view.h / app.frame.height);
  const scale = fit * mapZoom;
  const [cx, cy] = clampedCentre(mapCentre ?? [app.frame.width / 2, app.frame.height / 2], view, scale);
  return {
    view, fit, cell: scale,
    x: view.w / 2 - cx * scale,
    y: view.h / 2 - cy * scale,
    w: scale * app.frame.width,
    h: scale * app.frame.height,
  };
}

function drawMap() {
  if (!app.frame) return;
  const frame = app.frame;
  const geometry = board();
  const { view, cell } = geometry;
  const ctx = mapContext;
  ctx.setTransform(view.scale, 0, 0, view.scale, 0, 0);
  ctx.clearRect(0, 0, view.w, view.h);
  // Each row in its temperature's colour, and the grass: a pixel a cell when cells are small.
  if (cell < 4) {
    if (grassCanvas.width !== frame.width || grassCanvas.height !== frame.height) {
      grassCanvas.width = frame.width;
      grassCanvas.height = frame.height;
    }
    const gctx = grassCanvas.getContext('2d');
    const image = gctx.createImageData(frame.width, frame.height);
    const data = image.data;
    for (let y = 0; y < frame.height; ++y) {
      const zone = zoneColour(frame.temperatures[y]);
      const mixed = zone.map((value, c) => Math.round(value + (GRASS[c] - value) * Math.min(1, 0.4 * cell)));
      for (let x = 0; x < frame.width; ++x) {
        const at = (y * frame.width + x) * 4, colour = frame.grass[y * frame.width + x] ? mixed : zone;
        data[at] = colour[0]; data[at + 1] = colour[1]; data[at + 2] = colour[2]; data[at + 3] = 255;
      }
    }
    gctx.putImageData(image, 0, 0);
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(grassCanvas, geometry.x, geometry.y, geometry.w, geometry.h);
  } else {
    for (let y = 0; y < frame.height; ++y) {
      const top = pyRound(geometry.y + y * cell), bottom = pyRound(geometry.y + (y + 1) * cell);
      if (bottom < 0 || top > view.h) continue;
      ctx.fillStyle = css(zoneColour(frame.temperatures[y]));
      ctx.fillRect(geometry.x, top, geometry.w, bottom - top);
    }
    const radius = Math.max(1, pyRound(cell * 0.18));
    ctx.fillStyle = css(GRASS);
    ctx.beginPath();
    const [x0, x1] = [Math.max(0, Math.floor(-geometry.x / cell) - 1), Math.min(frame.width, Math.ceil((view.w - geometry.x) / cell) + 1)];
    const [y0, y1] = [Math.max(0, Math.floor(-geometry.y / cell) - 1), Math.min(frame.height, Math.ceil((view.h - geometry.y) / cell) + 1)];
    for (let y = y0; y < y1; ++y) {
      for (let x = x0; x < x1; ++x) {
        if (!frame.grass[y * frame.width + x]) continue;
        const px = geometry.x + (x + 0.5) * cell, py = geometry.y + (y + 0.5) * cell;
        ctx.moveTo(px + radius, py);
        ctx.arc(px, py, radius, 0, Math.PI * 2);
      }
    }
    ctx.fill();
  }
  const [smallest, largest] = mapDotRadii(cell);
  const reach = (px, py) => px > -largest * 3 && py > -largest * 3 && px < view.w + largest * 3 && py < view.h + largest * 3;
  const centre = (x, y) => [geometry.x + (x + 0.5) * cell, geometry.y + (y + 0.5) * cell];
  ctx.save();
  ctx.globalAlpha = TRAIL_OPACITY;  // the way each came this turn, faint, beneath them all
  for (const agent of frame.agents) {
    if (!agent.trail.length) continue;
    const radius = dotRadius(agent.size, largest, smallest);
    for (const [x, y] of agent.trail) {
      const [px, py] = centre(x, y);
      if (reach(px, py)) drawBitzkey(ctx, agent, px, py, radius, RING_WIDTH, agent.alive);
    }
  }
  ctx.restore();
  const exporting = new Set(app.exportList.map(entry => entry.identifier));
  for (const agent of frame.agents) {
    const [px, py] = centre(agent.x, agent.y);
    if (!reach(px, py)) continue;
    const radius = dotRadius(agent.size, largest, smallest);
    drawBitzkey(ctx, agent, px, py, radius, RING_WIDTH, agent.alive, agent.opacity);
    if (agent.identifier === app.selectedId || (agent.alive && app.selection.has(agent.identifier))) {
      drawReticle(ctx, px, py, radius + RING_WIDTH + 3);
    }
    if (exporting.has(agent.identifier)) {
      ctx.strokeStyle = css(EXPORTING);
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(px, py, radius + RING_WIDTH + 6, 0, Math.PI * 2);
      ctx.stroke();
    }
  }
  if (marquee && Math.max(Math.abs(marquee.nowX - marquee.x), Math.abs(marquee.nowY - marquee.y)) >= MAP_DRAG_THRESHOLD) {
    const x = Math.min(marquee.x, marquee.nowX), y = Math.min(marquee.y, marquee.nowY);
    const w = Math.abs(marquee.nowX - marquee.x) + 1, h = Math.abs(marquee.nowY - marquee.y) + 1;
    ctx.fillStyle = 'rgba(245,238,206,0.16)';
    ctx.fillRect(x, y, w, h);
    ctx.strokeStyle = 'rgb(245,238,206)';
    ctx.lineWidth = 1;
    ctx.strokeRect(x + 0.5, y + 0.5, w, h);
  }
  ctx.strokeStyle = css(GRID);
  ctx.lineWidth = 1;
  ctx.beginPath();
  if (cell * 10 >= 6) {
    for (let x = 0; x <= frame.width; x += 10) {
      const px = pyRound(geometry.x + x * cell) + 0.5;
      ctx.moveTo(px, geometry.y); ctx.lineTo(px, geometry.y + geometry.h);
    }
    for (let y = 0; y <= frame.height; y += 10) {
      const py = pyRound(geometry.y + y * cell) + 0.5;
      ctx.moveTo(geometry.x, py); ctx.lineTo(geometry.x + geometry.w, py);
    }
  }
  ctx.stroke();
  ctx.strokeStyle = 'rgb(80,99,85)';
  ctx.strokeRect(geometry.x + 0.5, geometry.y + 0.5, geometry.w, geometry.h);
}

function mousePoint(event) {
  const rect = map.getBoundingClientRect();
  return [event.clientX - rect.left, event.clientY - rect.top];
}

function livingAt(x, y) {
  const geometry = board();
  let nearest = null, distance = Infinity;
  for (const agent of app.frame.living.values()) {
    const d = Math.hypot(x - (geometry.x + (agent.x + 0.5) * geometry.cell), y - (geometry.y + (agent.y + 0.5) * geometry.cell));
    if (d < distance) { nearest = agent; distance = d; }
  }
  const inside = x >= geometry.x && y >= geometry.y && x < geometry.x + geometry.w && y < geometry.y + geometry.h;
  return nearest && inside && distance < Math.max(9, geometry.cell * 1.6) ? nearest.identifier : null;
}

function pickOnMap(startX, startY, endX, endY, adding) {
  let picked = new Set();
  if (Math.max(Math.abs(endX - startX), Math.abs(endY - startY)) < MAP_DRAG_THRESHOLD) {
    const hit = livingAt(startX, startY);
    if (adding) {
      picked = new Set(app.selection);
      if (hit !== null && !picked.delete(hit)) picked.add(hit);
    } else if (hit !== null) {
      picked.add(hit);
    }
  } else {
    const geometry = board();
    const [left, right] = [Math.min(startX, endX), Math.max(startX, endX)];
    const [top, bottom] = [Math.min(startY, endY), Math.max(startY, endY)];
    if (adding) picked = new Set(app.selection);
    for (const agent of app.frame.living.values()) {
      const px = geometry.x + (agent.x + 0.5) * geometry.cell, py = geometry.y + (agent.y + 0.5) * geometry.cell;
      if (px >= left && px <= right && py >= top && py <= bottom) picked.add(agent.identifier);
    }
  }
  selectAll(picked);
}

// Exactly these picked, on the map and the graph alike; one picked shows in the inspector.
function selectAll(picked) {
  app.selection = new Set(picked);
  if (app.selection.size === 1) {
    app.selectedId = [...app.selection][0];
    app.showInsights = false;
  } else {
    app.selectedId = null;
  }
  agentCache = null;
  drawMap();
  drawSidebar();
  drawExemplars();
}

function select(identifier) {
  selectAll(new Set([identifier]));
}

map.addEventListener('mousedown', event => {
  if (!app.frame) return;
  const [x, y] = mousePoint(event);
  if (deploying && event.button === 2) {
    stopDeploying();
    event.preventDefault();
  } else if (deploying && event.button === 0) {
    const cell = cellAt(x, y);
    if (cell) deployAt(cell);
  } else if (event.button === 0) {
    marquee = { x, y, nowX: x, nowY: y, adding: event.ctrlKey || event.metaKey };
  } else if (event.button === 1 || event.button === 2) {
    const geometry = board();
    panFrom = { x, y, centre: clampedCentre(mapCentre ?? [app.frame.width / 2, app.frame.height / 2], geometry.view, geometry.cell) };
    event.preventDefault();
  }
});
// The square popup: moving the pointer over a map square lists the Bitzkeys stacked there, and the list stays
// while the pointer is over it; clicking a name selects that Bitzkey.
let square = null;
const squarePanel = element('div', { class: 'square-popup', hidden: true });
$('map-holder').append(squarePanel);

function showSquare() {
  const here = square && app.frame ? app.frame.agents.filter(agent => agent.alive && agent.x === square[0] && agent.y === square[1]) : [];
  if (!here.length) {
    squarePanel.hidden = true;
    return;
  }
  const geometry = board();
  const used = here.reduce((total, agent) => total + 1 + agent.size, 0);
  squarePanel.replaceChildren(
    element('div', { class: 'square-title' }, `SQUARE (${square[0]}, ${square[1]})   ${used}/${app.state?.cell_space ?? 10} SPACES`),
    here.map(agent => element('div', {
      class: agent.identifier === app.selectedId ? 'square-row chosen' : 'square-row',
      onclick: () => { select(agent.identifier); showSquare(); },
    }, `#${pad3(agent.identifier)}  ${upper(SPECIES[agent.species])}   size ${agent.size}  ·  ${1 + agent.size} spaces`)));
  squarePanel.hidden = false;
  const holder = $('map-holder');
  let left = geometry.x + (square[0] + 1) * geometry.cell;  // touching the square, to reach it
  if (left + squarePanel.offsetWidth > holder.clientWidth - 4) left = geometry.x + square[0] * geometry.cell - squarePanel.offsetWidth;
  const top = Math.max(4, Math.min(holder.clientHeight - squarePanel.offsetHeight - 4, geometry.y + square[1] * geometry.cell));
  squarePanel.style.left = `${left}px`;
  squarePanel.style.top = `${top}px`;
}

window.addEventListener('mousemove', event => {
  if (!app.frame) return;
  const [x, y] = mousePoint(event);
  if (!squarePanel.contains(event.target)) {
    square = event.buttons === 0 && !deploying && event.target === map ? cellAt(x, y) : null;
    showSquare();
  }
  if (deploying && painted && event.buttons & 1) {  // painting: another group every DEPLOY_GROUP cells
    const cell = cellAt(x, y);
    if (cell && Math.max(Math.abs(cell[0] - painted[0]), Math.abs(cell[1] - painted[1])) >= DEPLOY_GROUP) deployAt(cell);
  } else if (marquee) {
    marquee.nowX = x;
    marquee.nowY = y;
    drawMap();
  } else if (panFrom) {
    const geometry = board();
    mapCentre = clampedCentre([panFrom.centre[0] - (x - panFrom.x) / geometry.cell, panFrom.centre[1] - (y - panFrom.y) / geometry.cell],
                              geometry.view, geometry.cell);
    drawMap();
  }
});
window.addEventListener('mouseup', event => {
  if (deploying && event.button === 0) painted = null;
  if (marquee && event.button === 0) {
    const [x, y] = mousePoint(event);
    const { x: startX, y: startY, adding } = marquee;
    marquee = null;
    pickOnMap(startX, startY, x, y, adding);
  }
  if (panFrom && (event.button === 1 || event.button === 2)) panFrom = null;
});
map.addEventListener('contextmenu', event => event.preventDefault());
map.addEventListener('wheel', event => {
  if (!app.frame) return;
  event.preventDefault();
  const [x, y] = mousePoint(event);
  const geometry = board();
  const cellX = (x - geometry.x) / geometry.cell, cellY = (y - geometry.y) / geometry.cell;  // stays under the pointer
  const notches = event.deltaY < 0 ? 1 : -1;
  mapZoom = Math.max(1, Math.min(MAP_ZOOM_MAX, mapZoom * MAP_ZOOM_STEP ** notches));
  const scale = geometry.fit * mapZoom;
  mapCentre = clampedCentre([cellX - (x - geometry.view.w / 2) / scale, cellY - (y - geometry.view.h / 2) / scale], geometry.view, scale);
  drawMap();
}, { passive: false });
new ResizeObserver(() => drawMap()).observe($('map-holder'));

// ---------------------------------------------------------------------------------------------
// The exemplar cards

let exemplarStats = null;
let exemplarCards = [];

async function loadExemplars() {
  if (!exemplarStats) {
    const loaded = await api('/api/exemplar_stats');
    exemplarStats = loaded.stats;
    if (!Array.isArray(exemplarChoices) || exemplarChoices.length !== 10) exemplarChoices = loaded.defaults;
  }
  exemplarCards = await api('/api/exemplars', { choices: exemplarChoices });
  drawExemplars();
}

function drawExemplars() {
  if (!exemplarStats) return;
  for (const [side, holder] of [[0, $('exemplars-left')], [1, $('exemplars-right')]]) {
    if (holder.childElementCount !== 5) {
      holder.replaceChildren(...[0, 1, 2, 3, 4].map(index => {
        const slot = side * 5 + index;
        const menu = element('select', { title: 'What this card shows' },
          exemplarStats.map((title, stat) => element('option', { value: stat }, title)));
        menu.addEventListener('change', () => {
          exemplarChoices[slot] = Number(menu.value);
          remember('exemplars', exemplarChoices);
          loadExemplars();
        });
        menu.addEventListener('mousedown', event => event.stopPropagation());
        menu.addEventListener('click', event => event.stopPropagation());
        const card = element('div', { class: 'card' }, menu, element('div', { class: 'card-body' }));
        card.addEventListener('click', () => {
          const identifier = exemplarCards[slot]?.identifier;
          if (identifier !== undefined) select(identifier);
        });
        return card;
      }));
    }
    for (let index = 0; index < 5; ++index) {
      const slot = side * 5 + index, card = holder.children[index], found = exemplarCards[slot];
      const menu = card.firstChild;
      if (document.activeElement !== menu) menu.value = exemplarChoices[slot];
      if (!found) continue;
      card.classList.toggle('chosen', found.identifier !== undefined && found.identifier === app.selectedId);
      const key = JSON.stringify([found.identifier, found.said]);
      const body = card.lastChild;
      if (body.dataset.key === key) continue;
      body.dataset.key = key;
      if (found.identifier === undefined) {
        body.replaceChildren(element('div', { class: 'card-none' }, found.said));
        continue;
      }
      const radius = Math.max(4, Math.min(16, Math.floor(card.clientHeight / 5)));
      body.replaceChildren(
        bitzkeyCanvas({ dot: found.dot_rgb, ring: found.ring_rgb, social: found.social_rgb, species: found.species_index }, radius),
        element('div', { class: 'card-text' },
          element('span', { class: 'card-name' }, `#${pad3(found.identifier)}  ${found.species}`),
          element('span', { class: 'card-said' }, found.said)));
    }
  }
}

// ---------------------------------------------------------------------------------------------
// The sidebar: the selected Bitzkey, the insights, or neither

async function drawSidebar() {
  const holder = $('sidebar-body');
  if (app.showInsights) {
    const data = await api('/api/insights');
    if (!app.showInsights) return;
    const scroll = holder.scrollTop;
    holder.replaceChildren(insightsPanel(data));
    holder.scrollTop = scroll;
    return;
  }
  if (app.selectedId === null) {
    holder.replaceChildren(...[
      element('div', { class: 'heading' }, 'SELECTED BITZKEY'),
      element('div', { class: 'hint' }, 'Click a dot to inspect its genome; drag a box to pick several.'),
      element('div', { class: 'faint' }, 'CTRL+click adds or takes one out.'),
      app.selection.size ? element('div', { class: 'picked' }, `${app.selection.size} picked on the map and the colour graph.`) : null,
      app.selection.size ? element('div', { class: 'sidebar-buttons' },
        element('button', { onclick: () => catalogIdentifiers([...app.selection]) }, 'ADD TO CATALOG'),
        element('button', { onclick: () => exportIdentifiers([...app.selection], false) }, 'COPY')) : null].filter(Boolean));
    return;
  }
  const identifier = app.selectedId;
  let agent;
  try {
    agent = await api(`/api/agent/${identifier}`);
  } catch {
    return;
  }
  if (app.selectedId !== identifier || app.showInsights) return;
  agentCache = agent;
  const listed = app.exportList.some(entry => entry.identifier === identifier);
  holder.replaceChildren(
    element('div', { class: 'heading-row' }, element('div', { class: 'heading' }, 'SELECTED BITZKEY'),
      agent.alive !== false ? element('button', { onclick: () => inspectBrains(identifier) }, 'INSPECT BRAINS') : null),
    details(agent),
    element('div', { class: 'faint keys', style: { marginTop: '14px' } }, 'C  copy as JSON      A  export      K  catalog'),
    element('div', { class: 'sidebar-buttons' },
      element('button', { class: listed ? 'lit' : '', onclick: toggleExport }, listed ? 'REMOVE FROM EXPORT' : 'ADD TO EXPORT'),
      element('button', { onclick: () => catalogIdentifiers([identifier]) }, 'ADD TO CATALOG')));
}

function insightsPanel(data) {
  const panel = element('div', { class: 'insights' },
    element('div', { class: 'panel-tools' }, element('button', { onclick: openDeathReport }, 'DEATH REPORT')));
  const heads = data.species.map(name => element('th', {}, upper(name.slice(0, 4))));
  const table = (title, note, rows) => {
    panel.append(element('h3', {}, title), element('div', { class: 'sub' }, note));
    panel.append(element('table', {}, element('tr', {}, element('th', {}, ''), heads.map(head => head.cloneNode(true))), rows));
  };
  table('BEHAVIOUR', '% of chances taken, recent turns',
    data.behaviour.map(row => element('tr', {}, element('td', {}, upper(row.action)), row.shares.map(share => element('td', { class: 'num' }, share)))));
  for (const block of data.blocks) {
    panel.append(element('div', { class: 'block-title' }, block.title));
    if (!block.groups.length) panel.append(element('div', { class: 'faint' }, 'not enough chances yet'));
    for (const group of block.groups) panel.append(element('div', { class: 'group' }, element('span', {}, group.label), element('span', {}, group.share)));
  }
  table('LIFE', 'averages of the living',
    data.life.map(row => element('tr', {}, element('td', {}, row.label), row.values.map(value => element('td', { class: 'num' }, String(value).padStart(4))))));
  table('TEMPERAMENT', 'average now; green above inborn, amber below',
    data.temperament.map(row => element('tr', {}, element('td', {}, upper(row.trait)),
      row.values.map(value => element('td', { class: `num ${value === null ? '' : value.shift > 0 ? 'up' : value.shift < 0 ? 'down' : ''}` },
        value === null ? '   -' : value.value)))));
  panel.append(element('h3', {}, 'KEENEST BY ACTION'), element('div', { class: 'sub' }, 'the group most likely to take each chance'));
  if (!data.keenest.length) panel.append(element('div', { class: 'faint' }, 'not enough chances yet'));
  for (const row of data.keenest) {
    panel.append(element('div', { class: 'group' },
      element('span', { style: { color: 'var(--muted)', width: '76px', flex: 'none' } }, upper(row.action)),
      element('span', { style: { flex: '1', fontFamily: 'var(--sans)', color: 'var(--soft)', overflow: 'hidden', textOverflow: 'ellipsis' } }, row.label),
      element('span', {}, row.share)));
  }
  return panel;
}

// ---------------------------------------------------------------------------------------------
// The death report and a brain inspected

// A table laid over the page: a title, a note, a header row and rows; ESC or CLOSE closes it.
function showTable(title, note, header, rows) {
  const close = () => {
    document.removeEventListener('keydown', onKey, true);
    shade.remove();
  };
  const onKey = event => {
    if (event.key === 'Escape' || event.key === 'Enter') {
      event.stopPropagation();
      event.preventDefault();
      close();
    }
  };
  const shade = element('div', { class: 'shade dialog-shade', onclick: event => { if (event.target === shade) close(); } },
    element('div', { class: 'dialog report' },
      element('h2', {}, title),
      element('p', {}, note),
      element('div', { class: 'report-body scroll' },
        element('table', { class: 'report-table' },
          element('thead', {}, element('tr', {}, header.map(cell => element('th', {}, cell)))),
          element('tbody', {}, rows.map(row => element('tr', {}, row.map(cell =>
            cell instanceof Node ? cell : element('td', {}, cell))))))),
      element('div', { class: 'dialog-buttons' }, element('button', { class: 'primary', onclick: close }, 'CLOSE'))));
  document.body.append(shade);
  document.addEventListener('keydown', onKey, true);
}

async function openDeathReport() {
  try {
    const data = await api('/api/deaths');
    const totals = data.species.map((_, index) => data.rows.reduce((sum, row) => sum + row.counts[index], 0));
    const rows = data.rows.map(row => [row.cause, ...row.counts.map(String), String(row.total)]);
    rows.push([element('td', { class: 'total' }, 'ALL'), ...[...totals, data.deaths].map(count => element('td', { class: 'total' }, String(count)))]);
    showTable('Death report', `Every death in this world up to turn ${data.turn}: ${data.deaths} in all, by cause and species.`,
              ['CAUSE', ...data.species.map(upper), 'ALL'], rows);
  } catch (error) {
    say(error.message);
  }
}

async function inspectBrains(identifier) {
  await control('pause');  // the world waits while it is looked at, for everyone viewing it
  try {
    const data = await api(`/api/brain/${identifier}`);
    const rows = data.rows.map(row => {
      const best = Object.entries(row.chances).reduce((most, entry) => entry[1] > most[1] ? entry : most)[0];
      return [element('td', { class: 'situation' }, row.situation), ...data.actions.map(action => {
        const chance = row.chances[action.name];
        if (chance === undefined) return element('td', { class: 'closed' }, '·');
        return element('td', { class: action.name === best ? 'best' : '' }, `${Math.round(chance * 100)}%`);
      })];
    });
    showTable(`Brains of #${pad3(identifier)}, ${data.species}`,
              'Its chance of choosing each action open to it, in each standard situation, with its temperament now. ' +
              'The likeliest is lit; · marks an action not open. The world is paused.',
              ['SITUATION', ...data.actions.map(action => action.short)], rows);
  } catch (error) {
    say(error.message);
  }
}

// ---------------------------------------------------------------------------------------------
// Exporting and the catalog

async function toggleExport() {
  const identifier = app.selectedId;
  if (identifier === null) {
    say('Select a Bitzkey first, then add it to export.');
    return;
  }
  const found = app.exportList.findIndex(entry => entry.identifier === identifier);
  if (found >= 0) {
    app.exportList.splice(found, 1);
    say(`Removed #${pad3(identifier)} from the export list (${app.exportList.length} on it).`);
  } else {
    const records = await api('/api/records', { identifiers: [identifier], living_only: true });
    if (!records.entries.length) {
      say('Only a living Bitzkey can be added to export.');
      return;
    }
    app.exportList.push(records.entries[0]);
    say(`Added #${pad3(identifier)} to the export list (${app.exportList.length} on it).`);
  }
  updateExportButton();
  drawMap();
  drawSidebar();
}

async function openExportDialog() {
  const count = app.exportList.length;
  if (!count) {
    await dialog('Export all selected', ['Nothing has been added to export yet.', 'Select a Bitzkey and press ADD TO EXPORT (or A).'],
                 [['close', 'CLOSE']], 'close');
    return;
  }
  const living = app.frame ? app.exportList.filter(entry => app.frame.living.has(entry.identifier)).length : count;
  const gone = count - living;
  const lines = [`${plural(count)} added to export.`, 'The living export as they are now, with what they have learned since.'];
  if (gone) lines.push(`${gone}${gone !== 1 ? ' have' : ' has'} died; they export as they were when added.`);
  lines.push('Save them to a file in the daemon\'s exports/ (and download it), copy them, or add them to the catalog.');
  const choice = await dialog('Export all selected', lines,
    [['clear', 'CLEAR LIST'], ['cancel', 'CANCEL'], ['catalog', 'CATALOG'], ['copy', 'COPY'], ['save', 'SAVE FILE']], 'cancel');
  if (choice === 'save' || choice === 'copy') {
    const result = await api('/api/export', { list: app.exportList, save: choice === 'save' });
    await deliver(result, choice === 'save');
  } else if (choice === 'catalog') {
    say((await api('/api/catalog/add', { list: app.exportList })).note);
  } else if (choice === 'clear') {
    app.exportList = [];
    updateExportButton();
    drawMap();
    say(`Cleared ${plural(count)} from the export list.`);
  }
}

async function deliver(result, toFile) {
  if (!result.text) {
    say(result.note);
    return;
  }
  if (toFile) {
    download(result.name, `${result.text}\n`);
    say(result.note);
  } else if (await copyText(result.text)) {
    say(result.note);
  }
}

// The Bitzkeys by identifier, or everyone alive if none, copied or saved.
async function exportIdentifiers(identifiers, toFile) {
  try {
    await deliver(await api('/api/export', { identifiers, save: toFile }), toFile);
  } catch (error) {
    say(error.message);
  }
}

async function catalogIdentifiers(identifiers) {
  if (!identifiers.length) {
    say('Select a living Bitzkey first, then add it to the catalog.');
    return;
  }
  try {
    say((await api('/api/catalog/add', { identifiers })).note);
  } catch (error) {
    say(`Couldn't add to the catalog: ${error.message}`);
  }
}

async function copySelected() {
  if (app.selectedId === null) {
    say('Click a Bitzkey first, then press C to copy its genome.');
    return;
  }
  const records = await api('/api/records', { identifiers: [app.selectedId] });
  if (records.text && await copyText(records.text)) say(`Copied #${pad3(app.selectedId)}'s genome and brain to the clipboard.`);
}

async function saveReport() {
  try {
    const result = await api('/api/report', {});
    say(result.note);
    window.open(`/api/reports/${encodeURIComponent(result.name)}`, '_blank');
  } catch (error) {
    say(`Couldn't save the report: ${error.message}`);
  }
}

async function showFiles() {
  const listed = await api('/api/exports');
  const close = () => shade.remove();
  const shade = element('div', { class: 'shade dialog-shade', onclick: event => { if (event.target === shade) close(); } },
    element('div', { class: 'dialog' },
      element('h2', {}, 'Saved exports'),
      listed.files.length ? null : element('p', {}, 'Nothing has been saved yet.'),
      element('div', { class: 'files scroll' }, listed.files.map(file => element('a', { href: `/api/exports/${encodeURIComponent(file.name)}`, download: file.name },
        element('span', {}, file.name), element('span', {}, `${Math.ceil(file.size / 1024)} KB`)))),
      element('div', { class: 'dialog-buttons' }, element('button', { class: 'primary', onclick: close }, 'CLOSE'))));
  document.body.append(shade);
}

// A new world with the Bitzkeys picked, at once: the settings as they were, these alone imported.
async function renew(identifiers) {
  const records = await api('/api/records', { identifiers, living_only: true });
  if (!records.count) {
    say('Pick living Bitzkeys on the graph first, then start a new world with them.');
    return false;
  }
  await copyText(records.text);
  const setup = await api('/api/setup');
  const settings = Object.fromEntries(setup.settings.map(setting => [setting.key, setting.value]));
  settings.import = records.text;
  remember('import', records.text);
  try {
    const result = await api('/api/new_world', { settings, rules: setup.changed });
    applyState(result);
    say(`A new world with ${plural(records.count)}.`);
    await poll(true);
    return true;
  } catch (error) {
    say(error.message);
    return false;
  }
}

// ---------------------------------------------------------------------------------------------
// Keys and buttons

async function confirmRestart() {
  const choice = await dialog('Restart this world?', ['A new world from the same settings replaces this one,', 'for everyone viewing it.'],
                              [['stay', 'CANCEL'], ['restart', 'RESTART']], 'stay');
  if (choice === 'restart') {
    await control('restart');
    poll(true);
  }
}

function graph() {
  if (app.overlay) return;
  app.overlay = openGraph(app, newWorld => {
    app.overlay = null;
    drawMap();
    drawSidebar();
    if (newWorld) setup();
  });
}

function setup() {
  if (app.overlay) return;
  app.overlay = openSetup(app, () => { app.overlay = null; poll(true); });
}

// Look through the catalog, importing and exporting nothing. Over the graph, it returns there.
// Pause, and change the rules this world plays by: applied, they hold from its next turn, and for its restarts.
async function editRules() {
  await control('pause');
  const setup = await api('/api/setup');
  const under = app.overlay;
  app.overlay = { update() {} };
  openRules({
    ruleList: setup.rules, changed: setup.changed, running: true,
    onApply: async values => {
      const result = await api('/api/rules/world', { rules: values });
      if (result.problems.length) return `${result.problems[0]}.`;
      say(result.note);
      showRulesChanged(Object.keys(result.changed).length);
      return null;
    },
    onClose: () => { app.overlay = under; },
  });
}

// The ADVANCED button shows amber while the world plays by changed rules.
function showRulesChanged(count) {
  $('advanced').classList.toggle('any', count > 0);
}

function browseCatalog() {
  const under = app.overlay;
  app.overlay = { update() {} };
  openCatalog({ onDeploy: startDeploying, onClose: () => { app.overlay = under; } });
}

// Deploy mode: the cursor becomes a target, and each click on the map, or each step of a drag, puts DEPLOY_GROUP of
// each catalogued Bitzkey chosen there; a right-click or ESC stops.
const DEPLOY_GROUP = 5;
let deploying = null;  // the chosen entries' places in the catalog, while deploying
let painted = null;    // the cell last painted while the button is held

function startDeploying(indexes) {
  deploying = indexes;
  painted = null;
  map.style.cursor = 'crosshair';
  say(`DEPLOY: click or drag on the map to place ${DEPLOY_GROUP} of ${indexes.length === 1 ? 'it' : `each of the ${indexes.length}`}.  RIGHT-CLICK or ESC  stop`);
}

function stopDeploying() {
  deploying = painted = null;
  map.style.cursor = '';
  say('Deploying stopped.');
}

function cellAt(x, y) {
  const geometry = board();
  const cellX = Math.floor((x - geometry.x) / geometry.cell), cellY = Math.floor((y - geometry.y) / geometry.cell);
  return cellX >= 0 && cellX < app.frame.width && cellY >= 0 && cellY < app.frame.height ? [cellX, cellY] : null;
}

async function deployAt(cell) {
  painted = cell;
  try {
    say((await api('/api/deploy', { indexes: deploying, x: cell[0], y: cell[1] })).note);
    loadFrame();  // show them at once, even paused
  } catch (error) {
    say(error.message);
  }
}

function toggleInsights() {
  app.showInsights = !app.showInsights;
  $('sidebar-body').scrollTop = 0;
  drawSidebar();
}

document.addEventListener('keydown', event => {
  if (app.overlay || document.querySelector('.dialog-shade')) return;
  if (deploying && event.key === 'Escape') {  // stops deploying, rather than leaving for a new world
    event.preventDefault();
    stopDeploying();
    return;
  }
  if (event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement || event.target instanceof HTMLSelectElement) return;
  if (event.ctrlKey || event.altKey || event.metaKey) return;
  const state = app.state;
  if (!state) return;
  const key = event.key;
  const handled = () => event.preventDefault();
  if (key === ' ') { handled(); control('toggle'); }
  else if (key === 'ArrowRight') { handled(); control('step'); }
  else if (key === 'ArrowLeft') { handled(); control('rewind'); }
  else if (key === '+' || key === '=') control('speed', { speed: Math.min(state.speed + 1, SPEED_COUNT - 1) });
  else if (key === '-' || key === '_') control('speed', { speed: Math.max(state.speed - 1, 0) });
  else if (key === 'r' || key === 'R') confirmRestart();
  else if (key === 'i' || key === 'I') toggleInsights();
  else if (key === 'b' || key === 'B') saveReport();
  else if (key === 'g' || key === 'G') graph();
  else if (key === 'c' || key === 'C') copySelected();
  else if (key === 'a' || key === 'A') toggleExport();
  else if (key === 'e' || key === 'E') openExportDialog();
  else if (key === 'k' || key === 'K') catalogIdentifiers(app.selectedId !== null ? [app.selectedId] : []);
  else if (key === 'z' || key === 'Z') { mapZoom = 1; mapCentre = null; drawMap(); }
  else if (key === 'Escape') setup();
});

$('play').onclick = () => control('toggle');
$('step').onclick = () => control('step');
$('rewind').onclick = () => control('rewind');
$('faster').onclick = () => control('speed', { speed: Math.min(app.state.speed + 1, SPEED_COUNT - 1) });
$('slower').onclick = () => control('speed', { speed: Math.max(app.state.speed - 1, 0) });
$('show-insights').onclick = toggleInsights;
$('show-graph').onclick = graph;
$('save-report').onclick = saveReport;
$('show-files').onclick = showFiles;
$('new-world').onclick = setup;
$('export-all').onclick = openExportDialog;
$('open-catalog').onclick = () => { if (!app.overlay) browseCatalog(); };
$('advanced').onclick = () => { if (!app.overlay) editRules(); };
$('message').textContent = HINT;

window.bitzkey = app;  // for looking into from the browser's console
loop();
