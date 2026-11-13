// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame
// The ObsSynth FM-1 page: the FM-1 panel drawn flat inside its panel (layout.js: the landscape layout, or the
// stacked portrait one on narrow screens), pointer and keyboard input turned into messages for the worklet
// (worklet.js runs the firmware), the frames it sends back painted, the flash it writes kept in IndexedDB
// (store.js); around it the top bar's preset display and power switch, the clips, the sounds' demos, the palettes.
import { BTN, ENC, KEYS, HeldSet, keyEvent, keyHint, contextMenuLatches } from './controls.js';
import { FlashStore, openFlashDb } from './store.js';
import { needsResume, playbackSession } from './audio.js';
import { tr, LANG } from './i18n.js';
import { LAYOUTS, DECO_CLASS, blend } from './layout.js';
import { initClips } from './clips.js';

// ---- the panel's elements (built once, placed per layout)
const device = document.getElementById('device');
const fit = document.getElementById('fit');
const stage = document.getElementById('stage');
const note = document.getElementById('note');
const powerNote = document.getElementById('power-note');
const els = { deco: {}, ctl: {}, enc: {}, lbl: {}, power: document.getElementById('power-on') };
const lcd = document.createElement('canvas');
lcd.width = lcd.height = 240;
lcd.className = 'lcd';
lcd.setAttribute('aria-label', 'screen');
const lcdCtx = lcd.getContext('2d');
lcdCtx.fillStyle = '#0b0c0a';                   // (ObsSynth's display ground)
lcdCtx.fillRect(0, 0, 240, 240);

function place(el, [x, y, w, h]) {
  el.style.left = `${x}px`; el.style.top = `${y}px`; el.style.width = `${w}px`; el.style.height = `${h}px`;
}

function build() {
  for (const name of Object.keys(DECO_CLASS)) {
    const d = document.createElement('div');
    d.className = DECO_CLASS[name];
    device.append(d);
    els.deco[name] = d;
  }
  device.append(lcd);
  BTN.forEach((b, id) => {
    const el = document.createElement('div');
    el.className = 'btn';
    el.dataset.ctl = `btn:${id}`;
    el.setAttribute('role', 'button');
    el.setAttribute('aria-label', b.label);
    if (b.tip) el.title = `${b.label}: ${b.tip}`;
    el.innerHTML = b.label === 'PLAY' ? '<span class="two">PLAY<br>STOP</span>' : `<span>${b.label}</span>`;
    addHint(el, `btn:${id}`);
    device.append(el);
    els.ctl[`btn:${id}`] = el;
  });
  KEYS.forEach(k => {
    const el = document.createElement('div');
    el.className = `key${k.black ? ' black' : ''}`;
    el.dataset.ctl = `key:${k.n}`;
    el.setAttribute('role', 'button');
    el.setAttribute('aria-label', k.name);
    el.innerHTML = '<span class="stripe"></span>';
    addHint(el, `key:${k.n}`);
    device.append(el);
    els.ctl[`key:${k.n}`] = el;
  });
  [...ENC.map((e, i) => [String(i), e.label, e.tip]), ['master', 'MASTER', tr('the volume', 'o volume')]].forEach(([id, label, tip]) => {
    const el = document.createElement('div');
    el.className = `enc${id === 'master' ? ' master' : ''}`;
    el.tabIndex = 0;
    el.setAttribute('role', 'slider');
    el.setAttribute('aria-label', label);
    if (tip) el.title = `${label}: ${tip}`;
    el.innerHTML = '<span class="skirt"></span><span class="cap"></span>';
    device.append(el);
    els.enc[id] = el;
    const l = document.createElement('div');
    l.className = 'lbl';
    l.textContent = label;
    device.append(l);
    els.lbl[id] = l;
  });
}
function addHint(el, ctl) {
  if (!keyHint[ctl]) return;
  const h = document.createElement('span');
  h.className = 'hint';
  h.textContent = keyHint[ctl];
  el.append(h);
}

// ---- geometry: the landscape FM-1, or the portrait one when its panel is narrow; scaled to the panel's width and
// to what the window leaves under the top bar
let portrait = null;
function relayout() {
  const w = stage.clientWidth;
  if (!w) return;
  const nowPortrait = w < 600;
  const L = LAYOUTS.landscape, G = nowPortrait ? blend(L, LAYOUTS.portrait, 1) : blend(L, L, 0);
  if (nowPortrait !== portrait) {
    portrait = nowPortrait;
    for (const [name, d] of Object.entries(G.deco)) {
      place(els.deco[name], d.box);
      els.deco[name].style.opacity = d.opacity;
    }
    place(lcd, G.lcd);
    for (const [ctl, b] of Object.entries(G.ctl)) place(els.ctl[ctl], b);
    for (const [id, b] of Object.entries(G.enc)) {
      place(els.enc[id], b);
      place(els.lbl[id], [b[0] - 30, b[1] - 20, b[2] + 60, 14]);
    }
    device.style.width = `${G.W}px`;
    device.style.height = `${G.H}px`;
  }
  // the height the first screen leaves it: from its top (in the page, unscrolled) to the window's bottom, less the
  // tools row under it and the panel's padding
  const tools = stage.nextElementSibling?.offsetHeight ?? 40;
  const topInPage = stage.getBoundingClientRect().top + window.scrollY;
  const availH = Math.max(260, window.innerHeight - topInPage - tools - 34);
  const scale = Math.max(0.25, Math.min(w / G.W, nowPortrait ? 2 : availH / G.H, 1.1));
  device.style.setProperty('--scale', scale);
  fit.style.width = `${G.W * scale}px`;
  fit.style.height = `${G.H * scale}px`;
}

// ---- input -> worklet
let node = null;
const held = new HeldSet();
const pending = [];
function send(m) { if (node) node.port.postMessage(m); else pending.push(m); }
function ctlMsg(ctl, down) {
  const [kind, id] = ctl.split(':');
  return { t: kind, id: Number(id), down };
}
function press(ctl, src) {
  if (held.press(ctl, src)) { send(ctlMsg(ctl, true)); els.ctl[ctl]?.classList.add('down'); }
}
function release(ctl, src) {
  if (held.release(ctl, src)) { send(ctlMsg(ctl, false)); els.ctl[ctl]?.classList.remove('down'); }
  if (src === 'latch') els.ctl[ctl]?.classList.remove('latched');
}
function releaseAll() {
  for (const ctl of held.releaseAll()) {
    send(ctlMsg(ctl, false));
    els.ctl[ctl]?.classList.remove('down', 'latched');
  }
  send({ t: 'release' });
}
function toggleLatch(ctl) {
  if (held.holds(ctl, 'latch')) release(ctl, 'latch');
  else { press(ctl, 'latch'); els.ctl[ctl].classList.add('latched'); }
}

const typing = e => e.target.closest?.('select, input, textarea, .confirm');
function bindControls() {
  let lastPointerType;
  device.addEventListener('contextmenu', e => {          // right-click a button: hold it (combinations)
    const el = e.target.closest('[data-ctl]');
    e.preventDefault();
    if (el && el.classList.contains('btn') && contextMenuLatches(lastPointerType)) toggleLatch(el.dataset.ctl);
  });
  device.addEventListener('pointerdown', e => {
    if (poweredOff || !node) { e.preventDefault(); ready(); return; }   // off: the touch switches it on, plays nothing
    const el = e.target.closest('[data-ctl]');
    lastPointerType = e.pointerType;
    if (!el || e.button !== 0) return;
    const ctl = el.dataset.ctl, src = `p${e.pointerId}`;
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    if (held.holds(ctl, 'latch')) { release(ctl, 'latch'); return; }   // a tap lets a held button go
    press(ctl, src);
    if (el.classList.contains('btn') && e.pointerType !== 'mouse') {
      const timer = setTimeout(() => { if (held.holds(ctl, src)) toggleLatch(ctl); }, 600);
      el.addEventListener('pointerup', () => clearTimeout(timer), { once: true });
    }
  });
  const up = e => {
    const el = e.target.closest?.('[data-ctl]');
    if (el) release(el.dataset.ctl, `p${e.pointerId}`);
  };
  device.addEventListener('pointerup', up);
  device.addEventListener('pointercancel', up);
  device.addEventListener('lostpointercapture', up);

  for (const [id, el] of Object.entries(els.enc)) bindEncoder(id, el);

  window.addEventListener('keydown', e => {
    if (typing(e)) return;
    if ((e.code === 'ArrowUp' || e.code === 'ArrowDown') && hovered) {
      e.preventDefault();
      turn(hovered, e.code === 'ArrowUp' ? 1 : -1);
      return;
    }
    const k = keyEvent(e);
    if (k) { e.preventDefault(); press(k.ctl, 'kbd'); }
  });
  window.addEventListener('keyup', e => {
    const k = keyEvent(e);
    if (k) { e.preventDefault(); release(k.ctl, 'kbd'); }
  });
  window.addEventListener('blur', releaseAll);
  document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); else wake(); });
  device.addEventListener('pointerdown', wake, true);
  window.addEventListener('keydown', wake, true);
}

// encoders: 12 px of drag = one detent, up = clockwise; MASTER is a pot (0..1023 over 270 degrees)
let hovered = null, masterValue = 800;
const angle = { };
function turn(id, steps) {
  if (id === 'master') {
    masterValue = Math.max(0, Math.min(1023, masterValue + steps * 32));
    showMaster();
    send({ t: 'master', value: masterValue });
    return;
  }
  spin(id, steps);
  send({ t: 'enc', id: Number(id), steps });
}
function spin(id, steps) {
  angle[id] = (angle[id] ?? 0) + steps * 15;
  els.enc[id].style.setProperty('--turn', `${angle[id]}deg`);
}
function showMaster() { els.enc.master.style.setProperty('--turn', `${-135 + masterValue / 1023 * 270}deg`); }
function bindEncoder(id, el) {
  let acc = 0, lastY = 0;
  el.addEventListener('pointerenter', () => { hovered = id; });
  el.addEventListener('pointerleave', () => { if (hovered === id) hovered = null; });
  el.addEventListener('focus', () => { hovered = id; });
  el.addEventListener('pointerdown', e => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    lastY = e.clientY;
    acc = 0;
  });
  el.addEventListener('pointermove', e => {
    if (!el.hasPointerCapture(e.pointerId)) return;
    const scale = parseFloat(getComputedStyle(device).getPropertyValue('--scale')) || 1;
    acc += (lastY - e.clientY) / scale;
    lastY = e.clientY;
    const px = id === 'master' ? 4 : 12, steps = Math.trunc(acc / px);
    if (steps) { acc -= steps * px; turn(id, id === 'master' ? steps / 8 : steps); }
  });
  let wheel = 0;
  el.addEventListener('wheel', e => {
    e.preventDefault();
    wheel -= e.deltaY;
    const steps = Math.trunc(wheel / 40);
    if (steps) { wheel -= steps * 40; turn(id, steps); }
  }, { passive: false });
}

// ---- frames from the worklet: the screen, the LEDs, the preset's name for the top bar's display
let frame = null;
const img = lcdCtx.createImageData(240, 240);
const px = new Uint32Array(img.data.buffer);
const preName = document.getElementById('pre-name');
function rgb565(fb, out) {                         // the LCD's RGB565 -> canvas RGBA
  for (let i = 0; i < fb.length; i++) {
    const c = fb[i];
    const r = (((c >> 11) & 31) * 527 + 23) >> 6, g = (((c >> 5) & 63) * 259 + 33) >> 6, b = ((c & 31) * 527 + 23) >> 6;
    out[i] = 0xff000000 | (b << 16) | (g << 8) | r;
  }
}
function paint() {
  if (frame) {
    const { fb, leds, keyLeds, glow = 0, keyGlow = 0, pre } = frame;
    frame = null;
    rgb565(fb, px);
    lcdCtx.putImageData(img, 0, 0);
    for (let b = 0; b < 14; b++) {
      els.ctl[`btn:${b}`].classList.toggle('lit', ((leds >>> b) & 1) === 1);
      els.ctl[`btn:${b}`].classList.toggle('glow', ((glow >>> b) & 1) === 1);   // the backlight (ui.c ui_glow_btns)
    }
    for (let n = 0; n < 27; n++) {
      els.ctl[`key:${n}`].classList.toggle('lit', ((keyLeds >>> n) & 1) === 1);
      els.ctl[`key:${n}`].classList.toggle('glow', ((keyGlow >>> n) & 1) === 1);
    }
    if (pre !== undefined && !poweredOff && preName.textContent !== (pre || '—')) {
      preName.textContent = pre || '—';
      preName.classList.remove('off');
    }
  }
  requestAnimationFrame(paint);
}

// ---- flash in IndexedDB (store.js), the audio clock (audio.js)
const store = new FlashStore(openFlashDb, msg => { note.textContent = msg; });
let ctx = null, poweredOff = false;
function wake() {                                 // a tap or a key brings a stopped context back (not one switched off)
  if (!ctx || poweredOff || !needsResume(ctx.state)) return;
  ctx.resume().catch(() => {});
}

// ---- the top bar's preset display: the name from the frames, the arrows turn PRESETS
const prevBtn = document.getElementById('pre-prev'), nextBtn = document.getElementById('pre-next');
prevBtn.addEventListener('click', () => turn('2', -1));
nextBtn.addEventListener('click', () => turn('2', 1));

// ---- power: the top bar's switch; the first flip starts the firmware, later ones pause / resume it
const wasmBytes = fetch('fm1sim.wasm').then(r => {
  if (!r.ok) throw new Error(`fm1sim.wasm: HTTP ${r.status}`);
  return r.arrayBuffer();
});
const instTag = document.getElementById('inst-tag');
function setSwitch(on) {
  if (!on || !node) {                             // the display says what to do while there is no firmware to show
    preName.textContent = node ? tr('OFF', 'DESLIGADO') : tr('switch it on', 'ligue');
    preName.classList.add('off');
  }
  els.power.setAttribute('aria-checked', String(on));
  els.power.classList.toggle('on', on);
  instTag.classList.toggle('on', on);
  device.classList.toggle('powered-off', !on && !!node);
  for (const b of [prevBtn, nextBtn]) b.disabled = !on;
  if (node) powerNote.textContent = on ? tr('On. Play the keys, or a clip.', 'Ligado. Toque as teclas, ou um clip.')
    : tr('Off. Touch the FM-1 to switch it on again.', 'Desligado. Toque no FM-1 para ligar de novo.');
}
async function powerSwitch() {
  if (!node) return powerOn();
  poweredOff = !poweredOff;
  setSwitch(!poweredOff);
  if (poweredOff) { releaseAll(); await ctx.suspend(); }
  else await ctx.resume();
}
let starting = null;
async function ready() {                           // on, for the clips and the sounds (inside their click)
  if (node && !poweredOff) return;
  if (node) return powerSwitch();
  return (starting ??= powerOn());
}
const SOUND_STOPPED = tr('The sound stopped. Touch the panel to bring it back.', 'O som parou. Toque no painel para voltar.');
async function powerOn() {
  const btn = els.power;
  btn.disabled = true;
  btn.classList.remove('ready');
  setSwitch(true);
  powerNote.textContent = tr('Switching on…', 'Ligando…');
  try {
    if (!window.AudioWorkletNode) throw new Error(tr('This browser has no AudioWorklet. Use a current Chrome, Firefox or Safari.', 'Este navegador não tem AudioWorklet. Use um Chrome, Firefox ou Safari atual.'));
    playbackSession(navigator);
    ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
    const resumed = ctx.resume();                         // inside the tap: browsers allow sound from here on
    await ctx.audioWorklet.addModule('worklet.js');
    const [wasm, sectors] = await Promise.all([wasmBytes, store.load()]);
    node = new AudioWorkletNode(ctx, 'fm1', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm, sectors } });
    node.port.onmessage = e => {
      const m = e.data;
      if (m.t === 'frame') frame = m;
      else if (m.t === 'flash') store.put(m.sectors);
      else if (m.t === 'error') note.textContent = tr(`The firmware did not start: ${m.message}`, `O firmware não iniciou: ${m.message}`);
    };
    node.connect(ctx.destination);
    await resumed;
    send({ t: 'master', value: masterValue });
    for (const m of pending.splice(0)) node.port.postMessage(m);
    if (ctx.sampleRate !== 44100) note.textContent = tr(`Audio here runs at ${ctx.sampleRate} Hz, not 44100 Hz: the tuning will be off.`, `O áudio aqui roda a ${ctx.sampleRate} Hz, não a 44100 Hz: a afinação sai errada.`);
    ctx.addEventListener('statechange', () => {
      if (needsResume(ctx.state) && !document.hidden && !poweredOff) note.textContent = SOUND_STOPPED;
      else if (ctx.state === 'running' && note.textContent === SOUND_STOPPED) note.textContent = '';
    });
    powerNote.textContent = tr('On. Play the keys, or a clip.', 'Ligado. Toque as teclas, ou um clip.');
    setSwitch(true);
    btn.disabled = false;
  } catch (err) {
    btn.disabled = false;
    setSwitch(false);
    starting = null;
    powerNote.textContent = err.message || String(err);
  }
}

// ---- the palettes: the ObsSynth panels' (synth repository, builtin_palettes.hpp); OBS is page.css's own
// bg, surface, surface-2, idle, accent, ink, muted, line
const LOOKS = {
  obs: null,
  amber: ['#140d05', '#1c1308', '#27190b', '#0d0802', '#ffb000', '#f5d99a', '#8a6a34', '#4d3414'],
  forest: ['#0c1310', '#141e19', '#1d2b24', '#09100c', '#4fbf8a', '#c3d0c6', '#4d6357', '#2a3d33'],
  grey: ['#1c1c1c', '#242424', '#2d2d2d', '#181818', '#b0b0b0', '#c0c0c0', '#6e6e6e', '#3c3c3c'],
  lilac: ['#120e18', '#1a1522', '#2a2236', '#0d0a12', '#b07cff', '#c8c0d8', '#5a4d6e', '#3a2d4e'],
  ocean: ['#0b111d', '#131b2b', '#1c283e', '#080d17', '#4f9dff', '#c2ccdd', '#4a5a76', '#283a56'],
  oliva: ['#121511', '#1a1e19', '#242a23', '#0d0f0d', '#9cc48a', '#c9d0c8', '#5a6358', '#3a4338'],
  solarized: ['#002b36', '#073642', '#0d4351', '#00232c', '#6c71c4', '#93a1a1', '#586e75', '#18505e'],
  wine: ['#160b10', '#221219', '#321a25', '#10070b', '#d4567f', '#dcc9d0', '#6b4655', '#4a2636'],
};
const LOOK_VARS = ['--bg', '--surface', '--surface-2', '--idle', '--accent', '--ink', '--muted', '--line'];
function setLook(name, buttons) {
  const v = LOOKS[name];
  for (let i = 0; i < LOOK_VARS.length; i++) {
    if (v) document.documentElement.style.setProperty(LOOK_VARS[i], v[i]);
    else document.documentElement.style.removeProperty(LOOK_VARS[i]);
  }
  for (const b of buttons) b.setAttribute('aria-pressed', String(b.dataset.look === name));
  try { localStorage.setItem('obssynth-fm1-look', name); } catch { /* private mode: not kept */ }
}
function bindLooks() {
  const box = document.getElementById('looks');
  const buttons = Object.keys(LOOKS).map(name => {
    const b = document.createElement('button');
    b.type = 'button';
    b.dataset.look = name;
    b.title = name.toUpperCase();
    b.setAttribute('aria-label', name.toUpperCase());
    b.style.background = LOOKS[name]?.[4] ?? '#e8a33d';
    b.addEventListener('click', () => setLook(name, buttons));
    box.append(b);
    return b;
  });
  let saved = 'obs';
  try { saved = localStorage.getItem('obssynth-fm1-look') || 'obs'; } catch { /* none */ }
  setLook(LOOKS[saved] !== undefined ? saved : 'obs', buttons);
}

// ---- page controls
function bindPage() {
  const playNamed = initClips(send, ready);
  for (const b of document.querySelectorAll('.sound .btn-ui')) {   // a sound's demo: its preset, then its clip
    b.addEventListener('click', async () => {
      await ready();
      send({ t: 'preset', idx: Number(b.dataset.preset) });
      playNamed(b.dataset.clip);
      document.getElementById('play').scrollIntoView({ behavior: 'smooth', block: 'start' });
    });
  }
  const keys = document.getElementById('help-toggle');            // the computer keyboard's letters on the panel
  keys.addEventListener('click', () => {
    const on = !device.classList.contains('show-hints');
    device.classList.toggle('show-hints', on);
    keys.setAttribute('aria-expanded', String(on));
  });
  const confirm = document.getElementById('reset-confirm'), reset = document.getElementById('reset');
  reset.addEventListener('click', () => { confirm.hidden = false; reset.hidden = true; });
  document.getElementById('reset-no').addEventListener('click', () => { confirm.hidden = true; reset.hidden = false; });
  document.getElementById('reset-yes').addEventListener('click', async () => {
    await store.clear();
    location.reload();
  });
  for (const b of document.querySelectorAll('.btn-ui, #power-on, .display button'))   // a mouse click leaves no focus
    b.addEventListener('mousedown', e => e.preventDefault());                         // behind: Space plays, not it
  els.power.addEventListener('click', powerSwitch);
  instTag.addEventListener('click', powerSwitch);                 // the FM-1 panel's own switch (in view on phones)
  els.power.disabled = false;
  els.power.classList.add('ready');
  preName.textContent = tr('switch it on', 'ligue');
  preName.classList.add('off');
  powerNote.textContent = tr('Touch the FM-1 to switch it on, or play a clip.', 'Toque no FM-1 para ligar, ou toque um clip.');
}

// ---- what is new: the firmware this site runs (version.json, written by the build)
const MONTHS = LANG === 'pt' ? ['janeiro', 'fevereiro', 'março', 'abril', 'maio', 'junho', 'julho', 'agosto', 'setembro',
  'outubro', 'novembro', 'dezembro'] : ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September',
  'October', 'November', 'December'];
fetch(LANG === 'pt' ? 'version.pt.json' : 'version.json').then(r => r.ok ? r.json() : Promise.reject()).then(v => {
  const [y, m, d] = v.date.split('-').map(Number);
  document.getElementById('fw-version').textContent = tr(`v${v.version} · ${MONTHS[m - 1]} ${d}, ${y}`, `v${v.version} · ${d} de ${MONTHS[m - 1]} de ${y}`);
  const ul = document.getElementById('whats-new');
  for (const n of [...v.since, ...v.release.notes]) {
    const li = document.createElement('li'), b = document.createElement('b');
    b.textContent = n.title;
    let t = n.text;
    if (t.startsWith(n.title)) t = t.slice(n.title.length).replace(/^[\s:,]+/, '');
    li.append(b, ' ', t.charAt(0).toUpperCase() + t.slice(1));
    ul.append(li);
  }
}).catch(() => { document.getElementById('whats-new')?.closest('.panel')?.remove(); });

build();
relayout();
showMaster();
bindControls();
bindPage();
bindLooks();
new ResizeObserver(relayout).observe(stage);
window.addEventListener('resize', relayout);
document.fonts?.ready.then(relayout);
requestAnimationFrame(paint);
