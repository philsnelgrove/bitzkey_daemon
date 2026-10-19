// Languages. Every piece of text is written in English, and translated where it reaches the screen:
// text in the page (watched as it changes), tooltips and placeholders, the browser's own dialogs, and
// words drawn on the canvases. The catalogs, i18n/<code>.json, map the English to each language.
//
// A catalog key is the English exactly as shown, except that numbers can stand as {#} -- "{#} ALIVE"
// translates "12 ALIVE" and "9310 ALIVE" alike -- and any other changing words as {*}. A translation
// puts them back in order, or in another order with {#1}, {#2}, {*1}, {*2}, ... Keys in capitals are
// shown in capitals in every language that has them.
//
// The choice is this browser's own, kept with its other preferences. Opening the page with
// ?i18n=collect lists every piece of text that has no translation yet in window.bitzkeyMissing,
// for keeping the catalogs complete.

export const LANGUAGES = [
  ['en', 'English'], ['fr', 'Français'], ['it', 'Italiano'], ['es', 'Español'], ['de', 'Deutsch'],
  ['pt', 'Português'], ['pl', 'Polski'], ['uk', 'Українська'], ['zh', '简体中文'], ['ja', '日本語'], ['hi', 'हिन्दी'], ['he', 'עברית'],
];
const HTML_LANG = { zh: 'zh-Hans' };
const RIGHT_TO_LEFT = new Set(['he']);
const STORE = 'bitzkey.language';
const NUMBER = /[-+]?\d+(?:[.,]\d+)*/g;
// Not words: genome codes (hex), file names, dates and times.
const NOT_WORDS = /^(?:[\s0-9A-Fa-f#:.\-]*\d[\s0-9A-Fa-f#:.\-]*|[\w.\-]+\.(?:txt|json|bkw|bzw|csv))$/;

let language = 'en';
let exact = new Map();        // key -> translation
let spaced = new Map();       // key with its spaces run together -> key, for padded numbers
let folded = new Map();       // lowercased plain key -> translation, for the same words in another case
let patterns = [];            // [{regex, numbers: [...slot kinds], translation}]
const cache = new Map();
const missing = new Set();
const collecting = new URLSearchParams(location.search).get('i18n') === 'collect';
window.bitzkeyMissing = missing;

export function currentLanguage() { return language; }

function chosen() {
  let stored = null;
  try { stored = localStorage.getItem(STORE); } catch (error) { /* private browsing */ }
  if (stored && LANGUAGES.some(([code]) => code === stored)) return stored;
  for (const wanted of navigator.languages || [navigator.language || 'en']) {
    const code = wanted.toLowerCase().split('-')[0];
    if (LANGUAGES.some(([known]) => known === code)) return code;
  }
  return 'en';
}

function isCapitals(text) {
  return /[A-Z]/.test(text) && text === text.toUpperCase();
}

function escapeRegex(text) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function compile(key, translation) {
  // "{#} ALIVE  {#} LOST" -> /^([-+]?\d+...)\ ALIVE  (...)\ LOST$/ ; "{*}" matches any text.
  const kinds = [];
  const source = key.split(/(\{#\}|\{\*\})/).map(part => {
    if (part === '{#}') { kinds.push('#'); return `(${NUMBER.source})`; }
    if (part === '{*}') { kinds.push('*'); return '(.+?)'; }
    return escapeRegex(part);
  }).join('');
  return { regex: new RegExp(`^${source}$`, 's'), kinds, translation, capitals: isCapitals(key.replace(/\{[#*]\}/g, '')) };
}

function fill(translation, kinds, values) {
  const counters = { '#': 0, '*': 0 };
  const byKind = { '#': [], '*': [] };
  kinds.forEach((kind, index) => byKind[kind].push(values[index]));
  return translation.replace(/\{([#*])(\d*)\}/g, (whole, kind, number) => {
    const list = byKind[kind];
    const index = number ? Number(number) - 1 : counters[kind]++;
    return list[index] ?? whole;
  });
}

function shout(text, capitals) {
  return capitals ? text.toLocaleUpperCase(HTML_LANG[language] || language) : text;
}

// The text in this browser's language: unchanged when English, or when there is no translation.
export function tr(text) {
  if (language === 'en' || typeof text !== 'string') return text;
  const trimmed = text.trim();
  if (!trimmed || !/\p{L}/u.test(trimmed) || NOT_WORDS.test(trimmed)) return text;
  if (cache.has(text)) return cache.get(text);
  const lead = text.slice(0, text.indexOf(trimmed));
  const tail = text.slice(lead.length + trimmed.length);
  let result = null;
  if (exact.has(trimmed)) {
    result = shout(exact.get(trimmed), isCapitals(trimmed));
  } else if (folded.has(trimmed.toLowerCase())) {
    // The same words in another case: "grazers" from the key "GRAZERS".
    const translation = folded.get(trimmed.toLowerCase());
    result = isCapitals(trimmed) ? shout(translation, true)
           : /^\p{Ll}/u.test(trimmed) && language !== 'de' ? translation.charAt(0).toLocaleLowerCase(language) + translation.slice(1)
           : translation;
  } else {
    // Numbers as {#}, for the plain case; then the patterns with words that change.
    const values = [];
    const generic = trimmed.replace(NUMBER, number => { values.push(number); return '{#}'; });
    const key = exact.has(generic) ? generic : spaced.get(generic.replace(/\s+/g, ' '));
    if (values.length && key) {
      result = shout(fill(exact.get(key), values.map(() => '#'), values), isCapitals(key.replace(/\{#\}/g, '')));
    } else {
      for (const pattern of patterns) {
        const found = pattern.regex.exec(trimmed);
        if (found) {
          const parts = found.slice(1).map((value, index) => pattern.kinds[index] === '*' ? tr(value) : value);
          result = shout(fill(pattern.translation, pattern.kinds, parts), pattern.capitals);
          break;
        }
      }
    }
    if (result === null && collecting) missing.add(values.length ? generic : trimmed);
  }
  const shown = result === null ? text : lead + result + tail;
  if (cache.size > 20000) cache.clear();
  cache.set(text, shown);
  return shown;
}

// ---------------------------------------------------------------------------------------------
// The page: text nodes and attributes are translated as they appear or change.

const ATTRIBUTES = ['title', 'placeholder', 'aria-label'];
const shownText = new WeakMap();     // text node -> the translation last put in it

function translateNode(node) {
  if (node.nodeType === Node.TEXT_NODE) {
    const parent = node.parentNode;
    if (!parent || parent.nodeName === 'SCRIPT' || parent.nodeName === 'STYLE' || parent.closest?.('[data-no-translate]')) return;
    if (shownText.get(node) === node.data) return;
    const translated = tr(node.data);
    shownText.set(node, translated);
    // A button keeps its English label, for scripts and tests that find buttons in any language.
    if (translated !== node.data && parent.nodeName === 'BUTTON' && !parent.dataset.en) parent.dataset.en = node.data.trim();
    if (translated !== node.data) node.data = translated;
    return;
  }
  if (node.nodeType !== Node.ELEMENT_NODE || node.closest('[data-no-translate]')) return;
  for (const name of ATTRIBUTES) translateAttribute(node, name);
  if (node.tagName === 'INPUT' && (node.type === 'button' || node.type === 'submit')) translateAttribute(node, 'value');
  for (const child of node.childNodes) translateNode(child);
}

function translateAttribute(element, name) {
  const value = element.getAttribute(name);
  if (!value) return;
  const marker = `data-tr-${name}`;
  if (element.getAttribute(marker) === value) return;
  const translated = tr(value);
  element.setAttribute(marker, translated);
  if (translated !== value) element.setAttribute(name, translated);
}

function watch() {
  translateNode(document.body);
  new MutationObserver(changes => {
    for (const change of changes) {
      if (change.type === 'characterData') translateNode(change.target);
      else if (change.type === 'attributes') translateAttribute(change.target, change.attributeName);
      else change.addedNodes.forEach(translateNode);
    }
  }).observe(document.body, { subtree: true, childList: true, characterData: true, attributes: true,
                              attributeFilter: [...ATTRIBUTES, 'value'] });
}

// ---------------------------------------------------------------------------------------------
// Canvases and the browser's own dialogs.

function patchCanvas() {
  const context = CanvasRenderingContext2D.prototype;
  for (const name of ['fillText', 'strokeText', 'measureText']) {
    const original = context[name];
    context[name] = function (text, ...rest) { return original.call(this, tr(String(text)), ...rest); };
  }
  for (const name of ['alert', 'confirm', 'prompt']) {
    const original = window[name];
    window[name] = (text, ...rest) => original.call(window, tr(String(text ?? '')), ...rest);
  }
}

// ---------------------------------------------------------------------------------------------
// The menu.

function menu() {
  const select = document.createElement('select');
  select.id = 'language';
  select.className = 'language';
  select.title = 'Language';
  select.setAttribute('aria-label', 'Language');
  select.setAttribute('data-no-translate', '');
  for (const [code, name] of LANGUAGES) {
    const option = document.createElement('option');
    option.value = code;
    option.textContent = name;
    option.lang = HTML_LANG[code] || code;
    option.selected = code === language;
    select.append(option);
  }
  select.addEventListener('change', () => {
    try { localStorage.setItem(STORE, select.value); } catch (error) { /* private browsing */ }
    location.reload();
  });
  select.addEventListener('keydown', event => event.stopPropagation());  // the app's keys stay the app's
  const holder = document.getElementById('header') || document.body;
  holder.append(select);
}

export async function startLanguages() {
  language = chosen();
  document.documentElement.lang = HTML_LANG[language] || language;
  document.documentElement.dir = RIGHT_TO_LEFT.has(language) ? 'rtl' : 'ltr';
  if (language !== 'en') {
    try {
      const response = await fetch(`i18n/${language}.json`, { cache: 'no-cache' });
      const catalog = await response.json();
      for (const [key, translation] of Object.entries(catalog)) {
        if (!translation) continue;
        if (key.includes('{*}')) patterns.push(compile(key, translation));   // changing words: matched
        else exact.set(key, translation);                                     // plain, or numbers only
        if (!/\{[#*]\}/.test(key) && !folded.has(key.toLowerCase())) folded.set(key.toLowerCase(), translation);
        if (key.includes('{#}') && !key.includes('{*}')) spaced.set(key.replace(/\s+/g, ' '), key);
      }
      // Longer patterns first, so the most specific wins.
      patterns.sort((a, b) => b.regex.source.length - a.regex.source.length);
    } catch (error) {
      language = 'en';    // no catalog: English, rather than a broken page
    }
  }
  patchCanvas();
  menu();
  watch();
}
