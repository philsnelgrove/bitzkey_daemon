// Setting up a new world, as the window app's configuration panel does: the launch settings, the
// Bitzkeys to import (pasted, loaded, from the catalog or designed), and the advanced rules. Starting
// it replaces the daemon's world for everyone viewing it.

import { api, element, dialog, remembered, remember, upper, plural } from './common.js';
import { bitzkeyCanvas, dotRadius, SPECIES, TEMPERATURES, MIND_TRAITS, SOCIAL_TRAITS, BODY_TRAITS } from './draw.js';
import { details, look } from './inspector.js';

const MAX_IMPORT_BYTES = 5_000_000;

// A screen laid over the page, with its own keys while it is on top.
function screen(className, build) {
  const panel = element('div', { class: `screen ${className}` });
  const shade = element('div', { class: 'shade' }, panel);
  document.body.append(shade);
  build(panel);
  return shade;
}

const onTop = shade => {
  if (document.querySelector('.dialog-shade')) return false;
  const shades = document.querySelectorAll('.shade');
  return shades[shades.length - 1] === shade;
};

// A row of the form: its label and hint, and a number with - and + either side.
function numberRow({ label, hint, hintClass = '', locked = false, onStep, input }) {
  return element('div', { class: `field-row${locked ? ' locked' : ''}` },
    element('div', { class: 'field-text' },
      element('div', { class: 'field-label' }, upper(label)),
      element('div', { class: `field-hint ${hintClass}`, title: hint }, hint)),
    element('button', { disabled: locked, tabindex: -1, onclick: event => onStep(event.shiftKey ? -10 : -1) }, '-'),
    input,
    element('button', { disabled: locked, tabindex: -1, onclick: event => onStep(event.shiftKey ? 10 : 1) }, '+'));
}

export function openSetup(app, onClose) {
  let closed = false;
  let settings = [], rules = {}, ruleList = [];
  const inputs = new Map();
  const note = element('div', { class: 'screen-note' });
  const importBox = element('textarea', { spellcheck: false,
    placeholder: 'Paste exported genomes here, load a file, pick some FROM CATALOG, or DESIGN your own. Imported Bitzkeys join the ones spawned above.' });
  const importStatus = element('div', { class: 'import-status' }, 'Nothing to import.');
  const advancedButton = element('button', { onclick: openAdvanced }, 'ADVANCED');
  const advancedNote = element('small');
  const fileInput = element('input', { type: 'file', accept: '.json,.txt,application/json,text/plain', hidden: true });
  const rows = element('div');
  let noteTimer = null;

  const tell = (text, warn = true) => {
    note.textContent = text;
    note.classList.toggle('warn', warn);
    clearTimeout(noteTimer);
    noteTimer = setTimeout(() => {
      note.textContent = 'UP/DOWN  choose   +/- buttons adjust (SHIFT x10)   ENTER  start   ESC  cancel';
      note.classList.remove('warn');
    }, 5000);
  };

  const shade = screen('setup', panel => {
    panel.append(
      element('div', { class: 'screen-title' }, element('b', {}, 'BITZKEY'), element('span', {}, 'NEW WORLD  /  CONFIGURATION')),
      element('div', { class: 'advanced-button' }, advancedButton, advancedNote),
      element('div', { class: 'lead' }, 'Set up the world, then start it. It replaces the world running now, for everyone viewing it.'),
      element('div', { class: 'scroll' }, rows,
        element('div', { class: 'import-head' }, element('span', {}, 'IMPORT BITZKEYS'),
          element('button', { onclick: openSetupCatalog }, 'FROM CATALOG'),
          element('button', { onclick: openDesigner }, 'DESIGN'),
          element('button', { onclick: () => fileInput.click() }, 'LOAD FILE'),
          element('button', { onclick: () => { importBox.value = ''; checkImport(); } }, 'CLEAR')),
        importBox, importStatus, fileInput),
      element('div', { class: 'screen-foot' },
        element('button', { onclick: () => close() }, 'CANCEL'),
        element('button', { class: 'primary', onclick: start }, 'START')),
      note);
  });
  tell('', false);

  // ---- The settings

  function settle(setting) {
    const input = inputs.get(setting.key);
    const text = input.value.trim();
    if (text === '' && setting.optional) return null;
    const value = Math.max(setting.minimum, Math.min(setting.maximum, parseInt(text, 10) || setting.minimum));
    input.value = String(value);
    return value;
  }

  function buildRows() {
    rows.replaceChildren(...settings.map(setting => {
      const input = element('input', { inputmode: 'numeric', placeholder: setting.blank || '' });
      input.value = setting.value === null ? '' : String(setting.value);
      input.addEventListener('input', () => { input.value = input.value.replace(/[^0-9]/g, '').slice(0, 9); });
      input.addEventListener('blur', () => settle(setting));
      inputs.set(setting.key, input);
      return numberRow({
        label: setting.label, hint: setting.hint, input,
        onStep: steps => {
          const current = input.value.trim() === '' ? null : settle(setting);
          const value = current === null ? setting.minimum : current + steps * setting.step;
          input.value = String(Math.max(setting.minimum, Math.min(setting.maximum, value)));
        },
      });
    }));
  }

  function showRulesCount() {
    const count = Object.keys(rules).length;
    advancedButton.classList.toggle('changed', count > 0);
    advancedNote.textContent = count ? `${count} rule${count !== 1 ? 's' : ''} changed` : '';
  }

  // ---- Importing

  let checkTimer = null, checked = '';
  function checkImport() {
    clearTimeout(checkTimer);
    checkTimer = setTimeout(async () => {
      const text = importBox.value;
      if (text === checked) return;
      checked = text;
      remember('import', text.length <= MAX_IMPORT_BYTES ? text : '');
      const result = await api('/api/import/check', { text });
      importStatus.textContent = result.summary;
      importStatus.className = `import-status ${!result.ok ? 'bad' : result.count ? 'ok' : ''}`;
    }, 250);
  }
  importBox.addEventListener('input', checkImport);

  function addImport(text) {
    const current = importBox.value;
    importBox.value = current + (current && !current.endsWith('\n') ? '\n' : '') + text;
    checkImport();
  }

  fileInput.addEventListener('change', async () => {
    const file = fileInput.files[0];
    fileInput.value = '';
    if (!file) return;
    if (file.size > MAX_IMPORT_BYTES) {
      tell(`${file.name} is too large to import.`);
      return;
    }
    importBox.value = (await file.text()).replace(/\r/g, '');
    checkImport();
    tell(`Loaded ${file.name}.`, false);
  });
  shade.addEventListener('dragover', event => event.preventDefault());
  shade.addEventListener('drop', async event => {
    event.preventDefault();
    const file = event.dataTransfer.files[0];
    if (!file) return;
    if (file.size > MAX_IMPORT_BYTES) {
      tell(`${file.name} is too large to import.`);
      return;
    }
    importBox.value = (await file.text()).replace(/\r/g, '');
    checkImport();
    tell(`Loaded ${file.name}.`, false);
  });

  // ---- Starting

  async function start() {
    const form = {};
    for (const setting of settings) form[setting.key] = settle(setting);
    form.import = importBox.value;
    try {
      const result = await api('/api/new_world', { settings: form, rules });
      remember('import', importBox.value);
      app.say(result.note);
      close();
    } catch (error) {
      tell(error.message);
    }
  }

  function onKey(event) {
    if (!onTop(shade)) return;
    if (event.key === 'Escape') {
      event.preventDefault();
      close();
    } else if (event.key === 'Enter' && !(event.target instanceof HTMLTextAreaElement) && !(event.target instanceof HTMLButtonElement)) {
      event.preventDefault();
      start();
    } else if ((event.key === 'ArrowUp' || event.key === 'ArrowDown') && event.target instanceof HTMLInputElement) {
      event.preventDefault();
      const all = [...inputs.values()];
      const next = all[(all.indexOf(event.target) + (event.key === 'ArrowDown' ? 1 : all.length - 1)) % all.length];
      next.focus();
      next.select();
    }
  }
  document.addEventListener('keydown', onKey);

  function close() {
    if (closed) return;
    closed = true;
    document.removeEventListener('keydown', onKey);
    shade.remove();
    onClose();
  }

  (async () => {
    try {
      const setup = await api('/api/setup');
      settings = setup.settings;
      ruleList = setup.rules;
      rules = { ...setup.changed };
      buildRows();
      showRulesCount();
      importBox.value = remembered('import', '');
      checkImport();
      inputs.get('width')?.focus();
    } catch (error) {
      tell(error.message);
    }
  })();

  // ---- The advanced rules

  function openAdvanced() {
    const values = Object.fromEntries(ruleList.map(rule => [rule.key, rule.key in rules ? rules[rule.key] : rule.default]));
    const ruleInputs = new Map();
    let focused = ruleList[0]?.key;
    const lead = element('div', { class: 'lead' });
    const advancedNote = element('div', { class: 'screen-note' });
    const list = element('div', { class: 'scroll', style: { flex: '1' } });
    const shown = (rule, value) => {  // up to six significant digits, no trailing zeros
      const text = rule.integer ? String(Math.round(value)) : String(Number(value.toPrecision(6)));
      return text;
    };
    const refresh = () => {
      const changed = ruleList.filter(rule => values[rule.key] !== rule.default).length;
      lead.textContent = `The rules the world plays by. ${changed} changed from the defaults; changed values show in amber.`;
      for (const rule of ruleList) ruleInputs.get(rule.key).classList.toggle('changed', values[rule.key] !== rule.default);
      const chosen = ruleList.find(rule => rule.key === focused);
      advancedNote.textContent = chosen?.warning || 'UP/DOWN  choose   +/- adjust (SHIFT x10)   ENTER  apply   ESC  cancel';
      advancedNote.classList.toggle('warn', Boolean(chosen?.warning));
    };
    const clamp = (rule, value) => Math.max(rule.minimum, Math.min(rule.maximum, rule.integer ? Math.round(value) : value));
    const settleRule = rule => {
      const input = ruleInputs.get(rule.key);
      const typed = parseFloat(input.value);
      if (!Number.isNaN(typed)) values[rule.key] = clamp(rule, typed);
      input.value = shown(rule, values[rule.key]);
      refresh();
    };
    let group = null;
    for (const rule of ruleList) {
      if (rule.group !== group) {
        group = rule.group;
        list.append(element('div', { class: 'group-heading' }, upper(group)));
      }
      const input = element('input', { inputmode: 'decimal', disabled: Boolean(rule.locked) });
      input.value = shown(rule, values[rule.key]);
      input.addEventListener('focus', () => { focused = rule.key; refresh(); });
      input.addEventListener('blur', () => settleRule(rule));
      input.addEventListener('input', () => {
        input.value = input.value.replace(rule.integer ? (rule.minimum < 0 ? /[^0-9-]/g : /[^0-9]/g) : (rule.minimum < 0 ? /[^0-9.-]/g : /[^0-9.]/g), '').slice(0, 12);
      });
      ruleInputs.set(rule.key, input);
      const warned = !rule.locked && rule.warning;
      list.append(numberRow({
        label: rule.label, input, locked: Boolean(rule.locked),
        hint: rule.locked || (warned ? `WARNING  ${rule.hint}` : rule.hint),
        hintClass: rule.locked ? 'lock' : warned ? 'warn' : '',
        onStep: steps => {
          focused = rule.key;
          values[rule.key] = clamp(rule, Number((values[rule.key] + steps * rule.step).toFixed(9)));
          input.value = shown(rule, values[rule.key]);
          refresh();
        },
      }));
    }
    const advancedShade = screen('advanced', panel => {
      panel.append(
        element('div', { class: 'screen-title' }, element('b', {}, 'BITZKEY'), element('span', {}, 'NEW WORLD  /  ADVANCED RULES')),
        element('div', { class: 'advanced-tools' },
          element('button', { onclick: () => {
            const rule = ruleList.find(r => r.key === focused);
            if (!rule) return;
            values[rule.key] = rule.default;
            ruleInputs.get(rule.key).value = shown(rule, rule.default);
            refresh();
          } }, 'RESET'),
          element('button', { onclick: () => {
            for (const rule of ruleList) {
              values[rule.key] = rule.default;
              ruleInputs.get(rule.key).value = shown(rule, rule.default);
            }
            refresh();
          } }, 'RESET ALL')),
        element('div', { class: 'lead natural-law' }, "These are the settings for what might be called 'natural law'.  They have been carefully balanced to produce a world with fair chances for all.  Messing with any of them can produce unpredictable results.  Have fun!"),
        lead, list,
        element('div', { class: 'screen-foot' },
          element('button', { onclick: closeAdvanced }, 'CANCEL'),
          element('button', { class: 'primary', onclick: applyAdvanced }, 'APPLY')),
        advancedNote);
    });
    refresh();
    async function applyAdvanced() {
      for (const rule of ruleList) if (!rule.locked) settleRule(rule);
      const checked = await api('/api/rules/check', { rules: values });
      if (checked.problems.length) {
        advancedNote.textContent = `${checked.problems[0]}.`;
        advancedNote.classList.add('warn');
        return;
      }
      rules = checked.changed;
      showRulesCount();
      const count = Object.keys(rules).length;
      tell(`${count} advanced rule${count !== 1 ? 's' : ''} changed from the defaults.`, false);
      closeAdvanced();
    }
    function onAdvancedKey(event) {
      if (!onTop(advancedShade)) return;
      if (event.key === 'Escape') { event.preventDefault(); closeAdvanced(); }
      else if (event.key === 'Enter') { event.preventDefault(); applyAdvanced(); }
      else if ((event.key === 'ArrowUp' || event.key === 'ArrowDown') && event.target instanceof HTMLInputElement) {
        event.preventDefault();
        const all = [...ruleInputs.values()].filter(input => !input.disabled);
        const next = all[(all.indexOf(event.target) + (event.key === 'ArrowDown' ? 1 : all.length - 1)) % all.length];
        next.focus();
        next.select();
        next.scrollIntoView({ block: 'nearest' });
      }
    }
    document.addEventListener('keydown', onAdvancedKey);
    function closeAdvanced() {
      document.removeEventListener('keydown', onAdvancedKey);
      advancedShade.remove();
    }
  }

  // ---- The designer

  async function openDesigner() {
    // The three words: the ring, the dot and the social background.
    const words = { ring: 0, dot: 0, social: 0 };
    let sample = null;
    const take = made => { for (const word of Object.keys(words)) words[word] = parseInt(made[word] || '0', 16) >>> 0; };
    const last = remembered('design', null);
    if (last) take(last);
    else take(await api('/api/design', { random: true, animal: true }));
    // Each field: which word, where its bits start, and how large it goes.
    const fields = [
      ...MIND_TRAITS.map((name, index) => ({ name, word: 'dot', shift: 3 * index, most: 7 })),
      { name: 'hardiness', word: 'dot', shift: 30, most: 3 },
      ...BODY_TRAITS.map((name, index) => ({ name, word: 'ring', shift: 3 * index, most: 7 })),
      { name: 'temperature', word: 'ring', shift: 30, most: 3 },
      ...SOCIAL_TRAITS.map((name, index) => ({ name, word: 'social', shift: 3 * index, most: 7 })),
      { name: 'neurodivergence', word: 'social', shift: 3 * SOCIAL_TRAITS.length, most: 15 },
    ];
    const valueOf = field => (words[field.word] >>> field.shift) & field.most;
    const setValue = (field, value) => {
      const clamped = Math.max(0, Math.min(field.most, value));
      words[field.word] = ((words[field.word] & ~(field.most << field.shift)) | (clamped << field.shift)) >>> 0;
    };
    const codes = () => ({ ring: hex(words.ring), dot: hex(words.dot), social: hex(words.social) });
    const hex = word => word.toString(16).toUpperCase().padStart(8, '0');
    const shownValue = (field, value) => {
      if (field.name === 'species') return `${SPECIES[value % 4]} ${value}`;
      if (field.name === 'temperature') return TEMPERATURES[value];
      if (field.name === 'hardiness') return ['none', 'heat', 'cold', 'both'][value];
      return String(value);
    };

    const preview = element('div', { class: 'preview-box' });
    const inputs = { ring: element('input', { maxlength: 8, spellcheck: false }), dot: element('input', { maxlength: 8, spellcheck: false }),
                     social: element('input', { maxlength: 8, spellcheck: false }) };
    const facts = element('div', { class: 'facts' });
    const columns = [element('div', { class: 'column' }, element('h4', {}, 'MIND  /  INNER DOT')),
                     element('div', { class: 'column' }, element('h4', {}, 'BODY  /  OUTER RING')),
                     element('div', { class: 'column social' }, element('h4', {}, 'SOCIAL  /  BACKGROUND'))];
    const socialGrid = element('div', { class: 'social-grid' });  // beneath the mind and body, in two columns
    columns[2].append(socialGrid);
    const fieldRows = fields.map((field, index) => {
      const meter = element('div', {});
      const value = element('div', { class: 'value' });
      const row = element('div', { class: 'field-row', tabindex: 0 },
        element('div', { class: 'field-text' }, element('div', { class: 'field-label', style: { textTransform: 'none' } }, field.name)),
        element('button', { tabindex: -1, onclick: event => { setValue(field, valueOf(field) - (event.shiftKey ? 5 : 1)); update(); } }, '-'),
        value,
        element('button', { tabindex: -1, onclick: event => { setValue(field, valueOf(field) + (event.shiftKey ? 5 : 1)); update(); } }, '+'),
        element('div', { class: 'meter' }, meter));
      row.addEventListener('keydown', event => {
        if (event.key === 'ArrowRight' || event.key === 'ArrowLeft') {
          event.preventDefault();
          setValue(field, valueOf(field) + (event.shiftKey ? 5 : 1) * (event.key === 'ArrowRight' ? 1 : -1));
          update();
        } else if (/^[0-9]$/.test(event.key)) {
          setValue(field, Number(event.key));
          update();
        } else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
          event.preventDefault();
          fieldRows[(index + (event.key === 'ArrowDown' ? 1 : fieldRows.length - 1)) % fieldRows.length].row.focus();
        }
      });
      row.addEventListener('focus', () => row.classList.add('focused'));
      row.addEventListener('blur', () => row.classList.remove('focused'));
      (field.word === 'social' ? socialGrid : columns[field.word === 'dot' ? 0 : 1]).append(row);
      return { row, meter, value, field };
    });

    let asked = 0;
    async function update(fromCode = false) {
      for (const { meter, value, field } of fieldRows) {
        const current = valueOf(field);
        meter.style.width = `${current / field.most * 100}%`;
        value.textContent = shownValue(field, current);
      }
      if (!fromCode) {
        for (const word of Object.keys(words)) inputs[word].value = hex(words[word]);
      }
      const ticket = ++asked;
      sample = await api('/api/design', codes());
      if (ticket !== asked) return;
      const size = sample.body[1];
      preview.replaceChildren(
        bitzkeyCanvas(look(sample), 16 * dotRadius(size), 16 * 2),
        element('div', { class: 'actual' }, 'actual size', bitzkeyCanvas(look(sample), dotRadius(size))));
      facts.replaceChildren(...sample.facts.map(line => element('div', {}, line)));
    }
    for (const [word, input] of Object.entries(inputs)) {
      input.addEventListener('input', () => {
        input.value = input.value.replace(/[^0-9a-fA-F]/g, '').toUpperCase().slice(0, 8);
        if (input.value.length === 8) {
          words[word] = parseInt(input.value, 16) >>> 0;
          update(true);
        }
      });
      input.addEventListener('blur', () => { input.value = hex(words[word]); });
    }

    const designerShade = screen('designer', panel => {
      panel.append(
        element('div', { class: 'screen-title' }, element('b', {}, 'BITZKEY DESIGNER'), element('span', {}, 'NEW WORLD  /  DESIGN AN IMPORT')),
        element('div', { class: 'lead' }, 'Every trait is part of the genome, so the dot, ring and social background colours change with them.'),
        element('div', { class: 'designer-body' },
          element('div', { class: 'preview' }, preview,
            element('div', { class: 'code-row' }, element('span', {}, 'RING / BODY'), inputs.ring),
            element('div', { class: 'code-row' }, element('span', {}, 'DOT / MIND'), inputs.dot),
            element('div', { class: 'code-row' }, element('span', {}, 'SOCIAL'), inputs.social),
            facts),
          element('div', { class: 'columns' }, ...columns)),
        element('div', { class: 'screen-note' }, 'UP/DOWN  choose   LEFT/RIGHT  adjust (SHIFT x5)   type a value or hex code   ENTER  apply   ESC  cancel'),
        element('div', { class: 'screen-foot', style: { justifyContent: 'space-between' } },
          element('button', { onclick: async () => {
            take(await api('/api/design', { random: true }));
            update();
          } }, 'RANDOM'),
          element('span', { style: { display: 'flex', gap: '12px' } },
            element('button', { onclick: closeDesigner }, 'CANCEL'),
            element('button', { class: 'primary', onclick: applyDesign }, 'APPLY'))));
    });
    update();
    fieldRows[0].row.focus();

    async function applyDesign() {
      const made = await api('/api/design', codes());
      remember('design', codes());
      addImport(made.record_text);
      tell(`Added a designed ${made.species} to the import list.`, false);
      closeDesigner();
    }
    function onDesignerKey(event) {
      if (!onTop(designerShade)) return;
      if (event.key === 'Escape') { event.preventDefault(); closeDesigner(); }
      else if (event.key === 'Enter') { event.preventDefault(); applyDesign(); }
    }
    document.addEventListener('keydown', onDesignerKey);
    function closeDesigner() {
      document.removeEventListener('keydown', onDesignerKey);
      designerShade.remove();
    }
  }

  // ---- The catalog

  function openSetupCatalog() {
    openCatalog({
      onImport(records) {
        addImport(records.text);
        tell(records.count === 1 ? 'Added a Bitzkey from the catalog to the import list, with its trained brain.'
                                 : `Added ${records.count} Bitzkeys from the catalog to the import list, with their trained brains.`, false);
      },
    });
  }

  return { update() {}, close };
}

// The catalog of favourite Bitzkeys. Opened from the setup panel, the Bitzkeys chosen go to `onImport`
// with their records; opened to browse (no `onImport`), it only shows them, and can remove them.
// `onClose` is told when it closes.
export function openCatalog({ onImport = null, onClose = () => {} } = {}) {
  const browsing = !onImport;
  let entries = [], chosen = new Set(), picked = 0, anchor = 0, catalogNote = '';
  const list = element('div', { class: 'catalog-list scroll' });
  const side = element('div', { class: 'catalog-details scroll' });
  const hint = element('span', { class: 'catalog-hint' });
  const importButton = element('button', { class: 'primary', onclick: importChosen }, 'ADD TO IMPORT');
  const removeButton = element('button', { onclick: askToRemove }, 'REMOVE');

  async function load() {
    const loaded = await api('/api/catalog');
    entries = loaded.entries;
    catalogNote = loaded.note || catalogNote;
    picked = Math.max(0, Math.min(entries.length - 1, picked));
    chosen = new Set([...chosen].filter(index => index < entries.length));
    if (entries.length && !chosen.size) chosen.add(picked);
    anchor = picked;
    draw();
  }

  function draw() {
    if (!entries.length) {
      list.replaceChildren(element('div', { class: 'empty-note' }, 'The catalog is empty.',
        element('small', {}, 'Add Bitzkeys from a running world: ADD TO CATALOG in the sidebar, the export list or the colour graph, or press K.')));
      side.replaceChildren();
    } else {
      list.replaceChildren(...entries.map((entry, index) => {
        const row = element('div', { class: `entry${chosen.has(index) ? ' chosen' : ''}${index === picked ? ' picked' : ''}` },
          bitzkeyCanvas(look(entry.details), dotRadius(entry.details.body[1])), entry.line);
        row.addEventListener('click', event => choose(index, event.shiftKey, event.ctrlKey || event.metaKey));
        return row;
      }));
      const agent = entries[picked].details;
      side.replaceChildren(element('div', { class: 'big-look' }, bitzkeyCanvas(look(agent), 9 * dotRadius(agent.body[1]), 18)), details(agent));
      list.children[picked]?.scrollIntoView({ block: 'nearest' });
    }
    importButton.textContent = chosen.size <= 1 ? 'ADD TO IMPORT' : `ADD ${chosen.size} TO IMPORT`;
    importButton.disabled = !chosen.size;
    removeButton.disabled = !chosen.size;
    hint.textContent = catalogNote || (browsing ? 'CLICK, SHIFT or CTRL+CLICK  choose    DELETE  remove    ESC  close'
                                                : 'CLICK, SHIFT or CTRL+CLICK  choose    ENTER  add to import    DELETE  remove    ESC  close');
    hint.classList.toggle('warn', Boolean(catalogNote));
  }

  // As in Windows: a click chooses one, CTRL+click adds or takes one out, SHIFT+click chooses the
  // run from the last clicked (CTRL+SHIFT adds it).
  function choose(index, shift, ctrl) {
    if (shift) {
      const run = [];
      for (let at = Math.min(anchor, index); at <= Math.max(anchor, index); ++at) run.push(at);
      chosen = ctrl ? new Set([...chosen, ...run]) : new Set(run);
    } else if (ctrl) {
      if (!chosen.delete(index)) chosen.add(index);
      anchor = index;
    } else {
      chosen = new Set([index]);
      anchor = index;
    }
    picked = index;
    draw();
  }

  async function askToRemove() {
    if (!chosen.size) return;
    const count = chosen.size;
    const answer = await dialog('Remove from the catalog?',
      [count === 1 ? entries[[...chosen][0]].line : plural(count), count === 1 ? 'It will be gone from the catalog for good.' : 'They will be gone from the catalog for good.'],
      [['keep', 'KEEP'], ['remove', 'REMOVE']], 'keep');
    if (answer !== 'remove') return;
    try {
      catalogNote = (await api('/api/catalog/remove', { indexes: [...chosen] })).note;
    } catch (error) {
      catalogNote = error.message;
    }
    chosen = new Set();
    await load();
  }

  async function importChosen() {
    if (!chosen.size || browsing) return;
    onImport(await api('/api/catalog/records', { indexes: [...chosen] }));
    closeCatalog();
  }

  const catalogShade = screen('catalog', panel => {
    panel.append(
      element('div', { class: 'screen-title' }, element('b', {}, 'BITZKEY CATALOG'),
        element('span', {}, browsing ? 'YOUR FAVOURITES' : 'NEW WORLD  /  YOUR FAVOURITES')),
      element('div', { class: 'lead' }, browsing ? 'Each one kept as it was when catalogued, its trained brain and all.'
                                                 : 'Each one kept as it was when catalogued, its trained brain and all. Choose those to import.'),
      element('div', { class: 'catalog-body' }, list, side),
      element('div', { class: 'catalog-foot' }, removeButton, hint, browsing ? null : importButton,
        element('button', { onclick: closeCatalog }, 'CLOSE')));
  });
  function onCatalogKey(event) {
    if (!onTop(catalogShade)) return;
    if (event.key === 'Escape') { event.preventDefault(); closeCatalog(); }
    else if (event.key === 'Enter') { event.preventDefault(); importChosen(); }
    else if (event.key === 'Delete') { event.preventDefault(); askToRemove(); }
    else if ((event.key === 'a' || event.key === 'A') && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      chosen = new Set(entries.map((_, index) => index));
      draw();
    } else if ((event.key === 'ArrowUp' || event.key === 'ArrowDown') && entries.length) {
      event.preventDefault();
      const moved = Math.max(0, Math.min(entries.length - 1, picked + (event.key === 'ArrowDown' ? 1 : -1)));
      choose(moved, event.shiftKey, false);
    }
  }
  document.addEventListener('keydown', onCatalogKey);
  function closeCatalog() {
    document.removeEventListener('keydown', onCatalogKey);
    catalogShade.remove();
    onClose();
  }
  load();
}
