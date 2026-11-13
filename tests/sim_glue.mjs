// SPDX-License-Identifier: GPL-3.0-only
// ObsSynth FM-1: 2026 Orian Beltrame
// The page's glue on the built wasm (tools/build_sim.sh first): web/sim/engine.js drives build/sim/fm1sim.wasm as
// the worklet does; web/sim/controls.js's ids are panel.c's enums.
//   node tests/sim_glue.mjs
import { readFileSync } from 'node:fs';
import { Fm1Sim } from '../web/sim/engine.js';
import { BTN, ENC, KEYS } from '../web/sim/controls.js';

let fails = 0;
const check = (what, ok) => { console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); fails += ok ? 0 : 1; };
const root = new URL('..', import.meta.url);

const panel = readFileSync(new URL('firmware/src/panel.c', root), 'utf8');
const ids = re => panel.match(re)[1].split(',').map(s => s.trim()).filter(s => s && s !== 'NB' && s !== 'NE');
check('controls.js BTN = panel.c B_ order', JSON.stringify(BTN.map(b => b.c)) === JSON.stringify(ids(/enum \{ (B_FX[^}]*)\}/)));
check('controls.js ENC = panel.c EN_ order', JSON.stringify(ENC.map(e => e.c)) === JSON.stringify(ids(/enum \{ (EN_SELECT[^}]*)\}/)));
check('27 keys, F3 .. G5', KEYS.length === 27 && KEYS[0].name === 'F3' && KEYS[26].name === 'G5');

const sim = await Fm1Sim.create(readFileSync(new URL('build/sim/fm1sim.wasm', root)));
check('a first boot (no saved sectors)', sim.boot(null) === true);
const run = frames => { let peak = 0, ui = 0; for (let n = 0; n < frames; n += 128) { const r = sim.render(128); ui += r.uiFrames; for (const v of r.left) peak = Math.max(peak, Math.abs(v)); } return { peak, ui }; };
let r = run(4410);
check('it runs: UI frames every ~15 ms of audio', r.ui >= 5 && r.ui <= 8);
check('silent at rest', r.peak === 0);
const fb = sim.fb();
check('the screen draws', new Set(fb).size >= 2 && fb.some(c => c !== fb[0]));
sim.key(16, true);
r = run(22050);
check('a key sounds', r.peak > 0.05 && r.peak < 1);
check('its key LED lights', ((sim.keyLeds() >>> 16) & 1) === 1);
check('sounding', sim.playing());
sim.key(16, false);
run(44100);
check('released: silent again', run(4410).peak === 0 && !sim.playing());
check('the flash: nothing written yet (no storage)', sim.takeDirty().length === 0);
const before = sim.fb();
sim.enc(0, 1);
run(4410);
const after = sim.fb();
check('SELECT: the page changes on the screen', after.some((c, i) => c !== before[i]));
sim.btn(13, true);
run(2205);
sim.btn(13, false);
run(2205);
check('OCT+: its LED lights', ((sim.leds() >>> 13) & 1) === 1);
sim.x.sim_midi(0x90, 60, 100);
check('MIDI in (sim_midi): a note sounds', run(4410).peak > 0.05);
sim.x.sim_midi(0x80, 60, 0);
run(44100);
check('.. and its note off ends it', run(4410).peak === 0);
check('the backlight: HOME ENV LFO FX SAVE GLO SEL EDIT SEQ OCT- OCT+ glow, the unused buttons dark, every key',
  sim.glow() === ((1 << 6) | (1 << 2) | (1 << 3) | (1 << 0) | (1 << 7) | (1 << 5) | (1 << 1) | (1 << 4) | (1 << 9) |
                  (1 << 12) | (1 << 13)) &&
  sim.keyGlow() === 0x7FFFFFF);
console.log(fails ? `sim_glue: ${fails} FAILED` : 'sim_glue: all ok');
process.exit(fails ? 1 : 0);
