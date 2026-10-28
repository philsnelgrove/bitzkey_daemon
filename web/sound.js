// The sound of a button pressed: a quarter-second click and beep, made here rather than fetched, as the desktop
// app's. Any button on the page pressed with the left button plays it.

const LENGTH = 0.25;  // seconds
const PITCH = 1600;   // the beep's, in hertz

let context = null;
let sound = null;

// A click, a few milliseconds of a sharp falling edge with a little noise, under a beep that sounds at once and dies
// away, the whole fading to nothing by its end so it doesn't pop.
function clickSound(rate) {
  const samples = new Float32Array(Math.floor(LENGTH * rate));
  let noise = 0x9E3779B9;
  for (let index = 0; index < samples.length; index++) {
    const time = index / rate;
    noise = (Math.imul(noise, 1664525) + 1013904223) >>> 0;
    const hiss = (noise >>> 8) / (1 << 24) * 2 - 1;
    const click = Math.exp(-time / 0.0015) * (0.7 + 0.3 * hiss);
    const beep = Math.min(1, time / 0.002) * Math.exp(-time / 0.045) * Math.sin(2 * Math.PI * PITCH * time);
    const fade = Math.min(1, (LENGTH - time) / 0.02);
    samples[index] = 0.3 * (0.6 * click + beep) * fade;
  }
  return samples;
}

document.addEventListener('pointerdown', event => {
  if (event.button !== 0 || !event.target.closest || !event.target.closest('button')) return;
  try {
    if (!context) {  // a browser lets a page make sound only once it has been touched
      context = new AudioContext();
      const samples = clickSound(context.sampleRate);
      sound = context.createBuffer(1, samples.length, context.sampleRate);
      sound.copyToChannel(samples, 0);
    }
    const source = context.createBufferSource();
    source.buffer = sound;
    source.connect(context.destination);
    source.start();
  } catch {
    // no sound to be had: buttons are silent
  }
}, true);
