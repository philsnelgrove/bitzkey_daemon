// What every screen shares: talking to the daemon, dialogs, the clipboard, and small helpers.

const FETCH_TIMEOUT_MS = 10000;  // a request the daemon hasn't answered by then fails, rather than stall the page

// A fetch that gives up after FETCH_TIMEOUT_MS.
function request(path, body) {
  const options = body === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
  return fetch(path, { ...options, signal: AbortSignal.timeout(FETCH_TIMEOUT_MS) });
}

export async function api(path, body) {
  const response = await request(path, body);
  const text = await response.text();
  let value;
  try {
    value = text ? JSON.parse(text) : {};
  } catch {
    throw new Error(`The daemon answered ${response.status}: ${text.slice(0, 200)}`);
  }
  if (!response.ok) throw new Error(value.error || `The daemon answered ${response.status}`);
  return value;
}

export async function binary(path, body) {
  const response = await request(path, body);
  if (!response.ok) throw new Error(`The daemon answered ${response.status}`);
  return new DataView(await response.arrayBuffer());
}

export function element(tag, attributes = {}, ...children) {
  const made = document.createElement(tag);
  for (const [key, value] of Object.entries(attributes)) {
    if (key === 'class') made.className = value;
    else if (key === 'style' && typeof value === 'object') Object.assign(made.style, value);
    else if (key.startsWith('on')) made.addEventListener(key.slice(2), value);
    else if (value !== undefined && value !== null && value !== false) made.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat(Infinity)) {  // nested lists too: the death report's tables are two deep
    if (child === null || child === undefined || child === false) continue;
    made.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return made;
}

export const pad3 = number => String(number).padStart(3, '0');
export const pad5 = number => String(number).padStart(5, '0');
export const plural = (count, word = 'Bitzkey') => `${count} ${word}${count !== 1 ? 's' : ''}`;
export const upper = text => String(text).toUpperCase();

// A modal dialog: a title, lines of text, and buttons. Resolves with the key of the button chosen,
// or the cancel key on ESC.
export function dialog(title, lines, buttons, cancel) {
  return new Promise(resolve => {
    const close = key => {
      document.removeEventListener('keydown', onKey, true);
      shade.remove();
      resolve(key);
    };
    const onKey = event => {
      if (event.key === 'Escape') { event.stopPropagation(); event.preventDefault(); close(cancel); }
      if (event.key === 'Enter') { event.stopPropagation(); event.preventDefault(); close(buttons[buttons.length - 1][0]); }
    };
    const shade = element('div', { class: 'shade dialog-shade' },
      element('div', { class: 'dialog' },
        element('h2', {}, title),
        lines.map(line => element('p', {}, line)),
        element('div', { class: 'dialog-buttons' },
          buttons.map(([key, label], index) => element('button', {
            class: index === buttons.length - 1 ? 'primary' : '', onclick: () => close(key) }, label)))));
    document.body.append(shade);
    document.addEventListener('keydown', onKey, true);
    shade.querySelector('.dialog-buttons button:last-child')?.focus();
  });
}

// Copy text to the clipboard. Browsers allow the modern clipboard only on secure pages (HTTPS or the
// computer itself); elsewhere the old way is tried, and failing that, the text is shown to copy by hand.
export async function copyText(text) {
  if (navigator.clipboard && window.isSecureContext) {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch { /* fall through */ }
  }
  const area = element('textarea', { style: { position: 'fixed', left: '-9999px', top: '0' } });
  area.value = text;
  document.body.append(area);
  area.select();
  let copied = false;
  try { copied = document.execCommand('copy'); } catch { copied = false; }
  area.remove();
  if (copied) return true;
  await showText('Copy these Bitzkeys', text);
  return false;
}

// The text in a box, selected, to copy by hand.
export function showText(title, text) {
  return new Promise(resolve => {
    const area = element('textarea', { class: 'copy-box', readonly: true });
    area.value = text;
    const close = () => { shade.remove(); resolve(); };
    const shade = element('div', { class: 'shade dialog-shade' },
      element('div', { class: 'dialog wide' },
        element('h2', {}, title),
        element('p', {}, 'This browser won\'t let the page reach the clipboard here. Press CTRL+C to copy, then close.'),
        area,
        element('div', { class: 'dialog-buttons' }, element('button', { class: 'primary', onclick: close }, 'CLOSE'))));
    document.body.append(shade);
    area.focus();
    area.select();
  });
}

export function download(name, text) {
  const link = element('a', { href: URL.createObjectURL(new Blob([text], { type: 'application/json' })), download: name });
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

// Something a viewer keeps between visits, in their own browser.
export function remembered(key, fallback) {
  try {
    const value = localStorage.getItem(`bitzkey.${key}`);
    return value === null ? fallback : JSON.parse(value);
  } catch {
    return fallback;
  }
}

export function remember(key, value) {
  try { localStorage.setItem(`bitzkey.${key}`, JSON.stringify(value)); } catch { /* not kept */ }
}
