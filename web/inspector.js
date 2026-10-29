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
const row = (cls, text) => element('div', { class: cls, 'data-no-translate': '' }, text);

export function details(agent, { large = 0 } = {}) {
  const known = agent.known;
  const radius = large || dotRadius(agent.body[1], DOT_RADIUS_LARGEST);
  const panel = element('div', { class: 'details' });
  panel.append(element('div', { class: 'details-head' },
    bitzkeyCanvas(look(agent), radius, large ? Math.round(large / 4) : 2, agent.alive !== false || !known),
    element('span', { class: 'details-name' }, `#${pad3(agent.identifier)}  ${upper(agent.species)}${agent.terrain ? '  ' + upper(agent.terrain) : ''}${agent.nocturnal === undefined ? '' : agent.nocturnal ? '  NOCTURNAL' : '  DIURNAL'}`),
    iconCanvas(agent.species_index, 9, 'rgb(220,229,211)')));
  panel.append(element('div', { class: 'mono small swatches' }, ...[['DOT', agent.dot, agent.dot_rgb], ['RING', agent.ring, agent.ring_rgb],
    ['SOCIAL', agent.social, agent.social_rgb]].map(([name, hex, rgb]) =>  // its three colours as swatches, the hex beside each
    element('span', {}, element('i', { class: 'swatch', style: { background: rgb ? css(shownColour(rgb)) : 'none' } }), `${name} ${hex} `))));
  // Temperament: the mind's traits beside the social background's, inborn and now; the body beneath.
  const temperament = (heading, names, first) => {
    // As wide as the longest name in this language needs, and never narrower than the English.
    const width = Math.max(11, ...names.map(name => columns(tr(name)) + 1), columns(tr('neurodiv.')) + 1);
    const column = element('div', { class: 'traits' },
      row('label mono small', `${pad(tr(heading), width - 1)}${tr('born')}${known ? ' ' + tr('now') : ''}`));
    names.forEach((name, index) => {
      const born = agent.inborn[first + index];
      if (!known) {
        column.append(row('mono small', `${pad(tr(name), width)}${String(born).padStart(2)}`));
        column.append(traitBar(born, born));
        return;
      }
      const now = agent.mind[first + index];
      const shift = Math.abs(now - born) < 0.25 ? 'same' : now > born ? 'up' : 'down';
      const change = shift === 'same' ? ' ' : shift === 'up' ? '+' : '-';  // not by colour alone
      column.append(row(`mono small ${shift}`, `${pad(tr(name), width)}${String(born).padStart(2)}${fixed(now, 1).padStart(5)}${change}`));
      column.append(traitBar(now, born));
    });
    column.width = width;
    return column;
  };
  const social = temperament('SOCIAL', SOCIAL_TRAITS, MIND_TRAITS.length);
  social.append(row('mono small same', `${pad(tr('neurodiv.'), social.width)}${String(agent.neurodivergence).padStart(2)}`));
  panel.append(element('div', { class: 'trait-columns' }, temperament('MIND', MIND_TRAITS, 0), social));
  const body = element('div', { class: 'traits' }, element('div', { class: 'label' }, 'BODY'));
  const bodyWidth = Math.max(10, ...BODY_TRAITS.slice(1).map(name => columns(tr(name)) + 1));
  const cells = BODY_TRAITS.slice(1).map((name, index) => `${pad(tr(name), bodyWidth)}${String(agent.body[index + 1]).padStart(2)}`);
  cells.push(`${pad(tr('burn'), 7)}${fixed(agent.burn, 2).padStart(5)}`);
  for (let row = 0; row < cells.length; row += 3) {
    body.append(element('div', { class: 'mono small same body-row', 'data-no-translate': '' }, ...cells.slice(row, row + 3).map((cell, index) =>
      element('span', {}, cell, row + index < BODY_TRAITS.length - 1 ? traitBar(agent.body[row + index + 1]) : null))));
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
