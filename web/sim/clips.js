// SPDX-License-Identifier: GPL-3.0-only
// ObsSynth FM-1: 2026 Orian Beltrame
// MIDI clips for trying the synth: played into the simulator (its MIDI in, as USB would bring them) or out of a
// Web MIDI port (an FM-1 on USB: "ObsSynthFM1"), on a channel, once or looping. The page's Clips panel drives it;
// from the browser's console too: fm1clips.list(), fm1clips.play(0, { to: 'ObsSynthFM1', loop: true }),
// fm1clips.stop().
//
// A clip: its tempo, its notes as [start, length, note, velocity] in beats (a quarter note: 1).
import { tr } from './i18n.js';

const C = (start, len, note, vel = 100) => [start, len, note, vel];

function seq(notes, step, len = step * 0.9, vel = 100, from = 0) {   // a run of notes, one each step
  return notes.map((n, i) => C(from + i * step, len, n, Array.isArray(vel) ? vel[i % vel.length] : vel));
}

function chords(list, beats, vel = 90) {                             // chords, one every beats
  return list.flatMap((ch, i) => ch.map(n => C(i * beats, beats * 0.95, n, vel)));
}

export const CLIPS = [
  { name: tr('C scale', 'Escala de C'), bpm: 120, notes: seq([60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60], 0.5) },
  { name: tr('Simple melody', 'Melodia simples'), bpm: 110,
    notes: [...seq([60, 64, 67, 72], 0.5), C(2, 1, 71), ...seq([67, 64, 62], 0.5, 0.45, 100, 3), C(4.5, 1, 60),
            ...seq([67, 65, 64, 62], 0.5, 0.45, 100, 5.5), C(7.5, 1.5, 60)] },
  { name: tr('Cm arpeggio', 'Arpejo Cm'), bpm: 128, notes: seq([48, 51, 55, 60, 63, 67, 72, 67, 63, 60, 55, 51], 0.25, 0.2) },
  { name: tr('Chords Am F C G', 'Acordes Am F C G'), bpm: 90,
    notes: chords([[57, 60, 64], [53, 57, 60], [48, 52, 55, 60], [55, 59, 62]], 4) },
  { name: tr('Long pad', 'Pad longo'), bpm: 70, notes: chords([[48, 55, 60, 64], [45, 52, 57, 60]], 8, 80) },
  { name: tr('Eighth-note bass', 'Baixo em colcheias'), bpm: 120,
    notes: seq([36, 36, 48, 36, 39, 36, 43, 41, 36, 36, 48, 36, 46, 43, 41, 39], 0.5, 0.4, [110, 80, 100, 80]) },
  { name: 'Acid 16', bpm: 132,
    notes: seq([36, 36, 48, 36, 39, 51, 36, 43, 36, 46, 36, 48, 41, 36, 53, 39], 0.25, 0.18,
               [120, 70, 110, 60, 90, 127, 60, 100, 70, 110, 60, 127, 80, 60, 120, 90]) },
  { name: tr('Fast staccato', 'Staccato rápido'), bpm: 140, notes: seq(Array(16).fill(64), 0.25, 0.08) },
  { name: 'Legato (glide)', bpm: 96,                                  // each note held into the next: MONO / LEGATO
    notes: [C(0, 1.2, 60), C(1, 1.2, 67), C(2, 1.2, 64), C(3, 1.2, 72), C(4, 1.2, 70), C(5, 2, 63)] },
  { name: tr('Velocity ramp', 'Rampa de velocity'), bpm: 120,
    notes: Array.from({ length: 12 }, (_, i) => C(i * 0.5, 0.4, 60, Math.round(10 + i * 117 / 11))) },
  { name: tr('Whole keyboard', 'Teclado inteiro'), bpm: 160, notes: seq(Array.from({ length: 13 }, (_, i) => 24 + i * 6), 0.5, 0.45) },
  { name: tr('Four voices + one', 'Quatro vozes + uma'), bpm: 100,                             // a fifth note over four: voice stealing
    notes: [C(0, 4, 48), C(0.5, 3.5, 55), C(1, 3, 60), C(1.5, 2.5, 64), C(2, 2, 67)] },
];

// ---- the player: a 10 ms scheduler, note ons and offs in time order; stop sends every note off (and CC 123)
let timer = null, startMs = 0, events = [], next = 0, loopLen = 0, out = null, chan = 0, looping = false;
const sounding = new Set();

function emit(status, d1, d2) {
  if (out) out(status | chan, d1, d2);
  if ((status & 0xF0) === 0x90 && d2) sounding.add(d1);
  else sounding.delete(d1);
}

function tick() {
  const beatMs = events.beatMs, now = performance.now() - startMs;
  while (next < events.length && events[next].at * beatMs <= now) {
    const e = events[next++];
    emit(e.on ? 0x90 : 0x80, e.note, e.on ? e.vel : 0);
  }
  if (next >= events.length) {
    if (looping && now >= loopLen * beatMs) {
      startMs += loopLen * beatMs;
      next = 0;
    } else if (!looping) {
      stop();
      onEnd();
    }
  }
}

let onEnd = () => {};

export function stop() {
  if (timer) clearInterval(timer);
  timer = null;
  for (const n of [...sounding]) emit(0x80, n, 0);
  if (out) out(0xB0 | chan, 123, 0);                                   // all notes off, for good measure
}

// play clip (a CLIPS entry) through send(status, d1, d2) on channel ch (1..16)
export function play(clip, send, ch = 1, loop = false, ended = () => {}) {
  stop();
  out = send;
  chan = (ch - 1) & 15;
  looping = loop;
  onEnd = ended;
  events = clip.notes.flatMap(([s, l, note, vel]) => [{ at: s, on: true, note, vel }, { at: s + l, on: false, note }])
    .sort((a, b) => a.at - b.at || (a.on ? 1 : 0) - (b.on ? 1 : 0));   // (at a time: the offs first)
  events.beatMs = 60000 / clip.bpm;
  loopLen = Math.ceil(Math.max(...clip.notes.map(([s, l]) => s + l)));
  next = 0;
  startMs = performance.now();
  timer = setInterval(tick, 10);
  tick();
}

// ---- the panel: a clip, where to (the simulator, or a MIDI output), a channel, loop, play / stop
export async function initClips(sendSim) {
  const sel = document.getElementById('clip-sel'), dest = document.getElementById('clip-dest');
  const chSel = document.getElementById('clip-ch'), loopBox = document.getElementById('clip-loop');
  const playBtn = document.getElementById('clip-play'), stopBtn = document.getElementById('clip-stop');
  const status = document.getElementById('clip-status');
  const toSim = (st, d1, d2) => sendSim({ t: 'midi', st, d1, d2 });
  let access = null;
  if (!sel) return;
  CLIPS.forEach((c, i) => sel.add(new Option(c.name, String(i))));
  for (let c = 1; c <= 16; c++) chSel.add(new Option(String(c), String(c)));

  function outputs() {                                                 // the destination list, the FM-1 first
    const keep = dest.value;
    dest.length = 0;
    dest.add(new Option(tr('Simulator (this page)', 'Simulador (esta página)'), 'sim'));
    if (access) {
      const outs = [...access.outputs.values()].sort((a, b) => /obssynth|felucca|fm-1/i.test(b.name) -
                                                                 /obssynth|felucca|fm-1/i.test(a.name));
      for (const o of outs) dest.add(new Option(`MIDI: ${o.name}`, o.id));
    }
    dest.value = [...dest.options].some(o => o.value === keep) ? keep : dest.options[0].value;
  }
  async function midi() {                                              // Web MIDI, asked for once, on first use
    if (access || !navigator.requestMIDIAccess) return;
    try {
      access = await navigator.requestMIDIAccess();
      access.onstatechange = outputs;
      outputs();
    } catch (e) {
      status.textContent = tr('No access to the browser\'s MIDI: the simulator only.', 'Sem acesso ao MIDI do navegador: só o simulador.');
    }
  }
  function sender() {
    if (dest.value === 'sim') return toSim;
    const o = access && access.outputs.get(dest.value);
    return o ? (st, d1, d2) => o.send([st, d1, d2]) : toSim;
  }
  outputs();
  dest.addEventListener('focus', midi);
  dest.addEventListener('pointerdown', midi);
  document.getElementById('clip-midi').addEventListener('click', midi);
  playBtn.addEventListener('click', () => {
    const c = CLIPS[Number(sel.value)];
    play(c, sender(), Number(chSel.value), loopBox.checked, () => { status.textContent = tr('Done.', 'Fim.'); });
    const to = dest.options[dest.selectedIndex].text;
    status.textContent = tr(`Playing ${c.name} (${c.bpm} BPM) on ${to}, channel ${chSel.value}`,
                            `Tocando ${c.name} (${c.bpm} BPM) em ${to}, canal ${chSel.value}`) +
                         (loopBox.checked ? tr(', looped.', ', em loop.') : '.');
  });
  stopBtn.addEventListener('click', () => { stop(); status.textContent = tr('Stopped.', 'Parado.'); });

  // the console's command: fm1clips.play(0, { to: 'ObsSynthFM1', ch: 1, loop: true })
  window.fm1clips = {
    list: () => CLIPS.map(c => c.name),
    async play(name, { to = 'sim', ch = 1, loop = false } = {}) {
      const c = CLIPS.find(x => x.name.toLowerCase() === String(name).toLowerCase()) ||
                CLIPS[Number(name)] || CLIPS[0];
      let send = toSim;
      if (to !== 'sim') {
        await midi();
        const o = access && [...access.outputs.values()].find(x => x.name.toLowerCase().includes(String(to).toLowerCase()));
        if (!o) return tr(`no MIDI output matching "${to}"`, `nenhuma saída MIDI com "${to}"`);
        send = (st, d1, d2) => o.send([st, d1, d2]);
      }
      play(c, send, ch, loop);
      return tr(`playing ${c.name}`, `tocando ${c.name}`);
    },
    stop,
  };
}
