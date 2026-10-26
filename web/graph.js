// The colour graph: every Bitzkey alive over time, stacked in its own colour, by species, as in the
// window app. The daemon draws the bars; this lays them out, zooms (into the run, and as on the map into
// the height around the pointer) and pans by dragging with the right or middle button, travels in time
// along the timeline, and picks Bitzkeys by click or box, sharing the pick with the map.

import { api, binary, element, upper, plural, pad3 } from './common.js';
import { drawIcon, SPECIES, pyRound } from './draw.js';
import { details } from './inspector.js';

const BAR_PIXELS = 5;
const DRAG_THRESHOLD = 4;
const MIN_VIEW = 10;
const ZOOM_STEP = 0.8;
const HEIGHT_ZOOM_MAX = 32;  // the closest the graph zooms into its height, times the whole
const YEAR_LABEL_SPACING = 56;
const FONT = '12px "Segoe UI", "Noto Sans", "DejaVu Sans", Arial, sans-serif';
const LEFT = 64, RIGHT = 4, TOP = 20, TIMELINE = 24, GAP = 4;

let sorts = null;

export function openGraph(app, onClose) {
  let part = 0, sort = 0;  // whose colours: 0 the ring's, 1 the dot's, 2 the social background's
  let view = null, pinned = false;  // the turns shown when zoomed in, and whether that keeps to the latest
  let heightZoom = 1, floor = 0;    // how far zoomed into the height, and the share of the full height at the bottom
  let panning = null;               // a drag with the right or middle button: where, which, the view and floor then, moved
  let image = null;                 // the bars as last drawn by the daemon
  let requestKey = '', loading = false, again = false;
  let press = null, mouse = null, hovered = null, hoverTimer = null, hoverAsked = '';
  let closed = false;

  const canvas = element('canvas');
  const ctx = canvas.getContext('2d');
  const sortMenu = element('select', { class: 'sort-select', title: 'What each species is sorted by' });
  const note = element('span', { class: 'graph-note' });
  const describe = element('span', { class: 'graph-describe' });
  const title = element('b');
  const mapButton = element('button', { class: 'primary', onclick: () => close(false) }, 'RETURN TO MAP');
  const tooltip = element('div', { class: 'tooltip', hidden: true });
  const panel = element('div', { class: 'screen graph' },
    element('div', { class: 'graph-top' }, title, sortMenu,
      element('button', { onclick: () => { app.selectAll(new Set()); redraw(); } }, 'CLEAR'),
      element('button', { onclick: () => app.exportIdentifiers([...app.selection], false) }, 'COPY'),
      element('button', { onclick: () => app.exportIdentifiers([...app.selection], true) }, 'SAVE')),
    element('div', { class: 'graph-sub' },
      element('span', {}, 'Every Bitzkey alive over time, in its own colour.    TAB  ring / dot / social    C  copy    S  save    K  catalog    G or ESC  close'),
      note),
    element('div', { class: 'graph-canvas' }, canvas),
    element('div', { class: 'graph-bottom' }, describe,
      element('button', { onclick: renew }, 'START A NEW WORLD WITH THE SELECTED'),
      element('button', { onclick: () => app.catalogIdentifiers([...app.selection]) }, 'ADD TO CATALOG'),
      mapButton),
    element('div', { class: 'graph-help' },
      element('span', {}, 'Select: click, or drag a box (SHIFT adds, CTRL removes)    Time: drag the timeline, or right-click    Zoom: wheel    Pan: right-drag    Z: all'),
      element('button', { onclick: () => app.browseCatalog() }, 'OPEN CATALOG')));
  const shade = element('div', { class: 'shade' }, panel);
  document.body.append(shade, tooltip);

  const state = () => app.state;
  const latest = () => state().turn;

  // ---- Layout and time

  function chartRect() {
    const width = canvas.clientWidth, height = canvas.clientHeight;
    return { x: LEFT, y: TOP, w: Math.max(10, width - LEFT - RIGHT), h: Math.max(10, height - TOP - TIMELINE - GAP) };
  }
  function timelineRect() {
    const chart = chartRect();
    return { x: chart.x, y: chart.y + chart.h + GAP, w: chart.w, h: TIMELINE };
  }
  function window_() {
    if (!view) return [0, latest()];
    if (pinned) return [Math.max(0, latest() - (view[1] - view[0])), latest()];
    return [Math.max(0, view[0]), Math.min(latest(), view[1])];
  }
  function turnAt(x) {
    const chart = chartRect(), [first, last] = window_();
    const turns = last - first + 1;
    return Math.max(first, Math.min(last, first + Math.floor((x - chart.x) * turns / Math.max(1, chart.w))));
  }
  function xOf(turn) {
    const chart = chartRect(), [first, last] = window_();
    return pyRound(chart.x + (turn - first + 0.5) * chart.w / (last - first + 1));
  }
  function zoom(notches, x) {
    const [first, last] = window_();
    const span = last - first + 1, whole = latest() + 1;
    const newSpan = Math.max(MIN_VIEW, pyRound(span * ZOOM_STEP ** notches));
    if (newSpan >= whole) {
      view = null;
      return;
    }
    if (!view || pinned) {  // reaching the latest turn, it stays pinned there, the living in view
      view = [whole - newSpan, whole - 1];
      pinned = true;
      return;
    }
    const centre = turnAt(x);
    let newFirst = pyRound(centre - (centre - first) * newSpan / span);
    newFirst = Math.max(0, Math.min(whole - newSpan, newFirst));
    view = [newFirst, newFirst + newSpan - 1];
    pinned = view[1] >= latest();
  }
  function zoomHeight(notches, y) {
    // As the map zooms: the population under the pointer stays put.
    const chart = chartRect();
    const up = (chart.y + chart.h - y) / Math.max(1, chart.h);
    const share = floor + up / heightZoom;
    heightZoom = Math.max(1, Math.min(HEIGHT_ZOOM_MAX, heightZoom * (1 / ZOOM_STEP) ** notches));
    setFloor(share - up / heightZoom);
  }
  function setFloor(value) {
    floor = Math.max(0, Math.min(1 - 1 / heightZoom, value));
  }
  function drag(started, to) {
    // The graph follows the pointer: in time while zoomed into it, in height while zoomed into that.
    const chart = chartRect();
    if (view) {
      const [first, last] = started.view;
      const span = last - first + 1;
      const shift = pyRound((to.x - started.x) * span / Math.max(1, chart.w));
      const newFirst = Math.max(0, Math.min(latest() + 1 - span, first - shift));
      view = [newFirst, newFirst + span - 1];
      pinned = view[1] >= latest();
    }
    setFloor(started.floor + (to.y - started.y) / Math.max(1, chart.h) / heightZoom);
  }
  function unzoom() {
    view = null;
    pinned = false;
    heightZoom = 1;
    floor = 0;
  }
  function pan(notches) {
    if (!view) return;
    const [first, last] = window_();
    const span = last - first + 1;
    const newFirst = Math.max(0, Math.min(latest() + 1 - span, first + notches * Math.max(1, Math.floor(span / 10))));
    view = [newFirst, newFirst + span - 1];
    pinned = view[1] >= latest();
  }

  function graphView() {
    const chart = chartRect(), [first, last] = window_();
    return { first, last, bars: Math.max(1, Math.floor(chart.w / BAR_PIXELS)), height: Math.round(chart.h), part, sort,
             zoom: heightZoom, floor };
  }

  // ---- Fetching and drawing

  async function load() {
    if (closed || !state()) return;
    const asked = graphView();
    const key = JSON.stringify([asked, state().version, [...app.selection]]);
    if (key === requestKey) return;
    if (loading) {
      again = true;
      return;
    }
    loading = true;
    requestKey = key;
    try {
      const data = await binary('/api/graph', { ...asked, selection: [...app.selection] });
      image = parseImage(data, asked);
      draw();
    } catch (error) {
      note.textContent = error.message;
    } finally {
      loading = false;
      if (again) {
        again = false;
        load();
      }
    }
  }

  function parseImage(data, asked) {
    const bars = data.getUint32(4, true), height = data.getUint32(8, true);
    const peak = data.getFloat32(12, true), first = data.getUint32(16, true), last = data.getUint32(20, true);
    let offset = 24;
    const turns = [];
    for (let bar = 0; bar < bars; ++bar, offset += 5) turns.push({ turn: data.getUint32(offset, true), future: data.getUint8(offset + 4) === 1 });
    const offscreen = document.createElement('canvas');
    offscreen.width = Math.max(1, bars);
    offscreen.height = Math.max(1, height);
    const pixels = offscreen.getContext('2d').createImageData(offscreen.width, offscreen.height);
    for (let bar = 0; bar < bars; ++bar) {
      for (let y = 0; y < height; ++y, offset += 3) {
        const at = (y * bars + bar) * 4;
        pixels.data[at] = data.getUint8(offset);
        pixels.data[at + 1] = data.getUint8(offset + 1);
        pixels.data[at + 2] = data.getUint8(offset + 2);
        pixels.data[at + 3] = 255;
      }
    }
    offscreen.getContext('2d').putImageData(pixels, 0, 0);
    const bands = [];
    const count = data.getUint32(offset, true);
    offset += 4;
    for (let band = 0; band < count; ++band, offset += 9) {
      bands.push({ species: data.getUint8(offset), top: data.getFloat32(offset + 1, true), bottom: data.getFloat32(offset + 5, true) });
    }
    return { bars, height, peak, first, last, turns, offscreen, bands, asked };
  }

  function draw() {
    if (closed) return;
    const scale = window.devicePixelRatio || 1;
    const width = canvas.clientWidth, height = canvas.clientHeight;
    if (canvas.width !== Math.round(width * scale) || canvas.height !== Math.round(height * scale)) {
      canvas.width = Math.round(width * scale);
      canvas.height = Math.round(height * scale);
    }
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    ctx.clearRect(0, 0, width, height);
    const chart = chartRect(), timeline = timelineRect();
    const s = state();
    title.textContent = `COLOUR GRAPH  /  ${['RING / BODY', 'DOT / MIND', 'SOCIAL BACKGROUND'][part]}`;
    mapButton.hidden = !s.ended;
    ctx.fillStyle = 'rgb(30,40,40)';
    ctx.fillRect(chart.x, chart.y, chart.w, chart.h);
    if (image) {
      ctx.imageSmoothingEnabled = false;
      for (let bar = 0; bar < image.bars; ++bar) {
        const left = chart.x + Math.floor(bar * chart.w / image.bars), right = chart.x + Math.floor((bar + 1) * chart.w / image.bars);
        const barWidth = Math.max(1, right - left - (right - left > 3 ? 1 : 0));
        ctx.drawImage(image.offscreen, bar, 0, 1, image.height, left, chart.y, barWidth, chart.h);
      }
      ctx.font = FONT;
      ctx.textBaseline = 'middle';
      ctx.textAlign = 'right';
      ctx.fillStyle = 'rgb(149,170,150)';
      const zoomed = image.asked.zoom, lowest = image.asked.floor;
      for (const fraction of [0, 0.5, 1]) {
        const value = image.peak * (lowest + fraction / zoomed);
        ctx.fillText(zoomed === 1 ? String(value) : String(Math.round(value)), chart.x - 8, chart.y + chart.h - fraction * chart.h);
      }
      labelSpecies(chart);
    }
    drawTimeline(chart, timeline);
    if (press && mouse) {
      const box = boxBetween(press.x, press.y, mouse.x, mouse.y, chart);
      ctx.fillStyle = 'rgba(245,238,206,0.16)';
      ctx.fillRect(box.x, box.y, box.w, box.h);
      ctx.strokeStyle = 'rgb(245,238,206)';
      ctx.strokeRect(box.x + 0.5, box.y + 0.5, box.w, box.h);
    }
    describe.textContent = describeText();
  }

  function labelSpecies(chart) {
    // Name each species' band, with its icon, inside the right end of the chart, where it is tall enough.
    const scaleY = chart.h / image.height;
    ctx.font = FONT;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'left';
    for (const band of image.bands) {
      const top = Math.max(chart.y, chart.y + band.top * scaleY);  // the part in view
      const bottom = Math.min(chart.y + chart.h, chart.y + band.bottom * scaleY);
      if (bottom - top < 15) continue;
      const label = `${upper(SPECIES[band.species])}S`;
      const textWidth = ctx.measureText(label).width;
      const boxWidth = textWidth + 15 + 14, middle = (top + bottom) / 2;
      const left = chart.x + chart.w - 6 - boxWidth;
      ctx.fillStyle = 'rgba(19,26,27,0.75)';
      ctx.fillRect(left, middle - 9, boxWidth, 18);
      drawIcon(ctx, band.species, left + 4 + 7, middle, 6.3, 'rgb(238,238,220)');
      ctx.fillStyle = 'rgb(238,238,220)';
      ctx.fillText(label, left + 8 + 15, middle + 1);
    }
  }

  function drawTimeline(chart, timeline) {
    const [first, last] = window_();
    ctx.fillStyle = 'rgb(22,30,30)';
    ctx.fillRect(timeline.x, timeline.y, timeline.w, timeline.h);
    // Each winter quarter shaded, and each year's start marked up through the chart.
    const length = state().year_length || 360;
    const span = chart.w / (last - first + 1);
    const leftOf = turn => pyRound(chart.x + (turn - first) * span);
    const ceiling = (top, bottom) => Math.floor((top + bottom - 1) / bottom);
    ctx.fillStyle = 'rgb(30,42,56)';
    for (let quarter = Math.floor(first * 4 / length); quarter * length / 4 <= last; ++quarter) {
      if (quarter % 4 !== 1 && quarter % 4 !== 3) continue;
      const start = Math.max(first, ceiling(quarter * length, 4)), end = Math.min(last + 1, ceiling((quarter + 1) * length, 4));
      if (end > start) ctx.fillRect(leftOf(start), timeline.y + 1, leftOf(end) - leftOf(start), timeline.h - 2);
    }
    const yearPixels = Math.max(1, pyRound(length * span));
    const every = Math.max(1, ceiling(YEAR_LABEL_SPACING, yearPixels));
    ctx.font = FONT;
    ctx.textBaseline = 'top';
    ctx.textAlign = 'left';
    for (let turn = ceiling(Math.max(1, first), length) * length; turn <= last; turn += length) {
      const x = leftOf(turn) + 0.5;
      ctx.strokeStyle = 'rgb(88,104,112)';
      ctx.beginPath(); ctx.moveTo(x, chart.y); ctx.lineTo(x, timeline.y + timeline.h - 1); ctx.stroke();
      if ((turn / length) % every === 0) {
        ctx.fillStyle = 'rgb(140,185,230)';
        ctx.fillText(`YEAR ${turn / length + 1}`, x + 3, chart.y + 2);
      }
    }
    ctx.strokeStyle = 'rgb(54,68,62)';
    ctx.strokeRect(timeline.x + 0.5, timeline.y + 0.5, timeline.w - 1, timeline.h - 1);
    let middle = !view ? `TURN ${Math.floor((first + last) / 2)}`
                       : `TURNS ${first} - ${last}  (ZOOM x${((latest() + 1) / (last - first + 1)).toFixed(1)})`;
    if (heightZoom > 1) middle += `  (HEIGHT x${heightZoom.toFixed(1)})`;
    ctx.fillStyle = 'rgb(149,170,150)';
    ctx.textBaseline = 'middle';
    const y = timeline.y + timeline.h / 2;
    ctx.textAlign = 'left'; ctx.fillText(`TURN ${first}`, timeline.x + 8, y);
    ctx.textAlign = 'center'; ctx.fillText(middle, timeline.x + timeline.w / 2, y);
    ctx.textAlign = 'right'; ctx.fillText(`TURN ${last}`, timeline.x + timeline.w - 8, y);
    for (const [turn, colour] of [[state().turn, 'rgb(245,238,206)']]) {
      if (turn === null || turn < first || turn > last) continue;
      const x = xOf(turn);
      ctx.strokeStyle = colour;
      ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(x, chart.y); ctx.lineTo(x, timeline.y + timeline.h - 1); ctx.stroke();
      ctx.lineWidth = 1;
      ctx.fillStyle = colour;
      ctx.fillRect(x - 5, timeline.y, 10, timeline.h);
      ctx.textAlign = 'center';
      ctx.textBaseline = 'bottom';
      const label = `TURN ${turn}`, half = ctx.measureText(label).width / 2;
      ctx.fillText(label, Math.max(chart.x + half, Math.min(chart.x + chart.w - half, x)), chart.y - 2);
    }
  }

  function describeText() {
    const s = state();
    let chosen = app.selection.size ? `${app.selection.size} selected` : 'Nothing selected';
    if (sort !== 0 && sorts) chosen = `The living sorted by ${sorts[sort].toLowerCase()}, lowest at the bottom.     ${chosen}`;
    else if (s.ended) chosen = `${s.end_note}     ${chosen}`;
    if (!hovered) return chosen;
    if (!hovered.alive) {
      let died = hovered.cause ? `died: ${hovered.cause}` : 'dead';
      if (hovered.killer !== undefined) died += ` by #${pad3(hovered.killer)} ${hovered.killer_species}, ${hovered.activity}`;
      return `${chosen}     |     #${pad3(hovered.identifier)}  ${hovered.species}  ${died}   ring ${hovered.ring}   dot ${hovered.dot}   social ${hovered.social}`;
    }
    return `${chosen}     |     #${pad3(hovered.identifier)}  ${hovered.species}  alive   ring ${hovered.ring}   dot ${hovered.dot}   social ${hovered.social}`;
  }

  function redraw() {
    requestKey = '';
    load();
    draw();
  }

  // ---- The pointer

  function point(event) {
    const rect = canvas.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  }
  const inside = (p, r) => p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
  function boxBetween(x1, y1, x2, y2, chart) {
    const left = Math.max(chart.x, Math.min(x1, x2)), right = Math.min(chart.x + chart.w, Math.max(x1, x2));
    const top = Math.max(chart.y, Math.min(y1, y2)), bottom = Math.min(chart.y + chart.h, Math.max(y1, y2));
    return { x: left, y: top, w: Math.max(0, right - left), h: Math.max(0, bottom - top) };
  }
  function barAt(x) {
    const chart = chartRect();
    if (!image) return -1;
    return Math.max(0, Math.min(image.bars - 1, Math.floor((x - chart.x) * image.bars / chart.w)));
  }
  const pixelY = y => {
    const chart = chartRect();
    return image ? (y - chart.y) * image.height / chart.h : 0;
  };

  function hover() {
    clearTimeout(hoverTimer);
    hoverTimer = setTimeout(async () => {
      if (!mouse || !image || press || !inside(mouse, chartRect())) {
        hovered = null;
        tooltip.hidden = true;
        draw();
        return;
      }
      const asked = { ...image.asked, bar: barAt(mouse.x), y: pixelY(mouse.y) };
      const key = JSON.stringify([asked, state().version]);
      if (key === hoverAsked) return;
      hoverAsked = key;
      try {
        const found = await api('/api/graph/hover', asked);
        hovered = found.identifier === null ? null : found;
        if (hovered?.details && mouse) {
          tooltip.replaceChildren(details(hovered.details));
          tooltip.hidden = false;
          placeTooltip();
        } else {
          tooltip.hidden = true;
        }
        draw();
      } catch { /* the next move asks again */ }
    }, 60);
  }

  function placeTooltip() {
    if (!lastClient) return;
    const box = tooltip.getBoundingClientRect();
    let x = lastClient.x + 18, y = lastClient.y + 12;
    if (x + box.width > window.innerWidth) x = lastClient.x - 18 - box.width;
    y = Math.max(0, Math.min(window.innerHeight - box.height, y));
    tooltip.style.left = `${Math.max(0, x)}px`;
    tooltip.style.top = `${y}px`;
  }

  let lastClient = null;
  canvas.addEventListener('mousemove', event => {
    lastClient = { x: event.clientX, y: event.clientY };
    mouse = point(event);
    if (panning && (panning.moved || Math.max(Math.abs(mouse.x - panning.x), Math.abs(mouse.y - panning.y)) >= DRAG_THRESHOLD)) {
      panning.moved = true;
      drag(panning, mouse);
      redraw();
    }
    if (!tooltip.hidden) placeTooltip();
    hover();
    if (press) draw();
  });
  canvas.addEventListener('mouseleave', () => {
    mouse = null;
    hovered = null;
    tooltip.hidden = true;
    draw();
  });
  canvas.addEventListener('contextmenu', event => event.preventDefault());
  canvas.addEventListener('mousedown', event => {
    const p = point(event);
    const chart = chartRect(), timeline = timelineRect();
    if ((event.button === 1 || event.button === 2) && (inside(p, chart) || inside(p, timeline))) {
      event.preventDefault();  // no autoscroll for the middle button
      panning = { x: p.x, y: p.y, button: event.button, view: window_(), floor, moved: false };
    } else if (event.button === 0 && inside(p, chart)) {
      press = { x: p.x, y: p.y, shift: event.shiftKey, ctrl: event.ctrlKey || event.metaKey };
      tooltip.hidden = true;
    }
  });
  const onUp = async event => {
    if ((event.button === 1 || event.button === 2) && panning) {
      panning = null;
      return;
    }
    if (event.button !== 0) return;
    if (!press || !image) return;
    const p = point(event), started = press;
    press = null;
    const chart = chartRect();
    let picked;
    if (Math.max(Math.abs(p.x - started.x), Math.abs(p.y - started.y)) < DRAG_THRESHOLD) {
      const found = await api('/api/graph/hover', { ...image.asked, bar: barAt(started.x), y: pixelY(started.y) });
      picked = found.identifier !== null && found.alive ? [found.identifier] : [];  // only the living can be picked
      apply(picked, started.ctrl ? 'remove' : started.shift ? 'toggle' : 'replace');
    } else {
      const box = boxBetween(started.x, started.y, p.x, p.y, chart);
      const found = await api('/api/graph/pick', {
        ...image.asked, bar_from: barAt(box.x), bar_to: barAt(box.x + box.w - 1), top: pixelY(box.y), bottom: pixelY(box.y + box.h) });
      apply(found.identifiers, started.ctrl ? 'remove' : started.shift ? 'add' : 'replace');
    }
  };
  window.addEventListener('mouseup', onUp);
  canvas.addEventListener('wheel', event => {
    event.preventDefault();
    const p = point(event);
    if (!inside(p, chartRect()) && !inside(p, timelineRect())) return;
    const notches = event.deltaY < 0 ? 1 : -1;
    if (event.deltaX) pan(event.deltaX > 0 ? 1 : -1);
    else if (event.shiftKey) pan(-notches);
    else {
      zoom(notches, p.x);
      if (inside(p, chartRect())) zoomHeight(notches, p.y);
    }
    redraw();
  }, { passive: false });

  function apply(picked, mode) {
    let result;
    if (mode === 'add') result = new Set([...app.selection, ...picked]);
    else if (mode === 'remove') result = new Set([...app.selection].filter(identifier => !picked.includes(identifier)));
    else if (mode === 'toggle') {
      result = new Set(app.selection);
      for (const identifier of picked) if (!result.delete(identifier)) result.add(identifier);
    } else result = new Set(picked);
    app.selectAll(result);
    redraw();
  }

  async function renew() {
    if (await app.renew([...app.selection])) close(false);
  }

  // ---- Keys, the sort menu, closing

  function onKey(event) {
    if (document.querySelector('.dialog-shade')) return;
    const shades = document.querySelectorAll('.shade');
    if (shades[shades.length - 1] !== shade) return;  // the catalog is open over it
    if (event.target instanceof HTMLSelectElement) return;
    const key = event.key.toLowerCase();
    if (key === 'tab') { event.preventDefault(); part = (part + 1) % 3; redraw(); }
    else if (key === 'c') app.exportIdentifiers([...app.selection], false);
    else if (key === 's') app.exportIdentifiers([...app.selection], true);
    else if (key === 'k') app.catalogIdentifiers([...app.selection]);
    else if (key === 'z') { unzoom(); redraw(); }
    else if (key === 'g' || key === 'escape') { event.preventDefault(); close(state().ended); }
    else if (key === ' ') { event.preventDefault(); }
  }
  document.addEventListener('keydown', onKey);

  (async () => {
    if (!sorts) sorts = (await api('/api/graph/sorts')).sorts;
    sortMenu.replaceChildren(...sorts.map((label, index) => element('option', { value: index }, `SORT  ${upper(label)}`)));
    sortMenu.value = sort;
  })();
  sortMenu.addEventListener('change', () => {
    sort = Number(sortMenu.value);
    sortMenu.blur();
    redraw();
  });

  const resize = new ResizeObserver(() => redraw());
  resize.observe(canvas);

  // Closing after the run has ended goes on to set up a new world, as the window app does.
  function close(newWorld) {
    if (closed) return;
    closed = true;
    document.removeEventListener('keydown', onKey);
    window.removeEventListener('mouseup', onUp);
    resize.disconnect();
    shade.remove();
    tooltip.remove();
    onClose(newWorld);
  }

  redraw();
  return {
    update() {
      load();
      if (!press) draw();
    },
    close,
  };
}
