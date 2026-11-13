// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame
// The FM-1 panel's controls as data (no DOM): label ids are panel.c's enum order (tests/sim_glue.mjs checks it),
// the 27 keys, the computer keyboard map, and the held-state bookkeeping the page uses.
// tip: what the control does (the page shows it on hover), as the firmware has it; none: no use yet
import { tr } from './i18n.js';
export const BTN = [
  { c: 'B_FX', label: 'FX', tip: tr('DELAY, REVERB, CHORUS, in turn', 'DELAY, REVERB, CHORUS, em roda') }, { c: 'B_SCL', label: 'SEL', tip: tr('OSC 1 and OSC 2, alternating', 'OSC 1 e OSC 2, alternando') }, { c: 'B_ENV', label: 'ENV', tip: tr('AMP ENV and FILTER ENV, alternating', 'AMP ENV e FILTER ENV, alternando') }, { c: 'B_LFO', label: 'LFO', tip: tr('LFO 1 and LFO 2, alternating', 'LFO 1 e LFO 2, alternando') },
  { c: 'B_EDIT', label: 'EDIT', tip: tr('AMP (VOL, VEL, MODE, VOICE) and AMP ENV, alternating', 'AMP (VOL, VEL, MODE, VOICE) e AMP ENV, alternando') }, { c: 'B_GLO', label: 'GLO', tip: tr('GLOBAL (MIDI channel, tuning, velocity, lights), GLOBAL 2 (the screen\'s palette); with HOME: INFO', 'GLOBAL (canal MIDI, afinação, velocity, luz), GLOBAL 2 (a paleta da tela); com HOME: INFO') }, { c: 'B_HOME', label: 'HOME', tip: tr('SHIFT, on and off: the module\'s + page (more parameters), until HOME again or another page', 'SHIFT, liga e desliga: a página + do módulo (mais parâmetros), até HOME de novo ou outra página') },
  { c: 'B_SAVE', label: 'SAVE', tip: tr('the SAVE page: KNOB1 the slot, KNOB2 the position, KNOB3 the name\'s letter; SAVE again writes', 'a página SAVE: KNOB1 o slot, KNOB2 a posição, KNOB3 a letra do nome; SAVE de novo grava') }, { c: 'B_ARP', label: 'ARP' }, { c: 'B_SEQ', label: 'SEQ', tip: tr('FILTER and FILTER ENV, alternating', 'FILTER e FILTER ENV, alternando') },
  { c: 'B_PLAY', label: 'PLAY' }, { c: 'B_REC', label: 'REC' },
  { c: 'B_OCTDN', label: 'OCT-', tip: tr('the keyboard an octave down (to -2)', 'o teclado uma oitava abaixo (até -2)') },
  { c: 'B_OCTUP', label: 'OCT+', tip: tr('the keyboard an octave up (to +2)', 'o teclado uma oitava acima (até +2)') },
];
export const B = Object.fromEntries(BTN.map((b, i) => [b.label, i]));

export const ENC = [
  { c: 'EN_SELECT', label: 'SELECT', tip: tr('changes the page: OSC 1, OSC 2, AMP, AMP ENV, FILTER, FILTER ENV; on an LFO or FX page, back to them', 'troca a página: OSC 1, OSC 2, AMP, AMP ENV, FILTER, FILTER ENV; numa página de LFO ou FX, volta para elas') },
  { c: 'EN_ALGO', label: 'ALGORITHM', tip: tr('the engine of the oscillator on screen: STD, SUPER (7 saws), WT (wavetable), GRAIN (granular)', 'o motor do oscilador na tela: STD, SUPER (7 serras), WT (wavetable), GRAIN (granular)') }, { c: 'EN_PRESET', label: 'PRESETS', tip: tr('the next / previous preset, loaded at once', 'o próximo / anterior preset, carregado na hora') },
  { c: 'EN_K1', label: 'KNOB1', tip: tr('the page\'s first value', 'o primeiro valor da página') }, { c: 'EN_K2', label: 'KNOB2', tip: tr('the page\'s second value', 'o segundo valor da página') },
  { c: 'EN_K3', label: 'KNOB3', tip: tr('the page\'s third value', 'o terceiro valor da página') }, { c: 'EN_K4', label: 'KNOB4', tip: tr('the page\'s fourth value', 'o quarto valor da página') },
];

const NAMES = ['F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E'];
export const KEYS = Array.from({ length: 27 }, (_, n) => {
  const name = NAMES[n % 12], octave = 3 + Math.floor((n + 5) / 12);   // F3 .. G5 (C starts an octave)
  return { n, name: name + octave, black: name.includes('#') };
});

// KeyboardEvent.code (the physical key, any layout) -> control. Two piano rows: Z.. / S.. from F3, Q.. / 3.. to G5.
const PIANO = {
  KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyF: 5, KeyV: 6, KeyB: 7, KeyH: 8, KeyN: 9, KeyJ: 10, KeyM: 11,
  Comma: 12, KeyL: 13, Period: 14, Semicolon: 15, Slash: 16, Quote: 17,
  KeyQ: 18, KeyW: 19, Digit3: 20, KeyE: 21, Digit4: 22, KeyR: 23, KeyT: 24, Digit6: 25, KeyY: 26,
};
export const KEYBOARD = {
  ...Object.fromEntries(Object.entries(PIANO).map(([code, n]) => [code, { kind: 'key', id: n }])),
  Space: { kind: 'btn', id: B.PLAY }, Enter: { kind: 'btn', id: B.REC }, Escape: { kind: 'btn', id: B.HOME },
  ArrowLeft: { kind: 'btn', id: B['OCT-'] }, ArrowRight: { kind: 'btn', id: B['OCT+'] },
};
export const keyHint = Object.fromEntries(Object.entries(KEYBOARD).map(([code, c]) => [`${c.kind}:${c.id}`,
  code.replace(/^Key|^Digit/, '').replace('Comma', ',').replace('Period', '.').replace('Slash', '/')
    .replace('Semicolon', ';').replace('Quote', "'").replace('Space', 'Space').replace('Escape', 'Esc')
    .replace('ArrowLeft', '←').replace('ArrowRight', '→')]));

// a keyboard event -> { ctl: 'key:n' | 'btn:id', down } or null (unmapped, autorepeat, a shortcut with a modifier)
export function keyEvent(e) {
  const c = KEYBOARD[e.code];
  const down = e.type === 'keydown';
  if (!c || e.repeat || (down && (e.metaKey || e.ctrlKey || e.altKey))) return null;
  return { ctl: `${c.kind}:${c.id}`, down };
}

// does a contextmenu event latch a button? A right-click does; a touch / pen long-press fires contextmenu too (Android
// Chrome, Windows) and the long-press timer latches it, so that one must not (it would toggle the latch back off)
export function contextMenuLatches(lastPointerType) { return lastPointerType !== 'touch' && lastPointerType !== 'pen'; }

// a control is held while any source (a pointer, the keyboard, a latch) holds it
export class HeldSet {
  constructor() { this.m = new Map(); }
  press(ctl, src) {                                // -> true when the control just went down
    const s = this.m.get(ctl) ?? new Set();
    const first = s.size === 0;
    s.add(src);
    this.m.set(ctl, s);
    return first;
  }
  release(ctl, src) {                              // -> true when the control just went up
    const s = this.m.get(ctl);
    if (!s || !s.delete(src)) return false;
    if (s.size) return false;
    this.m.delete(ctl);
    return true;
  }
  holds(ctl, src) { return this.m.get(ctl)?.has(src) ?? false; }
  releaseAll() {                                   // -> the controls that were held
    const all = [...this.m.keys()];
    this.m.clear();
    return all;
  }
}
