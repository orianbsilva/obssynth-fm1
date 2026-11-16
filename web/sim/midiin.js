// SPDX-License-Identifier: GPL-3.0-only
// ObsSynth FM-1: 2026 Orian Beltrame
// MIDI in: a keyboard (any input the browser's MIDI gives: USB, Bluetooth, an app) plays the simulator, as an FM-1
// over USB does it (notes, the wheel, the controllers; SysEx is not taken). Asked for with the power switch (a
// click: the browser may ask the person once) or the clips' MIDI ports button.
const VOICE = { 0x80: 3, 0x90: 3, 0xA0: 3, 0xB0: 3, 0xC0: 2, 0xD0: 2, 0xE0: 3 };

// send({t: 'midi', st, d1, d2}) into the simulator while isOn(); show(text) says which inputs are heard
export function initMidiIn(send, isOn, show) {
  let access = null, asking = null;
  function hook() {
    const names = [];
    for (const input of access.inputs.values()) {
      input.onmidimessage = (e) => {
        const d = e.data;
        if (!isOn()) return;
        for (let i = 0; i < d.length;) {                              // (one event may carry several messages)
          const st = d[i], n = VOICE[st & 0xF0];
          if (!n) { i++; continue; }
          send({ t: 'midi', st, d1: d[i + 1] ?? 0, d2: n === 3 ? d[i + 2] ?? 0 : 0 });
          i += n;
        }
      };
      if (input.state !== 'disconnected') names.push(input.name);
    }
    show(names);
  }
  return async function ask() {
    if (access || !navigator.requestMIDIAccess) return;
    return (asking ??= navigator.requestMIDIAccess().then((a) => {
      access = a;
      a.addEventListener('statechange', hook);
      hook();
    }).catch(() => { asking = null; }));
  };
}
