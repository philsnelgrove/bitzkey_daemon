// Saving the whole world on the daemon, under a name, and loading a saved one to carry on, for everyone viewing it.

import { api, element, dialog, pad5 } from './common.js';

const NAME_PATTERN = /^[A-Za-z0-9_-]{1,60}$/;

// A dialog with a box to type in: resolves with the text when confirmed, or null on CANCEL or ESC.
function askName(title, lines, value) {
  return new Promise(resolve => {
    const input = element('input', { class: 'name-box', maxlength: 60, spellcheck: false });
    input.value = value;
    const problem = element('p', { class: 'bad' });
    const close = text => { shade.remove(); resolve(text); };
    const confirm = () => {
      const text = input.value.trim();
      if (!NAME_PATTERN.test(text)) {
        problem.textContent = 'Use up to 60 letters, digits, - and _.';
        return;
      }
      close(text);
    };
    input.addEventListener('keydown', event => {
      event.stopPropagation();
      if (event.key === 'Enter') { event.preventDefault(); confirm(); }
      if (event.key === 'Escape') { event.preventDefault(); close(null); }
    });
    const shade = element('div', { class: 'shade dialog-shade' },
      element('div', { class: 'dialog' },
        element('h2', {}, title),
        lines.map(line => element('p', {}, line)),
        input, problem,
        element('div', { class: 'dialog-buttons' },
          element('button', { onclick: () => close(null) }, 'CANCEL'),
          element('button', { class: 'primary', onclick: confirm }, 'SAVE'))));
    document.body.append(shade);
    input.focus();
    input.select();
  });
}

// Ask for a name and save the world under it. Resolves with the daemon's note, or null if cancelled.
export async function saveWorld(turn) {
  let name = `world-turn-${pad5(turn)}`;
  let lines = ['The whole world is saved on the daemon, to load from NEW WORLD and carry on exactly as it would have.',
               'Everyone using the daemon sees its saved worlds.'];
  for (;;) {
    name = await askName('Save World', lines, name);
    if (name === null) return null;
    try {
      return (await api('/api/world/save', { name })).note;
    } catch (error) {
      lines = [error.message];
    }
  }
}

// Choose a saved world and load it in place of the present one. Resolves with the daemon's answer (its state
// and a note), or null if cancelled.
export function loadWorld() {
  return new Promise(resolve => {
    const list = element('div', { class: 'files scroll' }, element('p', {}, 'Looking for saved worlds...'));
    const problem = element('p', { class: 'bad' });
    const close = result => { document.removeEventListener('keydown', onKey, true); shade.remove(); resolve(result); };
    const onKey = event => {
      if (event.key === 'Escape') { event.stopPropagation(); event.preventDefault(); close(null); }
    };
    const shade = element('div', { class: 'shade dialog-shade', onclick: event => { if (event.target === shade) close(null); } },
      element('div', { class: 'dialog' },
        element('h2', {}, 'Load World'),
        element('p', {}, 'The world chosen replaces the one running now, for everyone viewing it, and waits paused.'),
        list, problem,
        element('div', { class: 'dialog-buttons' }, element('button', { onclick: () => close(null) }, 'CANCEL'))));
    document.body.append(shade);
    document.addEventListener('keydown', onKey, true);
    // Each saved world: its name loads it, and DELETE, once confirmed, deletes it for everyone.
    const row = world => element('div', { class: 'world-row' },
      element('a', {
        href: '#', onclick: async event => {
          event.preventDefault();
          problem.textContent = `Loading ${world.name}, please be patient...`;
          try {
            close(await api('/api/world/load', { name: world.name }));
          } catch (error) {
            problem.textContent = error.message;
          }
        },
      }, element('span', {}, world.name), element('span', {}, `${Math.ceil(world.size / 1048576)} MB`)),
      element('button', {
        class: 'delete', title: `Delete ${world.name}`, onclick: async () => {
          const choice = await dialog(`Delete ${world.name}?`, ['It is deleted from the daemon for everyone, and can't be brought back.'],
                                      [['keep', 'CANCEL'], ['delete', 'DELETE']], 'keep');
          if (choice !== 'delete') return;
          try {
            problem.textContent = (await api('/api/world/delete', { name: world.name })).note;
            refresh();
          } catch (error) {
            problem.textContent = error.message;
          }
        },
      }, 'DELETE'));
    const refresh = async () => {
      try {
        const { worlds } = await api('/api/worlds');
        list.replaceChildren(...(worlds.length ? worlds.map(row)
          : [element('p', {}, 'No world has been saved yet: SAVE WORLD, below the map, saves one.')]));
      } catch (error) {
        problem.textContent = error.message;
      }
    };
    refresh();
  });
}
