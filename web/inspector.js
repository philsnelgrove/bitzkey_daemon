// A Bitzkey's details, as the window app's sidebar shows them: its colour words, its mind (inborn and
// now) and body, its climate, and how it fares. A genome alone (`known` false) shows only what is born in it.

import { element, pad3, upper } from './common.js';
import { bitzkeyCanvas, iconCanvas, dotRadius, DOT_RADIUS_LARGEST, TEMPERATURES, TEMPERATURE_COLOURS, MIND_TRAITS, BODY_TRAITS, css } from './draw.js';

const SHORT = ['hot', 'medium', 'cold', 'v. cold'];

export function look(agent) {
  return { dot: agent.dot_rgb, ring: agent.ring_rgb, species: agent.species_index };
}

function bar(value, colour) {
  return element('div', { class: 'bar' }, element('div', { style: { width: `${Math.max(0, Math.min(1, value)) * 100}%`, background: colour } }));
}

const fixed = (value, digits) => Number(value).toFixed(digits);

export function details(agent, { large = 0 } = {}) {
  const known = agent.known;
  const radius = large || dotRadius(agent.body[1], DOT_RADIUS_LARGEST);
  const panel = element('div', { class: 'details' });
  panel.append(element('div', { class: 'details-head' },
    bitzkeyCanvas(look(agent), radius, large ? Math.round(large / 4) : 2, agent.alive !== false || !known),
    element('span', { class: 'details-name' }, `#${pad3(agent.identifier)}  ${upper(agent.species)}`),
    iconCanvas(agent.species_index, 9, 'rgb(220,229,211)')));
  panel.append(element('div', { class: 'mono' }, `DOT / MIND   ${agent.dot}`));
  panel.append(element('div', { class: 'mono' }, `RING / BODY  ${agent.ring}`));
  const mind = element('div', { class: 'traits' }, element('div', { class: 'label' }, known ? 'MIND      born   now' : 'MIND      born'));
  for (let trait = 0; trait < MIND_TRAITS.length; ++trait) {
    const born = agent.inborn[trait];
    if (!known) {
      mind.append(element('div', { class: 'mono small' }, `${MIND_TRAITS[trait].padEnd(11)}${String(born).padStart(3)}`));
      continue;
    }
    const now = agent.mind[trait];
    const shift = Math.abs(now - born) < 0.25 ? 'same' : now > born ? 'up' : 'down';
    mind.append(element('div', { class: `mono small ${shift}` }, `${MIND_TRAITS[trait].padEnd(11)}${String(born).padStart(3)}${fixed(now, 1).padStart(6)}`));
  }
  const body = element('div', { class: 'traits' }, element('div', { class: 'label' }, 'BODY'));
  for (let trait = 1; trait < BODY_TRAITS.length; ++trait) {
    body.append(element('div', { class: 'mono small same' }, `${BODY_TRAITS[trait].padEnd(9)}${String(agent.body[trait]).padStart(3)}`));
  }
  body.append(element('div', { class: 'mono small same' }, `${'burn'.padEnd(7)}${fixed(agent.burn, 2).padStart(5)}`));
  panel.append(element('div', { class: 'trait-columns' }, mind, body));

  const climate = element('div', { class: 'climate' });
  climate.append(element('span', { style: { color: css(TEMPERATURE_COLOURS[agent.home]) } }, `HOME ${upper(TEMPERATURES[agent.home])}`));
  if (known && agent.here !== undefined) {
    const steps = agent.climate_steps;
    const colour = steps < 0.5 ? 'rgb(175,192,172)' : steps < 1.5 ? 'rgb(238,183,110)' : 'rgb(235,100,90)';
    climate.append(element('span', { style: { color: colour } }, `HERE ${upper(SHORT[Math.floor(agent.here + 0.5)])}`));
  }
  const hardy = agent.heat && agent.cold ? 'HEAT & COLD-HARDY' : agent.heat ? 'HEAT-HARDY' : agent.cold ? 'COLD-HARDY' : '';
  if (hardy) climate.append(element('span', { class: 'hardy' }, hardy));
  panel.append(climate);

  if (!known) {
    panel.append(element('div', { class: 'note' }, `LIFESPAN ~${fixed(agent.lifespan, 0)}`));
    panel.append(element('div', { class: 'faint' }, 'Not in the world at this turn: only its genome is known.'));
    return panel;
  }
  panel.append(element('div', { class: 'mono hunger' }, `HUNGER  ${fixed(Math.min(100, agent.hunger), 1).padStart(5)}%`));
  panel.append(bar(agent.hunger / 100, 'rgb(224,150,94)'));
  panel.append(element('div', { class: 'mono health' }, `HEALTH  ${fixed(Math.max(0, agent.health), 1).padStart(5)} / ${fixed(agent.max_health, 0)}`));
  panel.append(bar(agent.health / agent.max_health, 'rgb(124,190,131)'));
  panel.append(element('div', { class: 'note' },
    `AGE ${agent.age} / ~${fixed(agent.lifespan, 0)}    GENERATION ${agent.generation}    CHILDREN ${agent.children}`));
  let state = `LAST ACTION: ${upper(agent.last_action)}`;
  if (agent.alive === false) state += `     MEAT LEFT ${fixed(agent.meat, 0)}`;
  panel.append(element('div', { class: 'note' }, state));
  if (agent.chances?.length) {
    panel.append(element('div', { class: 'note light' }, `CHANCES  ${agent.chances.map(([action, share]) => `${action} ${share}`).join('   ')}`));
  }
  return panel;
}
