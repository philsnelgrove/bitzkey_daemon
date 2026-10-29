// The main screen: the map, the exemplar cards either side of it, the sidebar, and the keys, as in
// the window app. The world is the daemon's, shared by every viewer; what this viewer has selected,
// its export list and its exemplar cards stay in this browser.

import { startLanguages, tr } from './i18n.js';
import { api, binary, element, dialog, copyText, download, remembered, remember, pad3, pad5, plural, upper } from './common.js';
import { drawBitzkey, drawReticle, zoneColour, seaColour, mapDotRadii, dotRadius, bitzkeyCanvas, iconCanvas, css, pyRound,
         GRASS, SEA_GRASS, NIGHT, GRID, TEXTURE_FROM, GROUND_VARIANTS, GROUNDS, groundTile, grassTuft, EXPORTING, RING_WIDTH, SPECIES, settings, heat } from './draw.js';
import { details, look } from './inspector.js';
import { openGraph } from './graph.js';
import { openSetup, openCatalog, openRules } from './setup.js';
import { saveWorld } from './worlds.js';
import { openWorkers, showWorkerPanel } from './workers.js';
import './sound.js';  // a button pressed clicks

await startLanguages();  // before anything is drawn, so it appears in the chosen language

const MAP_ZOOM_STEP = 1.25;
const MAP_ZOOM_MAX = 12;
// The level of detail: zoomed out all the way, only the largest Bitzkeys (size 7) are drawn, and each step in shows
// the next size down, until from ALL_SIZES_AT of the way in (on the wheel's scale) every size shows. The one chosen,
// those picked and those to export are drawn whatever their size.
const ALL_SIZES_AT = 0.85;
const TRAIT_MAX = 7;
const TOO_FAR = 'Zoomed out too far to see anybody';
// The smallest size drawn at `zoom`, between `least` (all the way out) and `most` (all the way in).
function smallestShown(zoom, least, most) {
  const inward = Math.log(zoom / least) / Math.log(most / least);  // 0 all the way out, 1 all the way in
  return Math.max(0, Math.min(TRAIT_MAX, Math.ceil(TRAIT_MAX * (1 - inward / ALL_SIZES_AT) - 1e-9)));
}
// Whether a Bitzkey is drawn whatever its size: the one chosen, one picked, or one to export.
function marked(agent) {
  return agent.identifier === app.selectedId || app.selection.has(agent.identifier) ||
    app.exportList.some(entry => entry.identifier === agent.identifier);
}
// Whether a Bitzkey is drawn, `least` the smallest size the zoom shows: only while its species and terrain are both
// shown, and every size while LOD MANAGER is off.
function shownAt(agent, least) {
  if (marked(agent)) return true;
  if (filteredOut(agent)) return false;
  return !lod || agent.size >= least;
}
// Hidden by the filters: its species or its terrain off.
const filteredOut = agent => !speciesShown[agent.species] || !terrainShown[agent.terrain];
// Whether one not drawn counts as too small to see from this far out, rather than filtered out.
const tooSmall = agent => !filteredOut(agent);
// Every Bitzkey in view too small to draw from this far out: say so, in the middle of the view.
function sayTooFar(ctx, view) {
  const text = tr(TOO_FAR);
  ctx.save();
  ctx.font = '15px "Segoe UI", sans-serif';
  const width = ctx.measureText(text).width + 28;
  ctx.fillStyle = 'rgba(19,26,27,0.86)';
  ctx.fillRect(view.w / 2 - width / 2, view.h / 2 - 17, width, 34);
  ctx.fillStyle = 'rgb(238,183,110)';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(text, view.w / 2, view.h / 2);
  ctx.restore();
}
const MAP_DRAG_THRESHOLD = 5;
const TRAIL_OPACITY = 0.5;
const POLL_MS = 120;
const SLOW_POLL_MS = 1000;
const FRAME_MS = 250;  // the map and the graph refresh at most four times a second, however fast the world runs
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
let heardVersion = -1;  // the latest version the daemon has told of
let shownWorld = -1;
let mapZoom = 1;
let mapCentre = null;     // [x, y] in cells, or null for the middle
// The globe (see drawGlobe):
const GLOBE_SHARE = 0.46;     // its radius, this share of the view's smaller side at no zoom
const GLOBE_ZOOM_MAX = 8;
const GLOBE_TEXTURE = 1600;   // the map drawn onto it from a picture about this many pixels across
const STACK_SPACING = 0.7;    // a stack's Bitzkeys each this share of the one below's radius above it, its face showing
let globe = false;
let blind = false;            // neither map nor globe drawn, to spare the time
let lod = true;               // the level of detail hides the smaller Bitzkeys zoomed out; off, every size shows from any altitude
const speciesShown = [true, true, true, true];  // each species drawn, or filtered out: plants, grazers, omnivores, predators
const terrainShown = [true, true, true, true];  // each terrain drawn, or filtered out: terrestrial, aquatic, avian, amphibious
let globeYaw = 0, globeTilt = 0.35, globeZoom = 1;
let turnFrom = null;          // a drag on the globe: where it began, and its turn then
let globeLook = null;         // where each pixel of the disc lies on the map, kept while its size and tilt stay
let globeHits = [];           // each living Bitzkey drawn on the globe: x, y, reach, identifier
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

const HINT = 'SPACE  pause   -/+  speed   RIGHT  step   WHEEL / Z  zoom   I  insights   B  report   ' +
             'G  graph   E  export   R  restart   TAB  next   F8  colour-blind   F9  text size   ESC  new world';

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

let workerMode = false;  // a worker shows only its own panel, and fetches nothing of a world

async function poll(force = false) {
  try {
    // The next state as soon as the world has one (the daemon waits a while for it), or this one when asked to
    // (only once what it shows is all it has heard of: a frame held back is fetched without waiting)
    const waiting = !force && shownVersion >= 0 && shownVersion === heardVersion;
    const state = await api(waiting ? `/api/state?after=${shownVersion}` : '/api/state');
    heardVersion = state.version;
    $('lost').hidden = true;
    if (state.about && window.bitzkeyAbout !== state.about) {  // the version running and the cores it thinks on
      window.bitzkeyAbout = state.about;
      $('about').textContent = `/  ${state.about}`;
    }
    if (state.mode === 'worker') {
      workerMode = true;
      app.overlay = { update() {} };  // the keys steer nothing
      showWorkerPanel();
      return;
    }
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
  if (!workerMode) setTimeout(loop, POLL_MS);
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

// A genome word's colour: its four bytes as cyan, magenta, yellow and black ink, as the daemon reads them.
const wordColours = new Map();
function colourOfWord(word) {
  let colour = wordColours.get(word);
  if (!colour) {
    const black = word & 255;
    const channel = ink => pyRound((255 - ink) * (255 - black) / 255);
    colour = [channel(word >>> 24), channel((word >>> 16) & 255), channel((word >>> 8) & 255)];
    if (wordColours.size > 50000) wordColours.clear();
    wordColours.set(word, colour);
  }
  return colour;
}

function parseFrame(view) {
  const width = view.getUint32(4, true), height = view.getUint32(8, true);
  const turn = view.getUint32(12, true), count = view.getUint32(16, true), trailCount = view.getUint32(20, true);
  let offset = 24;
  const temperatures = new Float32Array(height);
  for (let y = 0; y < height; ++y, offset += 4) temperatures[y] = view.getFloat32(offset, true);
  const grass = new Uint8Array(view.buffer, offset, width * height);  // a byte a square: 1 for grass, and 2 for the sea
  offset += width * height;
  const agents = new Array(count);
  const living = new Map();
  for (let index = 0; index < count; ++index, offset += 24) {
    const social = view.getUint32(offset + 20, true);
    const agent = {
      identifier: view.getUint32(offset, true),
      x: view.getUint16(offset + 4, true),
      y: view.getUint16(offset + 6, true),
      species: view.getUint8(offset + 8),
      size: view.getUint8(offset + 9),
      alive: view.getUint8(offset + 10) === 1,
      opacity: view.getUint8(offset + 11) / 255,
      dot: colourOfWord(view.getUint32(offset + 12, true)),  // its colours, from its genome's words
      ring: colourOfWord(view.getUint32(offset + 16, true)),
      social: colourOfWord(social),
      terrain: (social >>> TERRAIN_SHIFT) & 3,
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
  $('year').textContent = `MONTH ${state.month}, YEAR ${state.year}`;
  $('season').textContent = upper(state.season);
  $('season').classList.toggle('winter', state.winter);
  const population = $('population');
  if (population.childElementCount !== SPECIES.length) {
    population.replaceChildren(...SPECIES.map(name => element('div', { class: 'row' }, element('span', {}, upper(name)), element('span', {}, '0'))));
  }
  SPECIES.forEach((name, index) => { population.children[index].lastChild.textContent = state.species[index]; });
  $('history').textContent = `TURN  ${state.turn}`;
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
  if (blind) {
    ctx.fillStyle = 'rgb(116,139,124)';
    ctx.font = '15px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText("BLIND: the map isn't drawn", view.w / 2, view.h / 2);
    ctx.textAlign = 'start';
    return;
  }
  if (globe) {
    drawGlobe(frame, ctx, view);
    return;
  }
  // Each row in its temperature's colour, and the grass: a pixel a cell when cells are small.
  if (cell < 4) {
    if (grassCanvas.width !== frame.width || grassCanvas.height !== frame.height) {
      grassCanvas.width = frame.width;
      grassCanvas.height = frame.height;
    }
    const gctx = grassCanvas.getContext('2d');
    const image = gctx.createImageData(frame.width, frame.height);
    const data = image.data;
    const mix = (ground, tuft) => ground.map((value, c) => Math.round(value + (tuft[c] - value) * Math.min(1, 0.4 * cell)));
    for (let y = 0; y < frame.height; ++y) {
      const zone = zoneColour(frame.temperatures[y]), sea = seaColour(frame.temperatures[y]);
      const colours = [zone, mix(zone, GRASS), sea, mix(sea, SEA_GRASS)];  // by the square's byte
      for (let x = 0; x < frame.width; ++x) {
        const at = (y * frame.width + x) * 4, colour = colours[frame.grass[y * frame.width + x] & 3];
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
      ctx.fillStyle = css(seaColour(frame.temperatures[y]));
      for (let x = 0; x < frame.width;) {  // each stretch of sea along the row at once
        if (!(frame.grass[y * frame.width + x] & 2)) { ++x; continue; }
        let end = x;
        while (end < frame.width && frame.grass[y * frame.width + end] & 2) ++end;
        const left = pyRound(geometry.x + x * cell), right = pyRound(geometry.x + end * cell);
        ctx.fillRect(left, top, right - left, bottom - top);
        x = end;
      }
    }
    const radius = Math.max(1, pyRound(cell * 0.18));
    const [x0, x1] = [Math.max(0, Math.floor(-geometry.x / cell) - 1), Math.min(frame.width, Math.ceil((view.w - geometry.x) / cell) + 1)];
    const [y0, y1] = [Math.max(0, Math.floor(-geometry.y / cell) - 1), Math.min(frame.height, Math.ceil((view.h - geometry.y) / cell) + 1)];
    const size = Math.round(cell);
    if (cell >= TEXTURE_FROM) {  // the ground's texture by the temperature now (sand to ice), or waves at sea; grass as tufts
      for (let y = y0; y < y1; ++y) {
        const land = GROUNDS[Math.max(0, Math.min(3, Math.round(frame.temperatures[y])))];
        for (let x = x0; x < x1; ++x) {
          const square = frame.grass[y * frame.width + x];
          const variant = ((Math.imul(x, 73856093) ^ Math.imul(y, 19349663)) >>> 0) % GROUND_VARIANTS;
          ctx.drawImage(groundTile(square & 2 ? 'sea' : land, size, variant), geometry.x + x * cell, geometry.y + y * cell, cell, cell);
          if (square & 1) ctx.drawImage(grassTuft(size, (square & 2) !== 0), geometry.x + x * cell, geometry.y + y * cell, cell, cell);
        }
      }
    } else for (const [under, colour] of [[0, GRASS], [2, SEA_GRASS]]) {  // grass on the land, then in the sea
      ctx.fillStyle = css(colour);
      ctx.beginPath();
      for (let y = y0; y < y1; ++y) {
        for (let x = x0; x < x1; ++x) {
          const square = frame.grass[y * frame.width + x];
          if (!(square & 1) || (square & 2) !== under) continue;
          const px = geometry.x + (x + 0.5) * cell, py = geometry.y + (y + 0.5) * cell;
          ctx.moveTo(px + radius, py);
          ctx.arc(px, py, radius, 0, Math.PI * 2);
        }
      }
      ctx.fill();
    }
  }
  {  // each climate band named where it starts, at the left of what is seen, so the zones read without colour
    ctx.font = '11px "Segoe UI", sans-serif';
    ctx.textBaseline = 'top';
    let lastZone = -1;
    for (let y = 0; y < frame.height; ++y) {
      const zone = Math.max(0, Math.min(HOMES.length - 1, Math.round(frame.temperatures[y])));
      if (zone === lastZone) continue;
      lastZone = zone;
      const top = Math.max(geometry.y + y * cell, 0) + 2, left = Math.max(geometry.x, 0) + 4;
      ctx.fillStyle = 'rgb(0,0,0)';
      ctx.fillText(HOMES[zone], left + 1, top + 1);
      ctx.fillStyle = 'rgb(240,244,236)';
      ctx.fillText(HOMES[zone], left, top);
    }
  }
  const day = app.state?.day_length;
  if (day) {  // the night, a shadow over half the map's columns, moving west round it once a day, as the daemon has it
    const width = frame.width, columns = Math.floor(width / 2);
    const start = (((Math.trunc(-(frame.turn % day) * width / day)) % width) + width) % width;
    ctx.fillStyle = NIGHT;
    for (const [from, count] of [[start, Math.min(columns, width - start)], [0, Math.max(0, start + columns - width)]]) {
      if (count > 0) ctx.fillRect(geometry.x + from * cell, geometry.y, count * cell, geometry.h);
    }
  }
  const [smallest, largest] = mapDotRadii(cell);
  const least = smallestShown(mapZoom, 1, MAP_ZOOM_MAX);
  const shown = agent => shownAt(agent, least);
  const reach = (px, py) => px > -largest * 3 && py > -largest * 3 && px < view.w + largest * 3 && py < view.h + largest * 3;
  const centre = (x, y) => [geometry.x + (x + 0.5) * cell, geometry.y + (y + 0.5) * cell];
  ctx.save();
  ctx.globalAlpha = TRAIL_OPACITY;  // the way each came this turn, faint, beneath them all
  for (const agent of frame.agents) {
    if (!agent.trail.length || !shown(agent)) continue;
    const radius = dotRadius(agent.size, largest, smallest);
    for (const [x, y] of agent.trail) {
      const [px, py] = centre(x, y);
      if (reach(px, py)) drawBitzkey(ctx, agent, px, py, radius, RING_WIDTH, agent.alive);
    }
  }
  ctx.restore();
  const exporting = new Set(app.exportList.map(entry => entry.identifier));
  let drawn = 0, hidden = 0;
  for (const agent of frame.agents) {
    const [px, py] = centre(agent.x, agent.y);
    if (!reach(px, py)) continue;
    if (!shown(agent)) {  // too small to see from this far out, or a plant hidden
      if (tooSmall(agent)) hidden += 1;
      continue;
    }
    drawn += 1;
    const radius = dotRadius(agent.size, largest, smallest);
    drawBitzkey(ctx, agent, px, py, radius, RING_WIDTH, agent.alive, agent.opacity);
    if (agent.identifier === app.selectedId || (agent.alive && app.selection.has(agent.identifier))) {
      drawReticle(ctx, px, py, (radius + RING_WIDTH + 3) * reticlePulse());
    }
    if (exporting.has(agent.identifier)) {
      ctx.strokeStyle = css(EXPORTING);
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(px, py, radius + RING_WIDTH + 6, 0, Math.PI * 2);
      ctx.stroke();
    }
  }
  if (!drawn && hidden) sayTooFar(ctx, view);
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

const HOMES = ['hot', 'medium', 'cold', 'very cold'];

function select(identifier) {
  selectAll(new Set([identifier]));
}

// Tab: the next living Bitzkey by identifier after the one selected, Shift+Tab the one before, round to the first
function stepSelection(way) {
  const identifiers = (app.frame?.agents ?? []).filter(agent => agent.alive).map(agent => agent.identifier).sort((a, b) => a - b);
  if (!identifiers.length) return;
  const at = app.selectedId;
  let next = identifiers[0];
  if (at !== null && at !== undefined) {
    if (way > 0) next = identifiers.find(identifier => identifier > at) ?? identifiers[0];
    else next = [...identifiers].reverse().find(identifier => identifier < at) ?? identifiers[identifiers.length - 1];
  }
  select(next);
}

const TEXT_SIZES = [1, 1.25, 1.5, 2];
let textSize = 0;

map.addEventListener('mousedown', event => {
  if (!app.frame) return;
  const [x, y] = mousePoint(event);
  if (globe) {  // a right or middle drag turns the globe; a click picks the Bitzkey there
    if (event.button === 0) {
      const hit = [...globeHits].reverse().find(([hx, hy, reach]) => (x - hx) ** 2 + (y - hy) ** 2 <= (reach + 2) ** 2);
      if (hit) select(hit[3]);
      else selectAll(new Set());
    } else {
      turnFrom = { x, y, yaw: globeYaw, tilt: globeTilt };
    }
    event.preventDefault();
    return;
  }
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
  if (turnFrom) {  // dragged across, the globe turns about its axis; up or down, it tips
    const geometry = board(), radius = Math.min(geometry.view.w, geometry.view.h) * GLOBE_SHARE * globeZoom;
    globeYaw = turnFrom.yaw - (x - turnFrom.x) / radius;
    globeTilt = Math.max(-1.3, Math.min(1.3, turnFrom.tilt + (y - turnFrom.y) / radius));
    drawMap();
    return;
  }
  if (!squarePanel.contains(event.target)) {
    square = event.buttons === 0 && !deploying && !globe && event.target === map ? cellAt(x, y) : null;
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
  turnFrom = null;
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
  if (globe) {
    globeZoom = Math.max(0.5, Math.min(GLOBE_ZOOM_MAX, globeZoom * MAP_ZOOM_STEP ** (event.deltaY < 0 ? 1 : -1)));
    drawMap();
    return;
  }
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
// The globe: the map wrapped round a sphere, turned about its axis by dragging and tipped up or down, zoomed by the
// wheel, with each square's Bitzkeys stacked on it, the largest at the bottom, as in the window app.

const globeGround = document.createElement('canvas');
const globeFrame = document.createElement('canvas');

const TERRAIN_SHIFT = 25;  // the terrain's two bits in the social word, after the traits and neurodivergence
const filterRow = (labels, shown) => element('div', { class: 'filter-row' }, labels.map((label, kind) =>
  element('button', { class: shown[kind] ? 'on' : '', onclick: event => {
    shown[kind] = !shown[kind];
    event.currentTarget.classList.toggle('on', shown[kind]);
    drawMap();
  } }, label)));
const viewButtons = element('div', { class: 'view-buttons' },
  element('button', { class: 'view-map', onclick: () => { globe = blind = false; showViewButtons(); drawMap(); } }, 'MAP'),
  element('button', { class: 'view-globe', onclick: () => { globe = true; blind = false; showViewButtons(); drawMap(); } }, 'GLOBE'),
  element('button', { class: 'view-blind', onclick: () => { blind = true; showViewButtons(); drawMap(); } }, 'BLIND'),
  element('button', { class: 'view-lod', onclick: () => { lod = !lod; showViewButtons(); drawMap(); } }, 'LOD MANAGER'));
// Beneath them, the filters: a row by species, a row by terrain
const filterButtons = element('div', { class: 'filter-buttons' },
  filterRow(['PLANTS', 'GRAZERS', 'OMNIVORES', 'PREDATORS'], speciesShown),
  filterRow(['TERRESTRIAL', 'AQUATIC', 'AVIAN', 'AMPHIBIOUS'], terrainShown));
$('map-holder').append(viewButtons, filterButtons);
function showViewButtons() {
  viewButtons.querySelector('.view-map').classList.toggle('on', !globe && !blind);
  viewButtons.querySelector('.view-globe').classList.toggle('on', globe && !blind);
  viewButtons.querySelector('.view-blind').classList.toggle('on', blind);
  viewButtons.querySelector('.view-lod').classList.toggle('on', lod);
}
showViewButtons();

function drawGlobeGround(frame) {
  // The map flat, a few pixels a square: the rows' colours, the sea, their textures and grass, and the night.
  const per = Math.max(2, Math.min(12, Math.floor(GLOBE_TEXTURE / Math.max(frame.width, frame.height))));
  if (globeGround.width !== frame.width * per || globeGround.height !== frame.height * per) {
    globeGround.width = frame.width * per;
    globeGround.height = frame.height * per;
  }
  const ctx = globeGround.getContext('2d');
  for (let y = 0; y < frame.height; ++y) {
    const land = GROUNDS[Math.max(0, Math.min(3, Math.round(frame.temperatures[y])))];
    const zone = css(zoneColour(frame.temperatures[y])), sea = css(seaColour(frame.temperatures[y]));
    for (let x = 0; x < frame.width; ++x) {
      const square = frame.grass[y * frame.width + x];
      ctx.fillStyle = square & 2 ? sea : zone;
      ctx.fillRect(x * per, y * per, per, per);
      if (per >= TEXTURE_FROM) {
        const variant = ((Math.imul(x, 73856093) ^ Math.imul(y, 19349663)) >>> 0) % GROUND_VARIANTS;
        ctx.drawImage(groundTile(square & 2 ? 'sea' : land, per, variant), x * per, y * per, per, per);
        if (square & 1) ctx.drawImage(grassTuft(per, (square & 2) !== 0), x * per, y * per, per, per);
      } else if (square & 1) {
        ctx.fillStyle = css(square & 2 ? SEA_GRASS : GRASS);
        ctx.fillRect(x * per + per / 4, y * per + per / 4, per / 2, per / 2);
      }
    }
  }
  const day = app.state?.day_length;
  if (day) {
    const width = frame.width, columns = Math.floor(width / 2);
    const start = (((Math.trunc(-(frame.turn % day) * width / day)) % width) + width) % width;
    ctx.fillStyle = NIGHT;
    for (const [from, count] of [[start, Math.min(columns, width - start)], [0, Math.max(0, start + columns - width)]]) {
      if (count > 0) ctx.fillRect(from * per, 0, count * per, globeGround.height);
    }
  }
}

function drawGlobe(frame, ctx, view) {
  drawGlobeGround(frame);
  const scale = view.scale;
  const radius = Math.min(view.w, view.h) * GLOBE_SHARE * globeZoom;
  const size = 2 * Math.ceil(radius * scale) + 1;  // in device pixels
  const pixelRadius = radius * scale;
  if (!globeLook || globeLook.size !== size || globeLook.tilt !== globeTilt) {
    const longitude = new Float32Array(size * size), row = new Float32Array(size * size).fill(-1), light = new Float32Array(size * size);
    const c = Math.cos(globeTilt), sn = Math.sin(globeTilt), half = size / 2;
    for (let py = 0; py < size; ++py) {
      for (let px = 0; px < size; ++px) {
        const nx = (px + 0.5 - half) / pixelRadius, ny = -(py + 0.5 - half) / pixelRadius, rr = nx * nx + ny * ny;
        if (rr >= 1) continue;
        const nz = Math.sqrt(1 - rr), y1 = ny * c + nz * sn, z1 = -ny * sn + nz * c;  // tipped back upright
        const at = py * size + px;
        longitude[at] = Math.atan2(nx, z1);
        row[at] = 0.5 - Math.asin(Math.max(-1, Math.min(1, y1))) / Math.PI;
        light[at] = 0.35 + 0.65 * Math.max(0, -0.35 * nx + 0.45 * ny + 0.82 * nz);  // lit from the upper left, in front
      }
    }
    globeLook = { size, tilt: globeTilt, longitude, row, light };
  }
  const ground = globeGround.getContext('2d').getImageData(0, 0, globeGround.width, globeGround.height).data;
  const gw = globeGround.width, gh = globeGround.height;
  if (globeFrame.width !== size) globeFrame.width = globeFrame.height = size;
  const fctx = globeFrame.getContext('2d');
  const image = fctx.createImageData(size, size), out = image.data;
  const turn = globeYaw / (2 * Math.PI);
  for (let at = 0; at < size * size; ++at) {
    const row = globeLook.row[at];
    if (row < 0) continue;
    let u = globeLook.longitude[at] / (2 * Math.PI) + 0.5 + turn;
    u -= Math.floor(u);
    const from = (Math.min(gh - 1, Math.floor(row * gh)) * gw + Math.min(gw - 1, Math.floor(u * gw))) * 4, light = globeLook.light[at];
    out[at * 4] = ground[from] * light;
    out[at * 4 + 1] = ground[from + 1] * light;
    out[at * 4 + 2] = ground[from + 2] * light;
    out[at * 4 + 3] = 255;
  }
  fctx.putImageData(image, 0, 0);
  const cx = view.w / 2, cy = view.h / 2;
  ctx.drawImage(globeFrame, cx - size / scale / 2, cy - size / scale / 2, size / scale, size / scale);

  // Each square's Bitzkeys, stacked largest at the bottom, the stacks drawn from the back of the globe to the front.
  const squares = new Map();
  for (const agent of frame.agents) {
    if (agent.opacity <= 0) continue;
    const key = agent.y * frame.width + agent.x;
    if (!squares.has(key)) squares.set(key, []);
    squares.get(key).push(agent);
  }
  const c = Math.cos(globeTilt), sn = Math.sin(globeTilt), stacks = [];
  for (const [key, members] of squares) {
    const x = key % frame.width, y = Math.floor(key / frame.width);
    const latitude = (0.5 - (y + 0.5) / frame.height) * Math.PI;
    const longitude = ((x + 0.5) / frame.width - 0.5) * 2 * Math.PI - globeYaw;
    const x1 = Math.cos(latitude) * Math.sin(longitude), y1 = Math.sin(latitude), z1 = Math.cos(latitude) * Math.cos(longitude);
    const up = y1 * c - z1 * sn, depth = y1 * sn + z1 * c;
    if (depth < 0.08) continue;  // round the back, or too near the edge to see
    stacks.push({ depth, px: cx + x1 * radius, py: cy - up * radius, members });
  }
  stacks.sort((a, b) => a.depth - b.depth);
  globeHits = [];
  const [smallest, largest] = mapDotRadii(Math.PI * radius / frame.height);
  const least = smallestShown(globeZoom, 0.5, GLOBE_ZOOM_MAX);
  let drawn = 0, hidden = 0;
  for (const stack of stacks) {
    const members = stack.members.filter(agent => shownAt(agent, least)).sort((a, b) => b.size - a.size);
    drawn += members.length;
    hidden += stack.members.filter(agent => !shownAt(agent, least) && tooSmall(agent)).length;  // too small to see from this far out
    const shrink = 0.55 + 0.45 * stack.depth;  // foreshortened toward the edge
    let lift = 0;
    for (const agent of members) {
      const dot = Math.max(2, Math.round(dotRadius(agent.size, largest, smallest) * shrink));
      drawBitzkey(ctx, agent, stack.px, stack.py - lift, dot, RING_WIDTH, agent.alive, agent.opacity);
      if (agent.alive) globeHits.push([stack.px, stack.py - lift, dot + RING_WIDTH, agent.identifier]);  // drawn last, on top
      if (agent.identifier === app.selectedId) drawReticle(ctx, stack.px, stack.py - lift, dot + RING_WIDTH + 3);
      lift += Math.max(2, Math.round((dot + RING_WIDTH) * STACK_SPACING));  // the next above, its face clear
    }
  }
  if (!drawn && hidden) sayTooFar(ctx, view);
}

// H hides everything but the map or globe, which then fill the window; H again brings it all back.
function toggleHud() {
  document.body.classList.toggle('hud-hidden');
  drawMap();
}


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
  showTables(title, note, [{ header, rows }]);
}

// A report of several tables, each with its own heading (if any) and header row.
function showTables(title, note, tables) {
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
      element('div', { class: 'report-body scroll' }, tables.map(({ heading, header, rows }) => [
        heading ? element('h3', { class: 'report-heading' }, heading) : null,
        element('table', { class: 'report-table' },
          element('thead', {}, element('tr', {}, header.map(cell => element('th', {}, cell)))),
          element('tbody', {}, rows.map(row => element('tr', {}, row.map(cell =>
            cell instanceof Node ? cell : /%\s*$/.test(String(cell))  // a share shaded by its size: a heatmap
              ? element('td', { style: { background: heat(Math.max(0, Math.min(1, parseFloat(cell) / 100))) } }, cell)
              : element('td', {}, cell))))))])),
      element('div', { class: 'dialog-buttons' }, element('button', { class: 'primary', onclick: close }, 'CLOSE'))));
  document.body.append(shade);
  document.addEventListener('keydown', onKey, true);
}

async function openDeathReport() {
  try {
    const data = await api('/api/deaths');
    const header = first => [first, ...data.species.map(upper), 'ALL'];
    const rows = list => list.map(row => [row.label, ...row.counts.map(String), String(row.total)]);
    const causes = rows(data.causes);
    causes.push([element('td', { class: 'total' }, 'ALL'), ...[...data.all, data.total].map(count => element('td', { class: 'total' }, String(count)))]);
    const tables = [{ header: header('CAUSE'), rows: causes }];
    if (data.ages && data.ages.length) tables.push({ heading: 'Age at death, by species', header: header('AGE'), rows: rows(data.ages) });
    if (data.kills) {
      tables.push(
        { heading: `Who killed whom: ${data.kills} killed, by the species of the dead (rows) and of their killers (columns)`,
          header: header('THE DEAD'), rows: rows(data.whom) },
        { heading: "What the killers were doing, by the killers' species", header: header('ACTIVITY'), rows: rows(data.activities) },
        { heading: `The deadliest killers, ${data.deadliest.length} of ${data.killers}`, header: ['KILLER', 'SPECIES', 'KILLS', 'DOING'],
          rows: data.deadliest.map(killer => [`#${pad3(killer.identifier)}`, killer.species, String(killer.kills),
                                              killer.doing.map(([activity, count]) => `${activity} ${count}`).join(', ')]) });
    }
    showTables('Death report', `Every death in this world up to turn ${data.turn}: ${data.deaths} in all, by cause and species, ` +
               'who killed whom and what the killers were doing, and the deadliest killers.' +
               (data.kills ? '' : ' Nothing has killed anything yet.'), tables);
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
        return element('td', { class: action.name === best ? 'best' : '', style: { background: heat(chance) } }, `${Math.round(chance * 100)}%`);
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

// The selected Bitzkeys' sights swell to twice their size and back every RETICLE_PULSE_MS, paused or not: while any
// is selected, the map redraws a few times a second to show it.
const RETICLE_PULSE_MS = 2000;
const reticlePulse = () => 1.5 - 0.5 * Math.cos(2 * Math.PI * performance.now() / RETICLE_PULSE_MS);
setInterval(() => {
  if (app.frame && !document.hidden && (app.selectedId !== null || app.selection.size)) drawMap();
}, 66);

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
  if (key === 'F8') { handled(); settings.colourBlindSafe = !settings.colourBlindSafe; drawMap(); }  // colour-blind safe colours
  else if (key === 'F9') {  // larger text: 100, 125, 150, 200%
    handled();
    textSize = (textSize + 1) % TEXT_SIZES.length;
    for (const id of ['details', 'insights', 'sidebar']) {
      const panel = document.getElementById(id);
      if (panel) panel.style.zoom = TEXT_SIZES[textSize];
    }
  }
  else if (key === 'Tab') { handled(); stepSelection(event.shiftKey ? -1 : 1); }
  else if (key === ' ') { handled(); control('toggle'); }
  else if (key === 'ArrowRight') { handled(); control('step'); }
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
  else if (key === 'z' || key === 'Z') { mapZoom = 1; mapCentre = null; globeZoom = 1; drawMap(); }
  else if (key === 'h' || key === 'H') toggleHud();
  else if (key === 'Escape') setup();
});

$('play').onclick = () => control('toggle');
$('step').onclick = () => control('step');
$('faster').onclick = () => control('speed', { speed: Math.min(app.state.speed + 1, SPEED_COUNT - 1) });
$('slower').onclick = () => control('speed', { speed: Math.max(app.state.speed - 1, 0) });
$('show-insights').onclick = toggleInsights;
$('show-graph').onclick = graph;
$('save-report').onclick = saveReport;
$('show-files').onclick = showFiles;
$('show-workers').onclick = () => {
  if (app.overlay) return;
  app.overlay = { update() {} };
  openWorkers(() => { app.overlay = null; poll(true); });
};
$('new-world').onclick = setup;
$('export-all').onclick = openExportDialog;
$('open-catalog').onclick = () => { if (!app.overlay) browseCatalog(); };
$('advanced').onclick = () => { if (!app.overlay) editRules(); };
$('save-world').onclick = async () => {
  if (app.overlay || !app.state) return;
  try {
    const note = await saveWorld(app.state.turn);
    if (note) say(note);
  } catch (error) {
    say(error.message);
  }
};
$('message').textContent = HINT;

window.bitzkey = app;  // for looking into from the browser's console
loop();
