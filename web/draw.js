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
// The sea: its row's temperature tint drawn this far toward deep blue, so the climate's bands show through, and its
// grass, sea grass, in kelp green.
const SEA = [38, 84, 140];
const SEA_TINT = 0.6;
export const SEA_GRASS = [58, 150, 120];
export const NIGHT = 'rgba(10, 16, 40, 0.35)';  // the shadow over the night's half of the map
export const GRID = [140, 146, 142];
export const EXPORTING = [205, 120, 30];
export const SPECIES = ['plant', 'grazer', 'omnivore', 'predator'];
export const TEMPERATURES = ['hot', 'medium', 'cold', 'very cold'];
export const TEMPERATURE_COLOURS = [[238, 160, 110], [175, 192, 172], [140, 185, 230], [200, 225, 250]];
export const MIND_TRAITS = ['aggression', 'libido', 'curiosity', 'fear', 'appetite', 'patience', 'intellect', 'memory', 'resilience', 'moodiness'];
export const SOCIAL_TRAITS = ['sociability', 'empathy', 'tolerance', 'loyalty', 'dominance', 'trust', 'kinship'];
export const BODY_TRAITS = ['species', 'size', 'strength', 'armour', 'weapons', 'speed', 'sight', 'fertility', 'litter', 'carrion'];

// Colour-blind safe colours (F8): genome colours with the red-green differences a deuteranope or protanope loses moved
// into lightness and blue-yellow, where they can be seen (a "daltonize" correction), as the desktop app's.
export const settings = { colourBlindSafe: false };

export function shownColour([r, g, b]) {
  if (!settings.colourBlindSafe) return [r, g, b];
  const seenR = 0.625 * r + 0.375 * g, seenG = 0.7 * r + 0.3 * g, seenB = 0.3 * g + 0.7 * b;  // as a deuteranope sees it
  const lostR = r - seenR, lostG = g - seenG, lostB = b - seenB;
  const clamp = value => Math.round(Math.min(255, Math.max(0, value)));
  return [clamp(r), clamp(g + 0.7 * lostR + lostG), clamp(b + 0.7 * lostR + lostB)];
}

// A share, 0 to 1, as a heatmap cell's shade
export const heat = share => css([30, Math.round(40 + 48 * share), Math.round(40 + 78 * share)]);

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

// The same under the sea.
export function seaColour(temperature) {
  const zone = zoneColour(temperature);
  return zone.map((value, c) => pyRound(value + (SEA[c] - value) * SEA_TINT));
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
// A Bitzkey is drawn as a poker chip, as in the window app: its coloured edge CHIP_EDGE of its radius (at least the
// ring's width) with CHIP_INSERTS lighter inserts round it, its face the social background's (or the dot's, too small
// for an icon) carrying the species icon, lit from the top left with a shine and a soft shadow. Each look is drawn
// once, onto a canvas of its own, and kept.
const CHIP_EDGE = 0.32;
const CHIP_INSERTS = 6;
const chips = new Map();  // each look drawn, from the least to the most recently drawn
const LOOKS_KEPT = 20000, LOOK_SHADES = 8;

const scaled = (colour, factor) => colour.map(value => Math.max(0, Math.min(255, Math.round(value * factor))));
const mixed = (from, to, share) => from.map((value, c) => Math.round(value + (to[c] - value) * share));

function drawChip(ctx, look, x, y, radius, ringWidth, alive) {
  const outer = radius + ringWidth;
  const edge = Math.max(ringWidth, Math.round(outer * CHIP_EDGE)), face = outer - edge, drop = Math.max(1, outer * 0.14);
  let dot = look.dot, ring = look.ring, outline = OUTLINE, social = look.social || [0, 0, 0];
  if (!alive) [dot, ring, outline, social] = [dot, ring, outline, social].map(c => scaled(c, 1 / 3));
  const icon = radius >= ICON_MIN_RADIUS;
  // The shadow, softened at its edge.
  const shadow = ctx.createRadialGradient(x + drop, y + drop, outer * 0.6, x + drop, y + drop, outer + 1 + outer * 0.35);
  shadow.addColorStop(0, 'rgba(0,0,0,0.38)');
  shadow.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = shadow;
  ctx.beginPath();
  ctx.arc(x + drop, y + drop, outer + 1 + outer * 0.35, 0, Math.PI * 2);
  ctx.fill();
  // The edge, its inserts, the groove and the face.
  circle(ctx, x, y, outer, css(ring));
  if (outer >= 5) {
    ctx.fillStyle = css(mixed(ring, [255, 255, 255], 0.55));
    for (let insert = 0; insert < CHIP_INSERTS; ++insert) {
      const from = -Math.PI + insert * 2 * Math.PI / CHIP_INSERTS, to = from + 0.4 * 2 * Math.PI / CHIP_INSERTS;
      ctx.beginPath();
      ctx.arc(x, y, outer, from, to);
      ctx.arc(x, y, face, to, from, true);
      ctx.closePath();
      ctx.fill();
    }
  }
  circle(ctx, x, y, face, css(icon ? social : dot));
  if (outer >= 5) {
    ctx.strokeStyle = 'rgba(0,0,0,0.3)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(x, y, face, 0, Math.PI * 2);
    ctx.stroke();
  }
  if (icon) {
    iconPath(ctx, look.species, x, y, face * 0.82);
    ctx.lineJoin = 'round';
    ctx.lineWidth = 2 * Math.max(1, face / 6);
    ctx.strokeStyle = css(outline);
    ctx.stroke();
    ctx.fillStyle = css(dot);
    ctx.fill('nonzero');
  }
  // Light from the top left, and the shine, on the chip alone.
  ctx.save();
  ctx.beginPath();
  ctx.arc(x, y, outer, 0, Math.PI * 2);
  ctx.clip();
  const light = ctx.createLinearGradient(x - outer, y - outer, x + outer, y + outer);
  light.addColorStop(0, 'rgba(255,255,255,0.14)');
  light.addColorStop(0.5, 'rgba(255,255,255,0)');
  light.addColorStop(1, 'rgba(0,0,0,0.22)');
  ctx.fillStyle = light;
  ctx.fillRect(x - outer, y - outer, 2 * outer, 2 * outer);
  const shineX = x - outer * 0.35, shineY = y - outer * 0.42, shine = ctx.createRadialGradient(shineX, shineY, 0, shineX, shineY, outer * 0.55);
  shine.addColorStop(0, `rgba(255,255,255,${alive ? 0.42 : 0.15})`);
  shine.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = shine;
  ctx.fillRect(x - outer, y - outer, 2 * outer, 2 * outer);
  ctx.restore();
  if (!alive) {
    ctx.strokeStyle = css(CROSS);
    ctx.lineWidth = Math.max(2, Math.floor(outer / 3));
    ctx.beginPath();
    ctx.moveTo(x - outer, y - outer); ctx.lineTo(x + outer, y + outer);
    ctx.moveTo(x - outer, y + outer); ctx.lineTo(x + outer, y - outer);
    ctx.stroke();
  }
}

export function drawBitzkey(ctx, look, x, y, radius, ringWidth = RING_WIDTH, alive = true, opacity = 1) {
  if (opacity <= 0) return;
  look = { ...look, dot: shownColour(look.dot), ring: shownColour(look.ring), social: shownColour(look.social) };
  const scale = window.devicePixelRatio || 1;
  // Each look drawn is kept, up to LOOKS_KEPT, the least recently drawn let go first: its colours to within
  // LOOK_SHADES of each channel, so the nearly alike, as most Bitzkeys are, share one.
  const shades = [look.dot, look.ring, look.social].map((colour) => colour.map((channel) => Math.floor(channel / LOOK_SHADES)).join(','));
  const key = `${shades.join('|')}|${look.species}|${radius}|${ringWidth}|${alive}|${scale}`;
  let chip = chips.get(key);
  if (chip) chips.delete(key);  // put back below, as the most recently drawn
  else {
    if (chips.size >= LOOKS_KEPT) chips.delete(chips.keys().next().value);
    const outer = radius + ringWidth, reach = Math.ceil(outer + Math.max(1, outer * 0.14) + outer * 0.35 + 3);
    chip = document.createElement('canvas');
    chip.width = chip.height = Math.ceil(2 * reach * scale);
    chip.reach = reach;
    const own = chip.getContext('2d');
    own.scale(scale, scale);
    drawChip(own, look, reach, reach, radius, ringWidth, alive);
  }
  chips.set(key, chip);
  ctx.save();
  ctx.globalAlpha *= opacity;
  ctx.drawImage(chip, x - chip.reach, y - chip.reach, 2 * chip.reach, 2 * chip.reach);
  ctx.restore();
}

// Grass as a tuft, or kelp in the sea, and the ground's faint texture, each drawn once for a square's size.
export const TEXTURE_FROM = 6;  // squares this many pixels across or more are textured, and their grass drawn as tufts
export const GROUND_VARIANTS = 4;
const tufts = new Map(), tiles = new Map();

function sprite(size, draw) {
  const scale = window.devicePixelRatio || 1;
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = Math.max(1, Math.ceil(size * scale));
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  ctx.lineCap = 'round';
  draw(ctx);
  return canvas;
}

function strokeLine(ctx, points, colour, width = 1) {
  ctx.strokeStyle = colour;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.moveTo(...points[0]);
  for (const point of points.slice(1)) ctx.lineTo(...point);
  ctx.stroke();
}

export function grassTuft(size, sea) {
  const key = `${size}|${sea}|${window.devicePixelRatio || 1}`;
  if (!tufts.has(key)) {
    tufts.set(key, sprite(size, ctx => {
      const baseX = size * 0.5, baseY = size * 0.8, tall = size * 0.55, thick = Math.max(1, size / 12);
      if (sea) {  // kelp: a few swaying strands, darker ones behind a light one
        for (let strand = 0; strand < 3; ++strand) {
          const x0 = baseX + (strand - 1) * size * 0.16, points = [[x0, baseY]];
          for (let step = 1; step <= 8; ++step) {
            const t = step / 8;
            points.push([x0 + Math.sin(t * 5.5 + strand) * size * 0.08, baseY - t * tall * (strand === 1 ? 1 : 0.8)]);
          }
          strokeLine(ctx, points, css(strand === 1 ? SEA_GRASS : scaled(SEA_GRASS, 0.7)), thick);
        }
      } else {  // blades fanning from a clump, dark beneath and light at their tips
        const lean = [-0.55, -0.25, 0, 0.3, 0.6], share = [0.7, 0.95, 1, 0.85, 0.65];
        lean.forEach((tilt, blade) => {
          const tip = [baseX + tilt * size * 0.42, baseY - tall * share[blade]];
          const mid = [baseX + tilt * size * 0.12, baseY - tall * share[blade] * 0.55];
          strokeLine(ctx, [[baseX + (blade - 2) * thick * 0.6, baseY], mid], css(scaled(GRASS, 0.75)), thick * 1.3);
          strokeLine(ctx, [mid, tip], css(mixed(GRASS, [170, 220, 120], 0.45)), thick);
        });
      }
    }));
  }
  return tufts.get(key);
}

// The ground's texture: 'sand', 'meadow', 'snow' or 'ice' by a zone's usual temperature (hot to very cold), or 'sea'.
export const GROUNDS = ['sand', 'meadow', 'snow', 'ice'];

export function groundTile(ground, size, variant) {
  const key = `${ground}|${size}|${variant}|${window.devicePixelRatio || 1}`;
  if (!tiles.has(key)) {
    let state = (Math.imul(2654435761, variant * 7 + GROUNDS.concat('sea').indexOf(ground) * 131 + 1) >>> 0) || 1;
    const next = () => {  // the same texture every time
      state ^= state << 13; state >>>= 0;
      state ^= state >>> 17;
      state ^= state << 5; state >>>= 0;
      return (state & 0xFFFFFF) / 0x1000000;
    };
    const speck = (ctx, colour) => ctx.fillRect(Math.floor(next() * size), Math.floor(next() * size), 1, 1, ctx.fillStyle = colour);
    tiles.set(key, sprite(size, ctx => {
      const s = size;
      if (ground === 'sand') {  // ripples drifted by the wind, and grit
        for (let ripple = 0; ripple < 2; ++ripple) {
          const y0 = s * (0.3 + 0.4 * ripple + 0.1 * next()), x0 = s * 0.1 * next(), points = [[x0, y0]];
          for (let step = 1; step <= 6; ++step) points.push([x0 + step * s * 0.13, y0 + Math.sin(step * 1.1 + ripple) * s * 0.05]);
          strokeLine(ctx, points, 'rgba(255,240,205,0.3)');
        }
        for (let grit = 0; grit < 4; ++grit) { ctx.fillStyle = 'rgba(110,75,40,0.3)'; ctx.fillRect(Math.floor(next() * s), Math.floor(next() * s), 1, 1); }
      } else if (ground === 'meadow') {  // short darker strokes, and pale flecks
        for (let mark = 0; mark < 4; ++mark) {
          const x0 = next() * s, y0 = next() * s;
          strokeLine(ctx, [[x0, y0], [x0 + (next() - 0.5) * s * 0.12, y0 - s * 0.12]], 'rgba(45,85,35,0.3)');
        }
        for (let fleck = 0; fleck < 3; ++fleck) { ctx.fillStyle = 'rgba(215,235,160,0.3)'; ctx.fillRect(Math.floor(next() * s), Math.floor(next() * s), 1, 1); }
      } else if (ground === 'snow') {  // a drift's blue shadow, and sparkle
        const x0 = s * (0.1 + 0.3 * next()), y0 = s * (0.4 + 0.4 * next());
        strokeLine(ctx, [[x0, y0], [x0 + s * 0.5, y0 - s * 0.08]], 'rgba(150,175,210,0.35)', Math.max(1, s / 10));
        for (let flake = 0; flake < 6; ++flake) {
          ctx.fillStyle = flake % 2 ? 'rgba(255,255,255,0.6)' : 'rgba(160,190,225,0.6)';
          ctx.fillRect(Math.floor(next() * s), Math.floor(next() * s), 1, 1);
        }
      } else if (ground === 'ice') {  // cracks across the ice, and a glint
        const points = [[next() * s, next() * s]];
        for (let step = 0; step < 3; ++step) {
          const [px, py] = points[points.length - 1];
          points.push([px + (next() - 0.4) * s * 0.4, py + (next() - 0.4) * s * 0.4]);
        }
        strokeLine(ctx, points, 'rgba(245,252,255,0.4)');
        const gx = Math.floor(next() * s), gy = Math.floor(next() * s);
        ctx.fillStyle = 'rgba(255,255,255,0.35)';
        ctx.fillRect(gx - 1, gy, 3, 1);
        ctx.fillRect(gx, gy - 1, 1, 3);
        ctx.fillStyle = 'rgba(255,255,255,0.8)';
        ctx.fillRect(gx, gy, 1, 1);
      } else {  // wave crests, light over the deep
        const crests = 1 + Math.floor(next() * 2);
        for (let crest = 0; crest < crests; ++crest) {
          const cx = s * (0.2 + 0.6 * next()), cy = s * (0.25 + 0.5 * next()), span = s * 0.22, points = [];
          for (let step = 0; step <= 6; ++step) points.push([cx - span + 2 * span * step / 6, cy - Math.sin(step / 6 * Math.PI) * s * 0.08]);
          strokeLine(ctx, points, 'rgba(225,238,255,0.33)');
        }
      }
    }));
  }
  return tiles.get(key);
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
