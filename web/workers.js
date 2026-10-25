// Workers: other daemons that hold some of this world's brains and think for them; becoming one; and, on a
// daemon that is a worker, the gray panel that is all its page shows.

import { api, element, dialog } from './common.js';

const DEFAULT_PORT = 50000;
const REFRESH_MS = 1000;
const MB = 1048576;

const STATES = {
  working: 'WORKING', connecting: 'CONNECTING', lost: 'LOST', stopping: 'STOPPING', away: 'AWAY', refused: 'REFUSED',
};

const megabytes = bytes => Math.round(bytes / MB);
const speedText = speed => `${Math.round(speed).toLocaleString('en')} a second`;

// What a worker shares: cores and memory, and GPUs if it has any.
function shares(worker) {
  const parts = [`${worker.threads} cores`, `${megabytes(worker.memory)} MB`];
  if (worker.gpus?.length) parts.push(...worker.gpus);
  return parts.join(', ');
}

// Text with its web addresses as links, opening in a new tab.
function linked(text) {
  return text.split(/(https?:\/\/[^\s<>"]+)/).map((part, index) => index % 2
    ? element('a', { href: part, target: '_blank', rel: 'noopener noreferrer' }, part)
    : part);
}

// A row of the form: a label, its control, and a hint beside it.
function field(label, control, hint) {
  return element('label', { class: 'worker-field' }, element('span', { class: 'worker-label' }, label), control,
                 hint ? element('span', { class: 'worker-hint' }, hint) : null);
}

function numberBox(value, min, max) {
  const box = element('input', { type: 'number', min, max, step: 1, class: 'worker-number' });
  box.value = value;
  box.addEventListener('keydown', event => event.stopPropagation());
  return box;
}

// The cores, memory and GPUs to share, with this computer's own; read() gives what was chosen.
function shareControls(machine, shared) {
  const cores = numberBox(shared.threads, 1, machine.threads);
  const memory = numberBox(megabytes(shared.memory), 64, megabytes(machine.memory));
  const gpus = machine.gpus.map((gpu, index) => {
    const box = element('input', { type: 'checkbox' });
    box.checked = shared.gpus.includes(index);
    return { box, row: element('label', { class: 'worker-gpu' }, box, element('span', { 'data-no-translate': '' }, gpu.name),
                                element('span', { class: 'worker-hint' }, `${megabytes(gpu.memory)} MB`)) };
  });
  const view = element('div', { class: 'worker-shares' },
    field('Cores', cores, `of ${machine.threads}`),
    field('Memory (MB)', memory, `of ${megabytes(machine.memory)} MB, for brains`),
    gpus.length ? element('div', { class: 'worker-field' }, element('span', { class: 'worker-label' }, 'GPUs'),
                          element('div', {}, gpus.map(gpu => gpu.row)))
                : element('p', { class: 'worker-hint' }, 'No GPU found: it thinks on the CPU.'));
  const read = () => ({
    threads: Number(cores.value),
    memory_mb: Number(memory.value),
    gpus: gpus.flatMap((gpu, index) => gpu.box.checked ? [index] : []),
  });
  return { view, read };
}

// ---------------------------------------------------------------------------------------------
// The main service's workers

export function openWorkers(onClose) {
  let timer = null;
  const close = () => {
    clearTimeout(timer);
    document.removeEventListener('keydown', onKey, true);
    shade.remove();
    onClose?.();
  };
  const onKey = event => {
    // ESC closes a dialog opened over this one, before this.
    if (event.key === 'Escape' && document.querySelectorAll('.dialog-shade').length === 1) { event.stopPropagation(); event.preventDefault(); close(); }
  };
  const table = element('div', { class: 'report-body worker-table' });
  const problem = element('p', { class: 'bad' });
  const host = element('input', { class: 'worker-host', placeholder: 'IP address', spellcheck: false });
  const port = numberBox('', 1, 65535);  // empty: the default
  port.placeholder = String(DEFAULT_PORT);
  port.title = 'Its port, if not 50000';
  const message = element('input', { class: 'worker-message', maxlength: 1000, placeholder: "A message for the worker's page (optional); it can hold a link" });
  for (const box of [host, message]) box.addEventListener('keydown', event => event.stopPropagation());
  const checkpoint = numberBox(100, 1, 100000);
  const hostShare = numberBox(100, 0, 100);
  let checkpointShown = false;

  const say = text => { problem.textContent = text; };
  const act = async (path, body) => {
    try {
      const result = await api(path, body);
      say(result.note || '');
      show(result);
      return true;
    } catch (error) {
      say(error.message);
      return false;
    }
  };

  const show = list => {
    if (!checkpointShown) {
      checkpoint.value = list.checkpoint_turns;
      hostShare.value = list.host_share;
      checkpointShown = true;
    }
    const row = (cells, worker) => element('tr', {}, cells.map((cell, index) => element('td', index === 0 ? { 'data-no-translate': '' } : {}, cell)), element('td', {},
      worker ? element('button', { class: 'delete', onclick: () => remove(worker) }, 'REMOVE') : null));
    const local = list.local;
    table.replaceChildren(element('table', { class: 'report-table' },
      element('tr', {}, ['WORKER', 'STATE', 'PING', 'SHARES', 'SPEED', 'BRAINS', ''].map(name => element('th', {}, name))),
      element('tr', {}, element('td', {}, 'This service'), element('td', {}, 'WORKING'), element('td', {}, ''),
              element('td', {}, local.description), element('td', {}, speedText(local.pace || local.speed)),
              element('td', {}, String(local.brains)), element('td', {})),
      list.workers.map(worker => row([
        element('div', {}, `${worker.host}:${worker.port}`, worker.message ? element('div', { class: 'worker-note', dir: 'auto' }, worker.message) : null),
        element('div', {}, element('span', { class: worker.state === 'working' ? 'worker-on' : 'worker-off' }, STATES[worker.state] || worker.state),
                worker.error ? element('div', { class: 'worker-note' }, worker.error) : null),
        worker.ping_ms >= 0 ? `${worker.ping_ms.toFixed(1)} ms` : '',
        worker.state === 'working' ? shares(worker) : '',
        worker.state === 'working' ? speedText(worker.pace || worker.speed) : '',
        String(worker.brains),
      ], worker))));
  };

  const refresh = async () => {
    try {
      show(await api('/api/workers'));
    } catch (error) {
      say(error.message);
    }
    timer = setTimeout(refresh, REFRESH_MS);
  };

  const add = async () => {
    const given = { host: host.value.trim(), message: message.value.trim() };
    if (port.value.trim()) given.port = Number(port.value);
    if (await act('/api/workers/add', given)) {
      host.value = '';
      port.value = '';
      message.value = '';
    }
  };

  const remove = async worker => {
    const choice = await dialog(`Remove ${worker.host}:${worker.port}?`,
      ['Its brains are brought back to this service first, and it no longer works for it.'],
      [['keep', 'CANCEL'], ['remove', 'REMOVE']], 'keep');
    if (choice === 'remove') {
      say('Bringing its brains back...');
      await act('/api/workers/remove', { host: worker.host, port: worker.port });
    }
  };

  const shade = element('div', { class: 'shade dialog-shade' },
    element('div', { class: 'dialog wide' },
      element('h2', {}, 'Workers'),
      element('p', {}, 'Workers are other Bitzkey daemons in worker mode. Each holds some of this world\'s brains, and thinks and learns for them; ' +
                       'this service shares the brains out by the pace each keeps, and keeps a share itself.'),
      table,
      element('div', { class: 'group-heading' }, 'Add a worker'),
      element('div', { class: 'worker-add' }, host, port, message, element('button', { class: 'primary', onclick: add }, 'ADD')),
      element('div', { class: 'group-heading' }, 'Checkpoints'),
      element('div', { class: 'worker-add' },
        element('span', {}, 'Bring every brain back every'), checkpoint, element('span', {}, 'turns'),
        element('button', { onclick: () => act('/api/workers/checkpoint', { turns: Number(checkpoint.value) }) }, 'SET')),
      element('p', { class: 'worker-hint' }, 'Should a worker fail, its brains go to the others as they were at the last checkpoint.'),
      element('div', { class: 'group-heading' }, 'Host share'),
      element('div', { class: 'worker-add' },
        element('span', {}, 'This service thinks at'), hostShare, element('span', {}, '% of its pace'),
        element('button', { onclick: () => act('/api/workers/host_share', { percent: Number(hostShare.value) }) }, 'SET')),
      element('p', { class: 'worker-hint' }, 'Brains are shared out by the pace each machine keeps on real turns. At 0, this service only runs the world.'),
      problem,
      element('div', { class: 'dialog-buttons' },
        element('button', { onclick: async () => { if (await becomeWorker()) close(); } }, 'BECOME A WORKER...'),
        element('button', { class: 'primary', onclick: close }, 'CLOSE'))));
  document.body.append(shade);
  document.addEventListener('keydown', onKey, true);
  refresh();
  host.focus();
}

// Set this daemon up as a worker for a main service elsewhere. Resolves true once it is one.
async function becomeWorker() {
  const machine = await api('/api/machine');
  return new Promise(resolve => {
    const port = numberBox(DEFAULT_PORT, 1, 65535);
    const controls = shareControls(machine, {
      threads: Math.max(1, Math.floor(machine.threads / 2)), memory: Math.max(64 * MB, Math.floor(machine.memory / 2 / MB) * MB),
      gpus: machine.gpus.map((gpu, index) => index),
    });
    const problem = element('p', { class: 'bad' });
    const close = result => { shade.remove(); resolve(result); };
    const start = async () => {
      const choice = await dialog('Become a worker?',
        ["This daemon's world is paused, and its page shows only that it is a worker, for everyone viewing it.",
         'Worker mode is turned off from that page.'],
        [['stay', 'CANCEL'], ['start', 'BECOME A WORKER']], 'stay');
      if (choice !== 'start') return;
      try {
        await api('/api/worker/start', { port: Number(port.value), ...controls.read() });
        close(true);
        location.reload();
      } catch (error) {
        problem.textContent = error.message;
      }
    };
    const shade = element('div', { class: 'shade dialog-shade confirm' },
      element('div', { class: 'dialog' },
        element('h2', {}, 'Become a Worker'),
        element('p', {}, 'A worker runs no world of its own: it holds brains for a main service elsewhere, and thinks for them. ' +
                         "The main service adds it by this computer's IP address and the port below."),
        element('div', { class: 'worker-shares' }, field('Port', port, 'to listen on for the main service')),
        controls.view,
        problem,
        element('div', { class: 'dialog-buttons' },
          element('button', { onclick: () => close(false) }, 'CANCEL'),
          element('button', { class: 'primary', onclick: start }, 'BECOME A WORKER'))));
    document.body.append(shade);
  });
}

// ---------------------------------------------------------------------------------------------
// The page of a daemon that is a worker

export async function showWorkerPanel() {
  for (const id of ['header', 'main', 'footer']) document.getElementById(id).hidden = true;
  const machine = await api('/api/machine').catch(() => ({ threads: 1, memory: 0, gpus: [] }));
  const lead = element('p', { class: 'worker-lead' });
  const note = element('div', { class: 'worker-message-box', dir: 'auto', 'data-no-translate': '' });
  const noteHolder = element('div', {}, element('div', { class: 'worker-label' }, 'Message from the main service'), note);
  const facts = element('div', { class: 'worker-facts' });
  const problem = element('p', { class: 'bad' });
  const stopButton = element('button', { class: 'worker-stop' }, 'STOP BEING A WORKER');
  let controls = null;
  const sharing = element('div');
  const portBox = numberBox(DEFAULT_PORT, 1, 65535);
  const moving = element('div', { class: 'worker-shares' }, field('Port', element('span', { class: 'worker-add' }, portBox,
    element('button', { onclick: async () => {
      try {
        await api('/api/worker/port', { port: Number(portBox.value) });
        problem.textContent = `Now listening on port ${portBox.value}; the main service follows.`;
      } catch (error) {
        problem.textContent = error.message;
      }
    } }, 'CHANGE')), 'to listen on for the main service'));
  const panel = element('div', { class: 'worker-panel' },
    element('div', { class: 'worker-card' },
      element('div', { class: 'worker-title' }, 'WORKER', element('span', { class: 'worker-about' }, window.bitzkeyAbout || '')),
      lead, noteHolder, facts,
      element('div', { class: 'group-heading' }, 'Shared with the main service'),
      moving,
      sharing,
      element('div', { class: 'dialog-buttons' },
        element('button', { onclick: async () => {
          try {
            await api('/api/worker/share', controls.read());
            problem.textContent = 'Shared as chosen.';
          } catch (error) {
            problem.textContent = error.message;
          }
        } }, 'APPLY')),
      problem,
      element('div', { class: 'worker-foot' }, stopButton)));
  const language = document.getElementById('language');
  if (language) panel.querySelector('.worker-card').prepend(element('div', { class: 'worker-language' }, language));
  document.body.append(panel);

  stopButton.onclick = async () => {
    const choice = await dialog('Stop being a worker?',
      ['Its brains are handed back to the main service first, which takes a few seconds.', 'This daemon then runs a world of its own again.'],
      [['stay', 'CANCEL'], ['stop', 'STOP']], 'stay');
    if (choice !== 'stop') return;
    try {
      await api('/api/worker/stop', {});
    } catch (error) {
      problem.textContent = error.message;
    }
  };

  const update = async () => {
    try {
      const state = await api('/api/state');
      if (state.mode !== 'worker') {
        location.reload();  // a main service again: its own world
        return;
      }
      const worker = state.worker;
      if (worker.leaving) {
        lead.textContent = 'Handing the brains back to the main service...';
      } else if (worker.connected) {
        lead.replaceChildren('This Bitzkey daemon is a worker, thinking for the main service at ',
                             element('b', { 'data-no-translate': '' }, worker.main), '.');
      } else if (worker.main) {
        lead.replaceChildren('This Bitzkey daemon is a worker for the main service at ',
                             element('b', { 'data-no-translate': '' }, worker.main), ", which hasn't been in touch lately.");
      } else {
        lead.replaceChildren(`This Bitzkey daemon is a worker, waiting on port ${worker.port} for a main service to add it.`);
      }
      noteHolder.hidden = !worker.message;
      note.replaceChildren(...linked(worker.message || ''));
      facts.replaceChildren(
        element('div', {}, `Brains held: ${worker.held}`),
        element('div', {}, element('span', {}, 'Thinking on '), element('span', {}, worker.description)),
        element('div', {}, `Speed: ${speedText(worker.speed)}`),
        element('div', {}, `Port ${worker.port}`));
      stopButton.disabled = worker.leaving;
      if (!controls) {
        portBox.value = worker.port;
        controls = shareControls(machine, worker.shared);
        sharing.replaceChildren(controls.view);
      }
    } catch (error) {
      lead.textContent = "The daemon can't be reached; trying again...";
    }
    setTimeout(update, REFRESH_MS);
  };
  update();
}
