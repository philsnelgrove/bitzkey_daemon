// A Bitzkey's details, as the window app's sidebar shows them: its colour words, its mind and social
// traits (inborn and now) and body, its climate, and how it fares. A genome alone (`known` false) shows only what is born in it.

import { element, pad3, upper } from './common.js';
import { tr } from './i18n.js';
import { bitzkeyCanvas, iconCanvas, dotRadius, DOT_RADIUS_LARGEST, TEMPERATURES, TEMPERATURE_COLOURS, MIND_TRAITS, SOCIAL_TRAITS, BODY_TRAITS, css, shownColour } from './draw.js';

const SHORT = ['hot', 'medium', 'cold', 'v. cold'];

export function look(agent) {
  return { dot: agent.dot_rgb, ring: agent.ring_rgb, social: agent.social_rgb, species: agent.species_index };
}

function bar(value, colour) {
  return element('div', { class: 'bar' }, element('div', { style: { width: `${Math.max(0, Math.min(1, value)) * 100}%`, background: colour } }));
}

const fixed = (value, digits) => Number(value).toFixed(digits);

// The monospaced rows: each name is translated before it's padded, so the columns stay in line in
// every language. Chinese and Japanese characters take two columns, and are counted so.
function columns(text) {
  let width = 0;
  for (const character of text) width += /[ᄀ-ᅟ⺀-꓏가-힣豈-﫿︰-﹏＀-｠￠-￦]/.test(character) ? 2 : 1;
  return width;
}
const pad = (text, width) => text + ' '.repeat(Math.max(1, width - columns(text)));
// A bar under a trait: filled to `value` of the most, a tick at `born` if given, so a change shows at a glance
const traitBar = (value, born) => element('div', { class: 'trait-bar' },
  element('i', { style: { width: `${Math.max(0, Math.min(1, value / TRAIT_MAX)) * 100}%` } }),
  born === undefined ? null : element('b', { style: { left: `${Math.max(0, Math.min(1, born / TRAIT_MAX)) * 100}%` } }));
const TRAIT_MAX = 7;
// The popup the mouse brings up over a colour chit or a trait: one for the page, beside the mouse, kept on the screen.
let popup = null;
function hovering(target, build) {
  const place = event => {
    const box = popup.getBoundingClientRect();
    let x = event.clientX - 18 - box.width;  // to the left: the inspector is at the right
    if (x < 4) x = event.clientX + 18;
    popup.style.left = `${Math.max(4, Math.min(x, innerWidth - box.width - 4))}px`;
    popup.style.top = `${Math.max(4, Math.min(event.clientY + 12, innerHeight - box.height - 4))}px`;
  };
  target.addEventListener('mouseenter', event => {
    if (!popup) {
      popup = element('div', { class: 'hover-popup', 'data-no-translate': '' });
      document.body.append(popup);
    }
    popup.replaceChildren(...build());
    popup.hidden = false;
    place(event);
  });
  target.addEventListener('mousemove', place);
  target.addEventListener('mouseleave', () => { if (popup) popup.hidden = true; });
  return target;
}

// What each trait governs, as the desktop app's.
const TRAIT_ABOUT = {
  aggression: 'How much it enjoys fighting and attacking, softened by its empathy and tolerance.',
  libido: 'How much it enjoys mating, and so how eagerly it seeks a partner.',
  curiosity: 'How much it enjoys setting off to explore.',
  fear: 'How strongly it shies from hunters and danger, and how much a blow frightens it.',
  appetite: 'How much it enjoys eating.',
  patience: 'How far ahead it looks for the weather turning.',
  intellect: 'How fast it learns and how many past experiences it re-learns from; a cleverer brain burns more energy.',
  memory: 'How long it remembers where it last fed and where it was last struck.',
  resilience: "How quickly it gets over a herd-mate's failure, how long it bears bad weather, and how little a bad experience frightens it.",
  moodiness: 'How much experience reshapes its temperament over its life.',
  sociability: 'How much it enjoys the company of its own kind.',
  empathy: "How much it shares in others' joys and sorrows: caring for its young, grieving for them, and holding back from fights.",
  tolerance: 'How readily it accepts others unlike itself, and how much less it enjoys fighting and rivalry.',
  loyalty: 'How much it values keeping to its herd.',
  dominance: 'How sought-after it is as a mate, and how little crowding troubles it.',
  trust: "How much it learns from what its herd-mates do, and how far off a herd-mate's alert reaches it.",
  kinship: 'How closely a herd-mate or partner must share its social background for it to accept them.',
  size: 'A larger body is mightier and tougher but has fewer young at a time; a small one may slip away from an attacker.',
  strength: 'How hard it hits; it costs energy to keep.',
  armour: 'How well it withstands blows; it costs energy to keep.',
  weapons: 'How much harm it does in a fight (thorns, for a plant); it costs energy to keep.',
  speed: 'How fast it moves; the costliest trait to keep.',
  sight: 'How far it sees: three squares further for each step.',
  fertility: 'How cheaply and how often it can breed (how freely a plant spreads).',
  litter: 'How many young it tends to have at once.',
  carrion: 'How well it digests what it eats, carrion included.',
  neurodivergence: 'A gene that does nothing yet.',
  burn: 'The energy its body and brain use each turn.',
};

function traitPopup(name, value) {
  return [element('div', { class: 'popup-title' }, upper(tr(name))), element('div', { class: 'popup-value' }, value),
    element('div', { class: 'popup-about' }, tr(TRAIT_ABOUT[name] ?? ''))];
}

const TERRAIN_NAMES = ['terrestrial', 'aquatic', 'avian', 'amphibious'];
const SPECIES_NAMES = ['plant', 'grazer', 'omnivore', 'predator'];
const NEURODIVERGENCE_SHIFT = 3 * SOCIAL_TRAITS.length;

// A word's genes, from its highest bits to its lowest, as they read left to right in binary, as the desktop app's.
function genesOf(part, word) {
  const value = (low, bits) => Math.floor(word / 2 ** low) % 2 ** bits;
  const yesNo = bit => (bit ? 'yes' : 'no');
  const traits = (names, from = 0) => names.map((name, index) => [name, 3 * index, 3, '']).slice(from).reverse();
  if (part === 0) return [['cold tolerance', 31, 1, yesNo(value(31, 1))], ['heat tolerance', 30, 1, yesNo(value(30, 1))], ...traits(MIND_TRAITS)];
  if (part === 1) {
    return [['home temperature', 30, 2, TEMPERATURES[value(30, 2)]], ...traits(BODY_TRAITS, 1),
      ['nocturnal', 2, 1, yesNo(value(2, 1))], ['species', 0, 2, SPECIES_NAMES[value(0, 2)]]];
  }
  const terrain = NEURODIVERGENCE_SHIFT + 4;
  return [['(unused)', terrain + 2, 32 - terrain - 2, ''], ['terrain', terrain, 2, TERRAIN_NAMES[value(terrain, 2)]],
    ['neurodivergence', NEURODIVERGENCE_SHIFT, 4, ''], ...traits(SOCIAL_TRAITS)];
}

function wordPopup(part, hex, rgb) {
  const word = parseInt(hex, 16);
  const binary = (low, bits) => (Math.floor(word / 2 ** low) % 2 ** bits).toString(2).padStart(bits, '0');
  const genes = genesOf(part, word);
  const table = element('table', { class: 'genes' }, genes.map(([name, low, bits, meaning]) => element('tr', {},
    element('td', {}, tr(name)),
    element('td', {}, bits === 1 ? `${tr('bit')} ${low}` : `${tr('bits')} ${low}-${low + bits - 1}`),
    element('td', {}, binary(low, bits)), element('td', {}, String(Math.floor(word / 2 ** low) % 2 ** bits)),
    element('td', {}, meaning ? tr(meaning) : ''))));
  return [element('div', { class: 'popup-title' }, element('i', { class: 'swatch', style: { background: rgb ? css(shownColour(rgb)) : 'none' } }),
    tr(['MIND', 'BODY', 'SOCIAL'][part])),
  element('div', { class: 'mono' }, `${tr('hex')}  ${hex}`), element('div', { class: 'mono popup-label' }, tr('binary')),
  element('div', { class: 'mono' }, genes.map(([, low, bits]) => binary(low, bits)).join(' ')), table];
}

const row = (cls, text) => element('div', { class: cls, 'data-no-translate': '' }, text);

export function details(agent, { large = 0 } = {}) {
  const known = agent.known;
  // At twice the size it has on the map, beside its name, its colours and its terrain
  const radius = large || 2 * dotRadius(agent.body[1], DOT_RADIUS_LARGEST);
  const panel = element('div', { class: 'details' });
  const name = `#${pad3(agent.identifier)}  ${upper(tr(agent.species))}${agent.nocturnal === undefined ? '' : '  ' + upper(tr(agent.nocturnal ? 'nocturnal' : 'diurnal'))}`;
  const chits = element('div', { class: 'mono small swatches', 'data-no-translate': '' }, ...[['MIND', agent.dot, agent.dot_rgb], ['BODY', agent.ring, agent.ring_rgb],
    ['SOCIAL', agent.social, agent.social_rgb]].map(([label, hex, rgb], part) =>  // its three colours as chits: the mouse over one shows that word
    hovering(element('span', { class: 'chit' }, element('i', { class: 'swatch', style: { background: rgb ? css(shownColour(rgb)) : 'none' } }), tr(label)),
      () => wordPopup(part, hex, rgb))));
  if (agent.terrain) chits.append(element('span', { class: 'terrain' }, upper(tr(agent.terrain))));
  panel.append(element('div', { class: 'details-head' },
    bitzkeyCanvas(look(agent), radius, large ? Math.round(large / 4) : 4, agent.alive !== false || !known),
    element('div', { class: 'details-title' }, element('span', { class: 'details-name', 'data-no-translate': '' }, name), chits),
    iconCanvas(agent.species_index, 9, 'rgb(220,229,211)')));
  // Temperament: the mind's traits beside the social background's, inborn and now; the body beneath.
  const temperament = (heading, names, first) => {
    // As wide as the longest name in this language needs, and never narrower than the English.
    const width = Math.max(11, ...names.map(name => columns(tr(name)) + 1), columns(tr('neurodiv.')) + 1);
    const column = element('div', { class: 'traits' },
      row('label mono small', `${pad(tr(heading), width - 1)}${tr('born')}${known ? ' ' + tr('now') : ''}`));
    names.forEach((name, index) => {
      const born = agent.inborn[first + index];
      if (!known) {
        column.append(hovering(row('mono small', `${pad(tr(name), width)}${String(born).padStart(2)}`), () => traitPopup(name, `${tr('inborn')} ${born}`)));
        column.append(traitBar(born, born));
        return;
      }
      const now = agent.mind[first + index];
      const shift = Math.abs(now - born) < 0.25 ? 'same' : now > born ? 'up' : 'down';
      const change = shift === 'same' ? ' ' : shift === 'up' ? '+' : '-';  // not by colour alone
      column.append(hovering(row(`mono small ${shift}`, `${pad(tr(name), width)}${String(born).padStart(2)}${fixed(now, 1).padStart(5)}${change}`),
        () => traitPopup(name, `${tr('inborn')} ${born}   ${tr('now')} ${fixed(now, 1)}`)));
      column.append(traitBar(now, born));
    });
    column.width = width;
    return column;
  };
  const social = temperament('SOCIAL', SOCIAL_TRAITS, MIND_TRAITS.length);
  social.append(hovering(row('mono small same', `${pad(tr('neurodiv.'), social.width)}${String(agent.neurodivergence).padStart(2)}`),
    () => traitPopup('neurodivergence', `${tr('value')} ${agent.neurodivergence} / 15`)));
  panel.append(element('div', { class: 'trait-columns' }, temperament('MIND', MIND_TRAITS, 0), social));
  const body = element('div', { class: 'traits' }, element('div', { class: 'label' }, 'BODY'));
  const bodyWidth = Math.max(10, ...BODY_TRAITS.slice(1).map(name => columns(tr(name)) + 1));
  const cells = BODY_TRAITS.slice(1).map((name, index) => `${pad(tr(name), bodyWidth)}${String(agent.body[index + 1]).padStart(2)}`);
  cells.push(`${pad(tr('burn'), 7)}${fixed(agent.burn, 2).padStart(5)}`);
  for (let row = 0; row < cells.length; row += 3) {
    body.append(element('div', { class: 'mono small same body-row', 'data-no-translate': '' }, ...cells.slice(row, row + 3).map((cell, index) =>
      hovering(element('span', {}, cell, row + index < BODY_TRAITS.length - 1 ? traitBar(agent.body[row + index + 1]) : null),
        () => row + index < BODY_TRAITS.length - 1
          ? traitPopup(BODY_TRAITS[row + index + 1], `${tr('value')} ${agent.body[row + index + 1]} / ${TRAIT_MAX}`)
          : traitPopup('burn', `${tr('value')} ${fixed(agent.burn, 2)}`)))));
  }
  panel.append(body);

  const climate = element('div', { class: 'climate' });
  climate.append(element('span', { style: { color: css(TEMPERATURE_COLOURS[agent.home]) } }, `HOME ${upper(TEMPERATURES[agent.home])}`));
  if (known && agent.here !== undefined) {
    const steps = agent.climate_steps;
    const colour = steps < 0.5 ? 'rgb(175,192,172)' : steps < 1.5 ? 'rgb(238,183,110)' : 'rgb(235,100,90)';
    climate.append(element('span', { style: { color: colour } }, `HERE ${upper(SHORT[Math.floor(agent.here + 0.5)])}`));
  }
  const hardy = agent.heat ? 'HEAT-HARDY' : agent.cold ? 'COLD-HARDY' : '';
  if (hardy) climate.append(element('span', { class: 'hardy' }, hardy));
  panel.append(climate);

  if (!known) {
    panel.append(element('div', { class: 'note' }, `LIFESPAN ~${fixed(agent.lifespan, 0)}`));
    panel.append(element('div', { class: 'faint' }, agent.note || 'Not in the world at this turn: only its genome is known.'));
    return panel;
  }
  panel.append(element('div', { class: 'mono hunger' }, `HUNGER  ${fixed(Math.min(100, agent.hunger), 1).padStart(5)}%`));
  panel.append(bar(agent.hunger / 100, 'rgb(224,150,94)'));
  if (agent.stamina !== undefined) {
    panel.append(element('div', { class: 'mono stamina' }, `STAMINA ${fixed(agent.stamina, 1).padStart(5)}`));
    panel.append(bar(agent.stamina / 100, 'rgb(110,160,220)'));
  }
  panel.append(element('div', { class: 'mono health' }, `HEALTH  ${fixed(Math.max(0, agent.health), 1).padStart(5)} / ${fixed(agent.max_health, 0)}`));
  panel.append(bar(agent.health / agent.max_health, 'rgb(124,190,131)'));
  panel.append(element('div', { class: 'note' },
    `AGE ${agent.age} / ~${fixed(agent.lifespan, 0)}${agent.feeble ? ' FEEBLE' : ''}    GENERATION ${agent.generation}    CHILDREN ${agent.children}`));
  let state = `LAST ACTION: ${upper(agent.last_action)}`;
  if (agent.alive === false) state += `     MEAT LEFT ${fixed(agent.meat, 0)}`;
  panel.append(element('div', { class: 'note' }, state));
  if (agent.chances?.length) {
    panel.append(element('div', { class: 'note light' }, `CHANCES  ${agent.chances.map(([action, share]) => `${action} ${share}`).join('   ')}`));
  }
  return panel;
}
