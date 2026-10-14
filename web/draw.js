// How a Bitzkey is drawn, as in the window app: an inner dot in its mind word's colour, sized by its
// body, inside a ring in its body word's colour. A dot big enough to hold its species' icon is black,
// with the icon in the dot's colour inside a thin light grey outline.

export const RING_WIDTH = 2;
export const DOT_RADIUS_SMALLEST = 1;
export const DOT_RADIUS_LARGEST = 5;
export const ICON_MIN_RADIUS = 3;
export const TRAIT_MAX = 7;
const CROSS = [225, 55, 55];
const OUTLINE = [190, 190, 190];
const WORLD_GREY = [196, 198, 196];
const ZONE_TINTS = [[150, 85, 55], [110, 170, 90], [255, 255, 255], [170, 210, 250]];
const ZONE_TINT = 0.45;
export const GRASS = [62, 122, 52];
export const GRID = [140, 146, 142];
export const EXPORTING = [205, 120, 30];
export const SPECIES = ['plant', 'grazer', 'omnivore', 'predator'];
export const TEMPERATURES = ['hot', 'medium', 'cold', 'very cold'];
export const TEMPERATURE_COLOURS = [[238, 160, 110], [175, 192, 172], [140, 185, 230], [200, 225, 250]];
export const MIND_TRAITS = ['aggression', 'libido', 'curiosity', 'fear', 'appetite', 'patience', 'intellect', 'memory', 'resilience', 'moodiness'];
export const SOCIAL_TRAITS = ['sociability', 'empathy', 'tolerance', 'loyalty', 'dominance', 'trust', 'kinship'];
export const BODY_TRAITS = ['species', 'size', 'strength', 'armour', 'weapons', 'speed', 'sight', 'fertility', 'litter', 'carrion'];

export const css = ([r, g, b], alpha = 1) => alpha >= 1 ? `rgb(${r},${g},${b})` : `rgba(${r},${g},${b},${alpha})`;

// Python's round: halves to even.
export function pyRound(value) {
  const floor = Math.floor(value), diff = value - floor;
  if (diff > 0.5) return floor + 1;
  if (diff < 0.5) return floor;
  return floor % 2 === 0 ? floor : floor + 1;
}

// The world's grey tinted for a temperature, part way between two tints while the weather turns.
export function zoneColour(temperature) {
  const top = ZONE_TINTS.length - 1;
  const lower = Math.min(Math.floor(temperature), top), upper = Math.min(lower + 1, top);
  const turned = temperature - lower;
  return [0, 1, 2].map(c => {
    const tint = ZONE_TINTS[lower][c] + (ZONE_TINTS[upper][c] - ZONE_TINTS[lower][c]) * turned;
    return pyRound(WORLD_GREY[c] + (tint - WORLD_GREY[c]) * ZONE_TINT);
  });
}

export function dotRadius(size, largest = DOT_RADIUS_LARGEST, smallest = DOT_RADIUS_SMALLEST) {
  return pyRound(smallest + (largest - smallest) * size / TRAIT_MAX);
}

// The smallest and largest dots on the map, zoomed with it: the largest body's dot, ring and all,
// fills a cell, and the smallest stays big enough for its icon.
export function mapDotRadii(cell) {
  const largest = Math.max(DOT_RADIUS_LARGEST, pyRound(cell / 2) - RING_WIDTH);
  const smallest = Math.max(ICON_MIN_RADIUS, pyRound(largest * ICON_MIN_RADIUS / DOT_RADIUS_LARGEST));
  return [smallest, largest];
}

function circle(ctx, x, y, radius, colour) {
  ctx.fillStyle = colour;
  ctx.beginPath();
  ctx.arc(x, y, Math.max(0.5, radius), 0, Math.PI * 2);
  ctx.fill();
}

// A species' icon, centred, `half` its half-width: a tree, a leaf, an apple, a drumstick.
export function iconPath(ctx, species, cx, cy, half) {
  const at = (x, y) => [cx + x * half, cy + y * half];
  const polygon = points => {
    ctx.moveTo(...at(...points[0]));
    for (const point of points.slice(1)) ctx.lineTo(...at(...point));
    ctx.closePath();
  };
  const disc = (x, y, r) => {
    const [px, py] = at(x, y);
    ctx.moveTo(px + r * half, py);
    ctx.arc(px, py, Math.max(0.5, r * half), 0, Math.PI * 2);
  };
  const bar = (a, b, width) => {  // a thick line, as a polygon
    const [x1, y1] = at(...a), [x2, y2] = at(...b);
    const length = Math.hypot(x2 - x1, y2 - y1) || 1, w = Math.max(1, width * half) / 2;
    const nx = -(y2 - y1) / length * w, ny = (x2 - x1) / length * w;
    ctx.moveTo(x1 + nx, y1 + ny); ctx.lineTo(x2 + nx, y2 + ny); ctx.lineTo(x2 - nx, y2 - ny); ctx.lineTo(x1 - nx, y1 - ny);
    ctx.closePath();
  };
  ctx.beginPath();
  switch (species) {
    case 0:  // a tree: a canopy over a trunk
      polygon([[0, -1], [0.85, 0.35], [-0.85, 0.35]]);
      ctx.rect(...at(-0.18, 0.3), Math.max(1, 0.36 * half), Math.max(1, 0.7 * half));
      break;
    case 1:  // a leaf on a short stem
      polygon([[-0.65, 0.65], [-0.62, -0.05], [-0.2, -0.58], [0.8, -0.8], [0.58, 0.2], [0.05, 0.62]]);
      bar([-0.6, 0.6], [-0.95, 0.95], 0.18);
      break;
    case 2:  // an apple: two lobes, a stem and a leaf
      disc(-0.28, 0.22, 0.58);
      disc(0.28, 0.22, 0.58);
      bar([0, -0.3], [0.12, -0.9], 0.16);
      polygon([[0.12, -0.7], [0.62, -0.95], [0.4, -0.52]]);
      break;
    default:  // a drumstick: the meat, a bone and its knuckle
      disc(0.25, -0.25, 0.6);
      bar([-0.05, 0.05], [-0.62, 0.62], 0.26);
      disc(-0.8, 0.6, 0.2);
      disc(-0.6, 0.8, 0.2);
  }
}

export function drawIcon(ctx, species, cx, cy, half, colour) {
  iconPath(ctx, species, cx, cy, half);
  ctx.fillStyle = colour;
  ctx.fill('nonzero');
}

// One Bitzkey: `look` holds dot (rgb), ring (rgb), social (rgb, the background behind its icon) and
// species. A dead one is dimmed and crossed in red; with `opacity` below 1, all of it is that faint,
// as a corpse fades.
export function drawBitzkey(ctx, look, x, y, radius, ringWidth = RING_WIDTH, alive = true, opacity = 1) {
  if (opacity <= 0) return;
  ctx.save();
  ctx.globalAlpha *= opacity;
  let dot = look.dot, ring = look.ring, outline = OUTLINE, social = look.social || [0, 0, 0];
  if (!alive) [dot, ring, outline, social] = [dot, ring, outline, social].map(c => c.map(v => Math.floor(v / 3)));
  circle(ctx, x, y, radius + ringWidth, css(ring));
  if (radius >= ICON_MIN_RADIUS) {
    circle(ctx, x, y, radius, css(social));
    iconPath(ctx, look.species, x, y, radius * 0.8);
    ctx.lineJoin = 'round';
    ctx.lineWidth = 2 * Math.max(1, ringWidth / RING_WIDTH);
    ctx.strokeStyle = css(outline);
    ctx.stroke();
    ctx.fillStyle = css(dot);
    ctx.fill('nonzero');
  } else {
    circle(ctx, x, y, radius, css(dot));
  }
  if (!alive) {
    const reach = radius + ringWidth;
    ctx.strokeStyle = css(CROSS);
    ctx.lineWidth = Math.max(2, Math.floor(reach / 3));
    ctx.beginPath();
    ctx.moveTo(x - reach, y - reach); ctx.lineTo(x + reach, y + reach);
    ctx.moveTo(x - reach, y + reach); ctx.lineTo(x + reach, y - reach);
    ctx.stroke();
  }
  ctx.restore();
}

// The ring round a selected Bitzkey, like a target's sights: white inside black, with four pips.
export function drawReticle(ctx, x, y, radius) {
  ctx.save();
  ctx.lineWidth = 2;
  ctx.strokeStyle = '#000';
  ctx.beginPath(); ctx.arc(x, y, radius + 2, 0, Math.PI * 2); ctx.stroke();
  ctx.strokeStyle = '#fff';
  ctx.beginPath(); ctx.arc(x, y, radius, 0, Math.PI * 2); ctx.stroke();
  const reach = radius + 2, length = Math.max(3, Math.floor(radius / 2));
  for (const [width, colour] of [[4, '#000'], [2, '#fff']]) {
    ctx.lineWidth = width;
    ctx.strokeStyle = colour;
    ctx.beginPath();
    for (const [dx, dy] of [[0, -1], [1, 0], [0, 1], [-1, 0]]) {
      ctx.moveTo(x + dx * reach, y + dy * reach);
      ctx.lineTo(x + dx * (reach + length), y + dy * (reach + length));
    }
    ctx.stroke();
  }
  ctx.restore();
}

// A small canvas holding one Bitzkey, for cards and panels; sized for the device's pixels.
export function bitzkeyCanvas(look, radius, ringWidth = RING_WIDTH, alive = true) {
  const canvas = document.createElement('canvas');
  const side = 2 * (radius + ringWidth + 2);
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.ceil(side * scale);
  canvas.height = Math.ceil(side * scale);
  canvas.style.width = `${side}px`;
  canvas.style.height = `${side}px`;
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  drawBitzkey(ctx, look, side / 2, side / 2, radius, ringWidth, alive);
  return canvas;
}

export function iconCanvas(species, half, colour) {
  const canvas = document.createElement('canvas');
  const side = Math.ceil(2 * half + 4), scale = window.devicePixelRatio || 1;
  canvas.width = Math.ceil(side * scale);
  canvas.height = Math.ceil(side * scale);
  canvas.style.width = `${side}px`;
  canvas.style.height = `${side}px`;
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  drawIcon(ctx, species, side / 2, side / 2, half, colour);
  return canvas;
}
