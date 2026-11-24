// The sound-designer page, as served by the WiFi server (transfer.cpp). What
// it does and how it talks to the program: designer.hpp.
//
// Nothing about any synth is written here. The page asks the program what
// the synth on the track has (its pages, settings, ranges and the names of
// their values) and builds itself from the answer, so a new synth or a new
// setting shows up without touching this file. The spec's "page" says what
// kind of track it is: "synth", "sampler" (its settings, and a picture of
// its recording with START and END on it to drag: `wave()` below) or "kit"
// (eight lanes, each a card with its sound, its picture by `wave()`, its
// settings and EDIT). Every page ends with EFFECTS (the track's slots, its
// sends, the buses, the master's slots, the mixer's LEVEL and PAN) and MODS
// (the two MODs with their rows of steps): `buildFx()` and `buildMods()`.

namespace apotheca {

extern const char* const kDesignPage;
const char* const kDesignPage = R"page(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca sound designer</title>
<style>
:root{--bg:#14110e;--panel:#1f1a16;--panel2:#2a231d;--line:#43392f;--text:#e0d5bf;--dim:#8f8370;--teal:#6fbfa3;--purple:#9a76b0;--brass:#c9a15a}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--text);font:14px/1.4 system-ui,sans-serif;margin:0;padding:20px 24px 150px}
h1{font-size:18px;letter-spacing:.14em;margin:0} h2{font-size:12px;letter-spacing:.14em;color:var(--dim);margin:0 0 10px;font-weight:600}
a{color:var(--teal)} .dim{color:var(--dim)}
header{display:flex;flex-wrap:wrap;gap:12px 20px;align-items:center;margin-bottom:18px}
header .grow{flex:1}
select,input[type=text],button{font:inherit;background:var(--panel2);color:var(--text);border:1px solid var(--line);padding:6px 8px;border-radius:0}
button{background:var(--teal);color:var(--bg);border:0;padding:7px 14px;cursor:pointer;font-weight:600;letter-spacing:.06em}
button.quiet{background:var(--panel2);color:var(--text);border:1px solid var(--line)}
section{background:var(--panel);border:1px solid var(--line);padding:14px 16px;margin:0 0 14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:12px 22px}
.ctl label{display:flex;justify-content:space-between;font-size:11px;letter-spacing:.1em;color:var(--dim);margin-bottom:2px}
.ctl label b{color:var(--text);font-weight:600;letter-spacing:.04em}
.ctl select{width:100%}
input[type=range]{width:100%;accent-color:var(--teal);margin:2px 0}
.env{display:grid;grid-template-columns:54px repeat(8,1fr);gap:4px 8px;align-items:center;margin-top:10px}
.env .h{font-size:11px;color:var(--dim);letter-spacing:.1em;text-align:center}
.env .r{font-size:11px;color:var(--dim);letter-spacing:.1em}
.env .cell{text-align:center;font-size:11px}
.envtop{display:flex;flex-wrap:wrap;gap:16px;align-items:flex-end}
.envtop svg{flex:1;min-width:260px;height:90px;background:var(--bg);border:1px solid var(--line)}
.envtop .ctl{width:150px}
#keys{position:fixed;left:0;right:0;bottom:0;background:var(--panel);border-top:1px solid var(--line);padding:10px 24px 12px}
#keys .row{display:flex;gap:14px;align-items:center;margin-bottom:8px;font-size:12px;color:var(--dim)}
#board{position:relative;height:64px;user-select:none;touch-action:none}
#board div{position:absolute;top:0;border:1px solid var(--bg)}
#board .w{background:var(--text);height:64px} #board .b{background:#000;height:38px;z-index:1;border-color:var(--line)}
#board .on{background:var(--teal)!important}
.steps{display:grid;grid-template-columns:repeat(8,1fr);gap:6px 12px;margin-top:4px}
.steps .ctl label{font-size:10px}
.fxkind{display:flex;gap:8px;align-items:center;margin-bottom:10px}
.fxkind select{flex:1;min-width:0}
#said{font-size:12px;min-height:18px} .ok{color:var(--teal)} .no{color:#d98a6a}
#nothing{padding:40px 0;color:var(--dim);font-size:16px}
.wavetop{display:flex;flex-wrap:wrap;gap:10px 16px;align-items:center;margin-bottom:10px}
.wavetop select{min-width:220px;max-width:100%}
.wave canvas{display:block;width:100%;height:150px;background:var(--bg);border:1px solid var(--line);touch-action:none;cursor:ew-resize}
.wave .note{font-size:11px;color:var(--dim);margin-top:4px}
.tables{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,320px),1fr));gap:14px}
.tables .wave canvas{height:170px}
body.kit{padding-bottom:24px}
#kithelp{font-size:12px;color:var(--dim);margin:2px 0 12px}
.lanes{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,480px),1fr));gap:14px}
.lane{margin:0;min-width:0}
.lane .top{display:flex;flex-wrap:wrap;gap:8px 10px;align-items:center;margin-bottom:8px}
.lane .num{font-weight:700;font-size:16px;color:var(--brass);min-width:16px}
.lane .top select{flex:1;min-width:0;max-width:100%}
.lane .wave canvas{height:96px}
.lane h3{font-size:11px;letter-spacing:.14em;color:var(--dim);margin:12px 0 6px;font-weight:600}
.lane .grid{grid-template-columns:repeat(auto-fill,minmax(128px,1fr));gap:8px 16px}
.lane .grid.five{grid-template-columns:repeat(auto-fill,minmax(96px,1fr))}
.lane .acts{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
button.on{background:var(--brass)}
@media (max-width:600px){body{padding:14px 16px 150px} body.kit{padding-bottom:24px} #keys{padding:10px 16px 12px} section{padding:12px}}
</style></head><body>
<header>
  <h1>SOUND DESIGNER</h1><span id="what" class="dim"></span>
  <span class="grow"></span>
  <label class="dim">TRACK NAME <input id="called" type="text" maxlength="10" size="11" placeholder="ITS INSTRUMENT"></label>
  <label class="dim" id="presetbox">PRESET <select id="preset"></select></label>
  <input id="name" type="text" maxlength="24" placeholder="NAME FOR A NEW PRESET" size="24">
  <button id="roll" class="quiet" hidden>ROLL A NEW SOUND</button>
  <button id="save">SAVE AS PRESET</button>
  <a id="files" href="#">files</a>
</header>
<div id="said" class="dim">The handheld makes the sound. Turn it up, or plug headphones into it.</div>
<div id="nothing" hidden>The track being worked on has no instrument. On the handheld, go to the SONG screen and move to a PHAZED, JAX-EP, FM-6, PHYSIC, ACID, CLOUD, MORPH, SAMPLER or DRUMS track (or add one with + TRACK).</div>
<div id="all"></div>
<div id="keys">
  <div class="row"><span>PLAY: click the keys, or use the computer keyboard: A W S E D F T G Y H U J K. Z and X: octave down / up.</span><span id="oct"></span></div>
  <div id="board"></div>
</div>
<script>
const base = location.pathname.replace(/\/design\/?$/, '');
document.getElementById('files').href = base + '/';
const $ = id => document.getElementById(id);
let spec = null, specSerial = -1, state = null, controls = {}, macroControls = [], touched = {}, envs = [], waves = [];

// ---- talking to the handheld: one request at a time, newest value wins ----
const waiting = new Map(); let pumping = false, serial = 0;
function tell(key, line) { waiting.set(key, line); pump(); }
async function pump() {
  if (pumping) return; pumping = true;
  while (waiting.size) {
    const [key, line] = waiting.entries().next().value; waiting.delete(key);
    try { const a = await fetch(base + '/design/do', {method: 'POST', body: line}); if (!a.ok) say('The handheld refused: ' + line, 'no'); }
    catch (e) { say('The handheld is not answering. Is the WiFi page still on?', 'no'); }
  }
  pumping = false;
}
function say(text, kind) { const s = $('said'); s.textContent = text; s.className = kind || 'dim'; }
async function get(path) { return (await fetch(base + path, {cache: 'no-store'})).json(); }

// ---- building the page from what the synth has ----
function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
// A setting with "stops" takes only those values (a kit's FADE, FIT, HUMAN TIME and LEVEL): its place
// among them is the last at or under the value, as on the handheld.
function stopPlace(item, v) { let k = 0; item.stops.forEach((s, i) => { if (s <= v) k = i; }); return k; }
function shown(item, v) { return item.texts ? item.texts[item.stops ? stopPlace(item, v) : v - item.min] : String(v); }
// A setting's control. A "flip" setting's slider runs the other way (the sampler's PITCH is kept as
// its root key: right is higher). An item with a "reading" says itself what a value reads.
function control(item, label) {
  const box = el('div', 'ctl'), lab = el('label'), val = el('b');
  lab.append(el('span', '', label || item.name), val); box.append(lab);
  const count = item.stops ? item.stops.length : item.max - item.min + 1;
  // the input's place <-> the setting's value
  const toValue = p => item.stops ? item.stops[p] : item.flip ? item.min + item.max - p : p;
  const toPlace = v => item.stops ? stopPlace(item, v) : item.flip ? item.min + item.max - v : v;
  let input;
  if (item.texts && count <= 9) {
    input = el('select');
    item.texts.forEach((t, i) => { const o = el('option', '', t); o.value = item.stops ? i : item.min + i; input.append(o); });
  } else {
    input = el('input'); input.type = 'range'; input.min = item.stops ? 0 : item.min; input.max = item.stops ? count - 1 : item.max; input.step = 1;
  }
  let now = null; // the value shown (a value between two stops is shown as it is)
  const show = () => { if (now === null) now = toValue(+input.value); val.textContent = input.tagName === 'SELECT' ? '' : item.reading ? item.reading(now) : shown(item, now); };
  const put = v => { input.value = toPlace(v); now = v; show(); };
  input.addEventListener('input', () => { now = toValue(+input.value); setValue(item.id, now); });
  // Once a choice is made the dropdown lets go of the keyboard, so the next
  // key pressed is a note again (and not a letter typed into the dropdown).
  if (input.tagName === 'SELECT') input.addEventListener('change', () => input.blur());
  box.append(input);
  controls[item.id] = {input, show, put};
  return box;
}
// A setting changed on the page: told to the handheld, and every control of it shown so at once.
function setValue(id, v) {
  touched[id] = Date.now();
  if (state) state.values[id] = v;
  if (controls[id] && controls[id].input !== document.activeElement) controls[id].put(v); else if (controls[id]) controls[id].show();
  tell('set' + id, 'set ' + id + ' ' + v);
  drawEnvs(); drawWaves();
}

// ---- a recording as a picture, its marks dragged ----
// w: {peaks (0..1000), seconds, recording, missing, marks: [id, id] (settings at 0..units of the way
// along, in order; units 100 unless it says), least: how near two marks may come}. The marks are the
// settings themselves, so a drag is the same request a slider makes. A kit's lane adds what only it
// has: label(id) (a mark's words), level(at) (how loud the edits make that point: the part played is
// drawn so), noteText() and emptyText.
function itemOf(id) {
  if (spec.page === 'kit') { const f = spec.fields.find(f => f.field === id % spec.stride); return f ? Object.assign({}, f, {id}) : {name: '', id}; }
  return spec.pages.flatMap(p => p.items).find(i => i.id === id) || {name: '', id};
}
function wave(w) {
  const box = el('div', 'wave'), cv = el('canvas'), note = el('div', 'note');
  box.append(cv, note);
  const item = itemOf, units = w.units || 100;
  const at = id => state && state.values ? +state.values[id] : 0;
  let drag = -1;
  function place(e) { const r = cv.getBoundingClientRect(); return Math.max(0, Math.min(units, Math.round((e.clientX - r.left) / Math.max(1, r.width) * units))); }
  // Each mark stops `least` short of its neighbours, as on the handheld.
  function held(k, v) {
    if (k > 0) v = Math.max(v, at(w.marks[k - 1]) + w.least);
    if (k < w.marks.length - 1) v = Math.min(v, at(w.marks[k + 1]) - w.least);
    const it = item(w.marks[k]); return Math.max(it.min ?? 0, Math.min(it.max ?? units, v));
  }
  function move(e) {
    if (w.band) { const v = place(e); if (v !== at(w.band[0])) setValue(w.band[0], v); return; } // CLOUD: POSITION goes where it is put
    const v = held(drag, place(e)); if (v !== at(w.marks[drag])) setValue(w.marks[drag], v);
  }
  cv.addEventListener('pointerdown', e => {
    if (!state || !w.peaks.length) return;
    if (w.band) { drag = 0; cv.setPointerCapture(e.pointerId); move(e); e.preventDefault(); return; }
    const v = place(e); let best = 0;
    // The nearest mark; where two stand together, the one on the side the pointer is.
    w.marks.forEach((id, k) => { const d = Math.abs(at(id) - v), b = Math.abs(at(w.marks[best]) - v); if (d < b || (d === b && v > at(id))) best = k; });
    drag = best; cv.setPointerCapture(e.pointerId); move(e); e.preventDefault();
  });
  cv.addEventListener('pointermove', e => { if (drag >= 0) move(e); });
  for (const up of ['pointerup', 'pointercancel']) cv.addEventListener(up, () => { drag = -1; });
  function draw() {
    const css = getComputedStyle(document.documentElement), c = n => css.getPropertyValue(n).trim();
    const dpr = window.devicePixelRatio || 1, W = Math.max(1, Math.round(cv.clientWidth * dpr)), H = Math.max(1, Math.round(cv.clientHeight * dpr));
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    const g = cv.getContext('2d'); g.clearRect(0, 0, W, H);
    if (!w.peaks.length) {
      g.fillStyle = c('--dim'); g.font = (14 * dpr) + 'px system-ui,sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
      g.fillText(w.emptyText || (w.missing ? 'MISSING FROM THE CARD: ' + w.recording : 'NO RECORDING: PICK ONE ABOVE'), W / 2, H / 2);
      note.textContent = ''; return;
    }
    if (w.band) return drawBand(g, c, dpr, W, H);
    const x = id => at(id) / units * W, lo = x(w.marks[0]), hi = x(w.marks[w.marks.length - 1]), mid = H / 2, n = w.peaks.length;
    for (let px = 0; px < W; ++px) {
      const i = Math.min(n - 1, Math.floor(px / W * n)), played = px >= lo && px <= hi;
      let p = w.peaks[i] / 1000;
      if (played && w.level) p = Math.abs(guarded(p * w.level(i / n)));
      const h = Math.max(1, p * (mid - 6 * dpr));
      g.fillStyle = played ? c('--teal') : c('--line');
      g.fillRect(px, mid - h, 1, 2 * h);
    }
    g.font = (11 * dpr) + 'px system-ui,sans-serif'; g.textBaseline = 'top';
    w.marks.forEach((id, k) => {
      const mx = Math.min(W - 2 * dpr, Math.max(0, x(id) - dpr)), last = k === w.marks.length - 1;
      g.fillStyle = c('--brass'); g.fillRect(mx, 0, 2 * dpr, H);
      g.fillRect(last ? mx - 8 * dpr : mx, 0, 10 * dpr, 10 * dpr); // its handle, on the side of what is played
      const label = w.label ? w.label(id) : item(id).name + ' ' + at(id) + '%';
      g.textAlign = last ? 'right' : 'left';
      const lx = last ? mx - 4 * dpr : mx + 6 * dpr, lw = g.measureText(label).width; // (on a patch of the background, to read over the picture)
      g.fillStyle = c('--bg'); g.fillRect(last ? lx - lw - 3 * dpr : lx - 3 * dpr, H - 18 * dpr, lw + 6 * dpr, 15 * dpr);
      g.fillStyle = c('--brass'); g.fillText(label, lx, H - 16 * dpr);
    });
    const s = w.seconds;
    if (w.noteText) { note.textContent = w.noteText(); return; }
    note.textContent = w.recording + ' · ' + s.toFixed(2) + ' S · a note plays ' + (s * (at(w.marks[w.marks.length - 1]) - at(w.marks[0])) / 100).toFixed(2) + ' S of it (at its own pitch). Drag a mark, or click to move the nearer one.';
  }
  // CLOUD's picture: the recording (the TONE's, dim), lit within SPRAY of POSITION and round the ends
  // (cloud_rule.hpp's sprayShare: half the recording either way at 100, squared), POSITION a brass mark.
  function drawBand(g, c, dpr, W, H) {
    const pos = at(w.band[0]) / 100, x = Math.min(1, Math.max(0, at(w.band[1]) / 100)), spray = 0.5 * x * x, mid = H / 2, n = w.peaks.length;
    for (let px = 0; px < W; ++px) {
      const i = Math.min(n - 1, Math.floor(px / W * n));
      let off = (px + 0.5) / W - pos;
      if (off > 0.5) off -= 1; if (off < -0.5) off += 1;
      const lit = Math.abs(off) <= spray + 0.5 / W, h = Math.max(1, w.peaks[i] / 1000 * (mid - 6 * dpr));
      g.fillStyle = lit ? (w.tone ? c('--dim') : c('--teal')) : c('--line');
      g.fillRect(px, mid - h, 1, 2 * h);
    }
    const mx = Math.min(W - 2 * dpr, Math.max(0, pos * W - dpr));
    g.fillStyle = c('--brass'); g.fillRect(mx, 0, 2 * dpr, H); g.fillRect(mx - 4 * dpr, 0, 10 * dpr, 10 * dpr);
    g.font = (11 * dpr) + 'px system-ui,sans-serif'; g.textBaseline = 'top';
    const label = 'POSITION ' + at(w.band[0]) + '%  SPRAY ' + (spray * 100).toFixed(spray < 0.0995 ? 1 : 0) + '%', right = pos > 0.6;
    g.textAlign = right ? 'right' : 'left';
    const lx = right ? mx - 6 * dpr : mx + 8 * dpr, lw = g.measureText(label).width;
    g.fillStyle = c('--bg'); g.fillRect(right ? lx - lw - 3 * dpr : lx - 3 * dpr, H - 18 * dpr, lw + 6 * dpr, 15 * dpr);
    g.fillStyle = c('--brass'); g.fillText(label, lx, H - 16 * dpr);
    note.textContent = (w.tone ? (w.missing ? w.recording + ' is missing from the card: the built-in TONE plays' : 'No recording: the built-in TONE plays') : w.recording) + ' · ' +
      w.seconds.toFixed(2) + ' S · the grains come from the lit part, the brass mark is POSITION. Click or drag to move it.';
  }
  waves.push({draw});
  return box;
}
// MORPH's tables (spec.tables: every frame of each at `points` points, -100..100; spec.oscs: which
// settings pick and place A's and B's): the table's frames stacked faint, back to front, and the frame
// POSITION reads drawn at its place in the stack, crossfaded between the two it falls between as the
// voice does (teal A, purple B). A click or a drag sets POSITION. The macros are left out, as CLOUD's
// band leaves them: the picture is the patch.
function tableWave(o) {
  const box = el('div', 'wave'), cv = el('canvas'), note = el('div', 'note');
  box.append(cv, note);
  const at = id => state && state.values ? +state.values[id] : 0;
  const table = () => { const v = at(o.table); return o.off ? v - 1 : v; };
  let dragging = false;
  function move(e) {
    const r = cv.getBoundingClientRect(), v = Math.max(0, Math.min(100, Math.round((e.clientX - r.left) / Math.max(1, r.width) * 100)));
    if (v !== at(o.position)) setValue(o.position, v);
  }
  cv.addEventListener('pointerdown', e => { if (!state || table() < 0) return; dragging = true; cv.setPointerCapture(e.pointerId); move(e); e.preventDefault(); });
  cv.addEventListener('pointermove', e => { if (dragging) move(e); });
  for (const up of ['pointerup', 'pointercancel']) cv.addEventListener(up, () => { dragging = false; });
  function draw() {
    const css = getComputedStyle(document.documentElement), c = n => css.getPropertyValue(n).trim();
    const dpr = window.devicePixelRatio || 1, W = Math.max(1, Math.round(cv.clientWidth * dpr)), H = Math.max(1, Math.round(cv.clientHeight * dpr));
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    const g = cv.getContext('2d'); g.clearRect(0, 0, W, H);
    const t = table(), T = spec.tables;
    if (t < 0 || t >= T.names.length) {
      g.fillStyle = c('--dim'); g.font = (14 * dpr) + 'px system-ui,sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
      g.fillText(o.name + ' IS OFF: TABLE ' + o.name + ' TURNS IT ON', W / 2, H / 2);
      note.textContent = ''; return;
    }
    const F = T.frames[t], P = T.points, data = T.waves[t], pos = at(o.position) / 100 * (F - 1);
    // The stack: frame 1 at the front (bottom left), the last at the back (top right).
    const dx = W * 0.22, dy = H * 0.34, w = W - dx - 8 * dpr, amp = (H - dy) * 0.36, x0 = 4 * dpr, y0 = H - (H - dy) / 2;
    const line = (f0, f1, k) => { // a frame, crossfaded between f0 and f1 by k, at its place in the stack
      const depth = (f0 + (f1 - f0) * k) / Math.max(1, F - 1), ox = x0 + depth * dx, oy = y0 - depth * dy;
      g.beginPath();
      for (let i = 0; i <= P; ++i) {
        const j = i % P, v = (data[f0 * P + j] * (1 - k) + data[f1 * P + j] * k) / 100;
        const x = ox + i / P * w, y = oy - v * amp;
        if (i) g.lineTo(x, y); else g.moveTo(x, y);
      }
      g.stroke();
    };
    g.lineWidth = dpr; g.strokeStyle = c('--line');
    for (let f = F - 1; f >= 0; --f) line(f, f, 0);
    const f0 = Math.min(F - 2, Math.floor(pos)), k = pos - f0;
    g.lineWidth = 2.5 * dpr; g.strokeStyle = c(o.name === 'A' ? '--teal' : '--purple');
    line(f0, f0 + 1, k);
    // POSITION along the bottom: a brass mark, and the FRAME it reads.
    const mx = Math.min(W - 2 * dpr, Math.max(0, at(o.position) / 100 * W - dpr));
    g.fillStyle = c('--brass'); g.fillRect(mx, H - 8 * dpr, 2 * dpr, 8 * dpr);
    g.font = (11 * dpr) + 'px system-ui,sans-serif'; g.textBaseline = 'top'; g.textAlign = 'left';
    g.fillStyle = c('--text'); g.fillText(o.name + '  ' + T.names[t], 6 * dpr, 6 * dpr);
    g.fillStyle = c('--brass'); g.textAlign = 'right'; g.fillText('FRAME ' + (pos + 1).toFixed(2) + ' OF ' + F, W - 6 * dpr, 6 * dpr);
    note.textContent = 'POSITION ' + o.name + ' ' + at(o.position) + '%: the bright wave is the frame it reads. Click or drag to move it.';
  }
  waves.push({draw});
  return box;
}
function drawWaves() { for (const w of waves) w.draw(); }
window.addEventListener('resize', drawWaves);
function build() {
  const all = $('all'); all.textContent = ''; controls = {}; macroControls = []; envs = []; waves = []; fxControls = {}; fxKinds = {}; modCards = [];
  const none = !spec || !spec.name, sampler = !none && spec.page === 'sampler', kit = !none && spec.page === 'kit';
  $('nothing').hidden = !none; $('keys').hidden = none || kit; $('roll').hidden = none || !spec.roll;
  document.body.classList.toggle('kit', kit);
  // A sampler keeps no presets (its sound is its recording), nor does a kit.
  for (const id of ['presetbox', 'name', 'save']) $(id).hidden = none || sampler || kit;
  if (none) return;
  if (kit) { buildKit(all); buildFx(all); buildMods(all); return; }
  if (sampler || spec.waves) for (const w of spec.waves) all.append(recordingSection(w)); // (a CLOUD's too)
  if (spec.tables) { // MORPH: A's and B's tables, side by side
    const s = el('section'), row = el('div', 'tables'); s.append(el('h2', '', 'TABLES'));
    for (const o of spec.oscs) row.append(tableWave(o));
    s.append(row); all.append(s);
  }
  // presets, in their folders
  const sel = $('preset'); sel.textContent = ''; let group = null, folder = null;
  spec.presets.forEach((p, i) => {
    if (p.folder !== folder) { folder = p.folder; group = el('optgroup'); group.label = folder; sel.append(group); }
    const o = el('option', '', p.name); o.value = i; group.append(o);
  });
  // macros
  if (spec.macros.length) {
  const ms = el('section'); ms.append(el('h2', '', 'MACROS (the eight on the handheld\'s main page)'));
  const mg = el('div', 'grid');
  spec.macros.forEach((name, m) => {
    const box = el('div', 'ctl'), lab = el('label'), val = el('b'), input = el('input');
    input.type = 'range'; input.min = 0; input.max = 200; input.step = 1;
    const show = () => { const v = input.value - 100; val.textContent = (v > 0 ? '+' : '') + v; };
    input.addEventListener('input', () => { touched['m' + m] = Date.now(); show(); tell('macro' + m, 'macro ' + m + ' ' + (input.value / 200)); });
    input.addEventListener('dblclick', () => { input.value = 100; input.dispatchEvent(new Event('input')); });
    lab.append(el('span', '', name), val); box.append(lab, input); mg.append(box);
    macroControls.push({input, show});
  });
  ms.append(mg); all.append(ms);
  }
  // the pages
  for (const page of spec.pages) {
    const s = el('section'); s.append(el('h2', '', page.name));
    if (page.env && page.items.length >= 18) {
      const top = el('div', 'envtop'), svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      svg.setAttribute('viewBox', '0 0 400 100'); svg.setAttribute('preserveAspectRatio', 'none');
      top.append(svg, control(page.items[16]), control(page.items[17])); s.append(top);
      const g = el('div', 'env'); g.append(el('div'));
      for (let i = 1; i <= 8; ++i) g.append(el('div', 'h', 'STEP ' + i));
      g.append(el('div', 'r', 'RATE')); for (let i = 0; i < 8; ++i) g.append(control(page.items[i], ' '));
      g.append(el('div', 'r', 'LEVEL')); for (let i = 8; i < 16; ++i) g.append(control(page.items[i], ' '));
      s.append(g); envs.push({svg, items: page.items});
    } else {
      const g = el('div', 'grid'); for (const item of page.items) g.append(control(item)); s.append(g);
    }
    all.append(s);
  }
  buildFx(all); buildMods(all);
  drawWaves();
}
// A sampler's recording: which one (the names on the card, in their folders), its picture, and a
// key that plays it at its own pitch for as long as it is held (as X on the handheld).
function recordingSection(w) {
  const s = el('section'), top = el('div', 'wavetop'), pick = el('select'), play = el('button');
  s.append(el('h2', '', 'RECORDING'));
  pick.id = 'recording';
  recordingOptions(pick, w.band ? 'THE BUILT-IN TONE' : 'NONE', w.recording);
  pick.addEventListener('change', () => { tell('sample', ('sample ' + pick.value).trim()); pick.blur(); });
  play.id = 'play'; play.textContent = 'PLAY  (hold, or hold the space bar)';
  play.addEventListener('pointerdown', e => { note(spec.hear, true); play.setPointerCapture(e.pointerId); e.preventDefault(); });
  for (const up of ['pointerup', 'pointercancel']) play.addEventListener(up, () => note(spec.hear, false));
  top.append(pick, play);
  s.append(top, wave(w));
  return s;
}
// The recordings on the card, in their folders, after a first choice that is none of them ('' ).
function recordingOptions(pick, first, now) {
  const none = el('option', '', first); none.value = ''; pick.append(none);
  let group = null, folder = null;
  for (const name of spec.recordings) {
    const cut = name.lastIndexOf('/'), f = cut < 0 ? '' : name.slice(0, cut);
    if (f !== folder || !group) { folder = f; group = el('optgroup'); group.label = f || 'UNSORTED'; pick.append(group); }
    const o = el('option', '', name.slice(cut + 1)); o.value = name; o.disabled = /[^ -~]/.test(name); group.append(o);
  }
  if (now && !spec.recordings.includes(now)) { const o = el('option', '', now + ' (missing)'); o.value = now; o.disabled = true; pick.append(o); }
  pick.value = now;
}

// ---- a drum KIT: a card a lane ----
// Each lane's settings are ids of their own (lane * spec.stride + field), so a lane's control sends
// `set` as any other. Fields 0 to 7 are EDIT's rows (START, END, FADE IN, FADE OUT, GAIN, PITCH, FINE,
// REVERSE); then FIT, CHOKE, VOLUME, HUMAN TIME, HUMAN LEVEL, SWING.
const kEdit = {start: 0, end: 1, fadeIn: 2, fadeOut: 3, gain: 4, pitch: 5, fine: 6, reverse: 7}, kFit = 8, kChoke = 9, kVolume = 10, kHumanTime = 11, kHumanLevel = 12, kSwing = 13;
// The guard and how loud a point of the file is made, for the picture only: lane_edit.cpp's guarded()
// and laneEditLevelAt(), which the KIT page's EDIT view draws with. Keep them the same.
function guarded(x) { const a = Math.abs(x); if (a <= 0.9) return x; const y = 0.9 + 0.1 * Math.tanh((a - 0.9) / 0.1); return x < 0 ? -y : y; }
function levelAt(v, b, at, seconds) {
  const start = v[b + kEdit.start] / 100000, end = v[b + kEdit.end] / 100000;
  if (at < start || at >= end) return 1;
  const speed = Math.pow(2, (v[b + kEdit.pitch] + v[b + kEdit.fine] / 100) / 12);
  const heard = (v[b + kEdit.reverse] ? end - at : at - start) * seconds / speed, length = (end - start) * seconds / speed;
  let g = v[b + kEdit.gain] === 0 ? 1 : Math.pow(10, v[b + kEdit.gain] / 200);
  const fadeIn = v[b + kEdit.fadeIn] / 1000, fadeOut = v[b + kEdit.fadeOut] / 1000;
  if (heard < fadeIn) g *= heard / fadeIn;
  if (length - heard < fadeOut) g *= (length - heard) / fadeOut;
  return g;
}
// START and END as the handheld reads them (laneEditText): a time into the lane's own file. Shown
// while a mark is dragged; the handheld's own reading takes its place when it comes.
function timeText(v, seconds) { const s = v / 100000 * seconds; return s < 1 ? (s * 1000).toFixed(1) + ' MS' : s.toFixed(3) + ' S'; }
function buildKit(all) {
  const help = el('div', '', 'PLAY a lane: hold its PLAY, or hold 1 to 8 on the computer\'s keyboard. Drag START and END on a picture. A lane\'s SOUND may be a SYNTH voice, made from its knobs. The files on the card are never changed.');
  help.id = 'kithelp'; all.append(help);
  const lanes = el('div', 'lanes');
  for (const w of spec.waves) lanes.append(laneCard(w));
  all.append(lanes);
  drawWaves();
}
function laneCard(w) {
  const lane = w.lane, base = lane * spec.stride, card = el('section', 'lane'), top = el('div', 'top');
  card.dataset.lane = lane;
  const pick = el('select'), play = el('button', '', 'PLAY');
  pick.id = 'sound' + lane; play.id = 'play' + lane;
  recordingOptions(pick, 'BUILT-IN ' + w.drum, w.recording);
  // DRUM SYNTH: the twelve models after the built-in drum (value "synth:WORD"), as on the handheld.
  const synths = el('optgroup'); synths.label = 'SYNTH';
  for (const m of spec.models || []) { const o = el('option', '', m.name); o.value = 'synth:' + m.word; synths.append(o); }
  pick.insertBefore(synths, pick.children[1] || null);
  pick.value = soundOf(w);
  pick.addEventListener('change', () => {
    const v = pick.value;
    if (v.startsWith('synth:')) tell('lanesample' + lane, 'lanesynth ' + lane + ' ' + v.slice(6));
    else tell('lanesample' + lane, ('lanesample ' + lane + ' ' + v).trim());
    pick.blur();
  });
  play.addEventListener('pointerdown', e => { hear(lane, true); play.setPointerCapture(e.pointerId); e.preventDefault(); });
  for (const up of ['pointerup', 'pointercancel']) play.addEventListener(up, () => hear(lane, false));
  top.append(el('span', 'num', String(lane + 1)), pick, play);
  card.append(top);
  const laneState = () => state && state.lanes ? state.lanes[lane] : null;
  // A setting of this lane: what the handheld reads, once it has the value shown.
  const item = field => {
    const it = itemOf(base + field);
    if (field <= kEdit.reverse) it.reading = v => { const s = laneState(); return s && state.values[it.id] === v ? s.readings[field] : it.time ? timeText(v, w.seconds) : shown(it, v); };
    return it;
  };
  w.units = w.units || 100000;
  w.label = id => itemOf(id).name + ' ' + item(id % spec.stride).reading(state ? +state.values[id] : 0);
  w.level = at => state && state.values ? levelAt(state.values, base, at, w.seconds) : 1;
  w.emptyText = w.missing ? 'NOT ON THE CARD: ' + w.recording + '. THE DRUM PLAYS.' : 'NO SOUND';
  w.noteText = () => {
    const s = laneState(), fit = state && state.values ? state.values[base + kFit] : 0;
    const voice = w.synth && s && s.synth ? (s.synth.voice >= 0 ? w.synth.voices[s.synth.voice] : 'TURNED BY HAND') : '';
    const what = (w.synth ? w.synth.name + ', SYNTH, ' + voice : w.recording || w.drum + ', BUILT IN') + ' · ' + w.seconds.toFixed(2) + ' S';
    if (!s) return what;
    return what + ' · ' + (s.making ? 'MAKING IT...' : s.fitting ? 'FITTING...' : 'PLAYS ' + s.plays.toFixed(2) + ' S' + (fit > 0 && w.recording ? ', FITTED' : ''));
  };
  card.append(wave(w));
  const g = el('div', 'grid five');
  for (const f of [kVolume, kChoke, kFit, kHumanTime, kHumanLevel, kSwing]) {
    const box = control(item(f));
    if (f === kFit && !w.recording && !w.synth) { box.querySelector('select').disabled = true; box.title = 'FIT stretches a recording: a built-in drum has none.'; }
    g.append(box);
  }
  card.append(g);
  if (w.synth) card.append(synthBox(w, lane, laneState));
  card.append(el('h3', '', 'EDIT'));
  const e = el('div', 'grid');
  for (const f of [kEdit.start, kEdit.end, kEdit.fadeIn, kEdit.fadeOut, kEdit.gain, kEdit.pitch, kEdit.fine, kEdit.reverse]) e.append(control(item(f)));
  const acts = el('div', 'acts'), norm = el('button', 'quiet', 'NORMALIZE'), trim = el('button', 'quiet', 'TRIM SILENCE');
  norm.id = 'normalize' + lane; trim.id = 'trim' + lane;
  // Done on the handheld (it knows the sound): the boxes take what it made of them.
  const after = fields => { for (const f of fields) touched[base + f] = 0; setTimeout(look, 250); };
  norm.addEventListener('click', () => { tell('normalize' + lane, 'normalize ' + lane); after([kEdit.gain]); });
  trim.addEventListener('click', () => { tell('trim' + lane, 'trim ' + lane); after([kEdit.start, kEdit.end]); });
  acts.append(norm, trim);
  card.append(e, acts);
  return card;
}
// What a lane's SOUND choice shows: its synth model, else its recording ('' : the built-in drum).
function soundOf(w) { return w.synth ? 'synth:' + w.synth.word : w.recording; }
// A synth lane's VOICE (DRUM SYNTH): its named voices, then its knobs, each a dial 0..100 whose reading
// the handheld gives (it hangs on the model), shown as the number while it is on its way.
function synthBox(w, lane, laneState) {
  const box = el('div'); box.append(el('h3', '', 'VOICE: ' + w.synth.name));
  const row = el('div', 'acts'), pick = el('select');
  pick.id = 'voice' + lane;
  const turned = el('option', '', 'TURNED BY HAND'); turned.value = -1; turned.disabled = true; pick.append(turned);
  w.synth.voices.forEach((name, i) => { const o = el('option', '', name); o.value = i; pick.append(o); });
  pick.addEventListener('change', () => {
    tell('lanevoice' + lane, 'lanevoice ' + lane + ' ' + pick.value);
    for (let p = 0; p < 8; ++p) touched[spec.knobs + lane * 8 + p] = 0; // the knobs take what the handheld made of it
    pick.blur(); setTimeout(look, 250);
  });
  row.append(pick); box.append(row);
  const g = el('div', 'grid');
  w.synth.places.forEach((name, p) => {
    if (!name) return; // a place this model does not use
    const it = {name, id: spec.knobs + lane * 8 + p, min: 0, max: 100}; // (ids from spec.knobs: designer.hpp's kKitKnobs)
    it.reading = v => { const s = laneState(); return s && s.synth && state.values[it.id] === v ? s.synth.readings[p] : String(v); };
    g.append(control(it, name));
  });
  box.append(g);
  return box;
}
// A lane heard for as long as it is held, as X on the handheld.
const lanesDown = new Set();
function hear(lane, on) {
  if (on === lanesDown.has(lane)) return;
  on ? lanesDown.add(lane) : lanesDown.delete(lane);
  tell('h' + (++serial), 'hear ' + lane + ' ' + (on ? 1 : 0));
  const b = $('play' + lane); if (b) b.classList.toggle('on', on);
}

// ---- EFFECTS and MODs: every instrument's page ends with them ----
// spec.fx: slots[3], buses[2], master[2] ({name, kinds:[{id,name}], params:[{name,choices,texts?}]}),
// sends[2] ({name}), mix{dbmin,dbmax,pan}. state.fxkinds[7] (the slots, the buses', the master's),
// state.fx[30] in designer.hpp's fxset ids, state.fxreadings[30] what each reads, state.mix[db,pan].
// spec.mods: targets[{id,name}], rates[], shapes[], phases[].
// state.mods[2]: {kind,target,depth,rate,shape,phase,length,slew,chance,speed,steps[16]}.
const kFxSends = 12, kFxBus = 14, kFxMaster = 22, kFxMixDb = 30, kFxMixPan = 31;
const kMod = {kind: 0, target: 1, depth: 2, rate: 3, shape: 4, phase: 6, length: 7, slew: 8, chance: 9, speed: 10, fields: 11};
let fxControls = {}, fxKinds = {}, modCards = [];
// An effect's setting that is an amount (a slider 0..1000) or a choice (a dropdown of its readings).
function fxControl(fxid, name, param) {
  const box = el('div', 'ctl'), lab = el('label'), val = el('b');
  lab.append(el('span', '', name), val); box.append(lab);
  let input, now = 0;
  const read = () => state && state.fxreadings ? state.fxreadings[fxid] : '';
  if (param && param.choices > 0) {
    input = el('select');
    param.texts.forEach((t, i) => { const o = el('option', '', t); o.value = i; input.append(o); });
    input.addEventListener('change', () => {
      const i = +input.value; now = (i + 0.5) / param.choices;
      fxTell(fxid, now); val.textContent = ''; input.blur();
    });
  } else {
    input = el('input'); input.type = 'range'; input.min = 0; input.max = 1000; input.step = 1;
    input.addEventListener('input', () => { now = input.value / 1000; fxTell(fxid, now); });
  }
  const show = () => { val.textContent = input.tagName === 'SELECT' ? '' : read(); };
  const put = v => {
    if (input.tagName === 'SELECT') input.value = Math.min(param.choices - 1, Math.floor(v * param.choices));
    else input.value = Math.round(v * 1000);
    now = v; show();
  };
  box.append(input);
  fxControls[fxid] = {input, show, put};
  return box;
}
function fxTell(fxid, v) {
  touched['fx' + fxid] = Date.now();
  if (state && state.fx) state.fx[fxid] = v;
  tell('fx' + fxid, 'fxset ' + fxid + ' ' + v);
}
// One slot: which effect it holds, and the settings of that one. A slot given another effect is a
// new spec, so the page is built again (as a preset picked: the boxes let go, and look soon).
function fxSlot(name, base, slotNo, kinds, params) {
  const card = el('section', 'lane'), row = el('div', 'fxkind'), pick = el('select');
  card.append(el('h3', '', name));
  for (const k of kinds) { const o = el('option', '', k.name); o.value = k.id; pick.append(o); }
  pick.addEventListener('change', () => { tell('fxtype' + slotNo, 'fxtype ' + slotNo + ' ' + pick.value); touched = {}; pick.blur(); setTimeout(look, 250); });
  row.append(pick); card.append(row);
  fxKinds[slotNo] = pick;
  const g = el('div', 'grid');
  params.forEach((p, i) => g.append(fxControl(base + i, p.name, p)));
  card.append(g);
  return card;
}
function buildFx(all) {
  const fx = spec.fx; if (!fx) return;
  const s = el('section'); s.append(el('h2', '', 'EFFECTS'));
  const cards = el('div', 'lanes');
  fx.slots.forEach((slot, i) => cards.append(fxSlot(slot.name, i * 4, i, slot.kinds, slot.params)));
  fx.buses.forEach((bus, b) => cards.append(fxSlot(bus.name, kFxBus + b * 4, 3 + b, bus.kinds, bus.params)));
  fx.master.forEach((slot, i) => cards.append(fxSlot(slot.name, kFxMaster + i * 4, 5 + i, slot.kinds, slot.params)));
  s.append(cards);
  const g = el('div', 'grid');
  fx.sends.forEach((send, b) => g.append(fxControl(kFxSends + b, send.name + ' SEND', null)));
  const mix = fx.mix, db = el('div', 'ctl'), pan = el('div', 'ctl');
  db.append(mixRow('LEVEL', mix.dbmin, mix.dbmax, v => (v > 0 ? '+' : '') + v + ' dB', kFxMixDb));
  pan.append(mixRow('PAN', -mix.pan, mix.pan, v => v === 0 ? 'CENTER' : v < 0 ? (-v) + ' L' : v + ' R', kFxMixPan));
  g.append(db, pan);
  s.append(g); all.append(s);
}
// The mixer's LEVEL and PAN: whole numbers over their range.
function mixRow(name, lo, hi, text, fxid) {
  const frag = document.createDocumentFragment(), lab = el('label'), val = el('b'), input = el('input');
  lab.append(el('span', '', name), val); frag.append(lab);
  input.type = 'range'; input.min = lo; input.max = hi; input.step = 1;
  let now = 0;
  const show = () => { val.textContent = text(now); };
  const put = v => { input.value = v; now = v; show(); };
  input.addEventListener('input', () => { now = +input.value; touched['fx' + fxid] = Date.now(); show(); tell('fx' + fxid, 'fxset ' + fxid + ' ' + now); });
  frag.append(input);
  fxControls[fxid] = {input, show, put, mix: fxid === kFxMixDb ? 0 : 1};
  return frag;
}
// One MOD: KIND, TARGET, DEPTH always; an LFO's SHAPE, RATE and PHASE (RATE's FREE: SPEED instead of
// PHASE), a STEPPER's RATE (in time alone), LENGTH, SLEW, CHANCE and its row of 16 steps. Aiming an
// OFF MOD at something wakes it as an LFO (ModKnob::fresh), so KIND picked first is picked again.
function modCard(m) {
  const card = el('section', 'lane'); card.append(el('h3', '', 'MOD ' + (m + 1)));
  const box = {lfo: [], stepper: [], free: []};
  const sel = (name, options, field) => {
    const row = el('div', 'fxkind'), lab = el('span', 'dim', name), pick = el('select');
    for (const o of options) { const e = el('option', '', o.text); e.value = o.value; pick.append(e); }
    pick.addEventListener('change', () => { modTell(m, field, +pick.value); pick.blur(); });
    row.append(lab, pick); card.append(row);
    return pick;
  };
  box.kind = sel('KIND', [{text: 'OFF', value: 0}, {text: 'LFO', value: 1}, {text: 'STEPPER', value: 2}], kMod.kind);
  box.target = sel('TARGET', spec.mods.targets.map(t => ({text: t.name, value: t.id})), kMod.target);
  box.depth = modSlider(card, 'DEPTH', -100, 100, v => (v > 0 ? '+' : '') + Math.round(v) + '%', v => modTell(m, kMod.depth, v / 100), v => v * 100);
  box.rate = sel('RATE', [], kMod.rate);
  box.shape = sel('SHAPE', spec.mods.shapes.map((t, i) => ({text: t, value: i})), kMod.shape);
  box.phase = sel('PHASE', spec.mods.phases.map((t, i) => ({text: t, value: i})), kMod.phase);
  box.lfo.push(box.shape.parentElement, box.phase.parentElement);
  box.length = modSlider(card, 'LENGTH', 1, 16, v => String(Math.round(v)), v => modTell(m, kMod.length, Math.round(v)), v => v);
  box.slew = modSlider(card, 'SLEW', 0, 100, v => Math.round(v) + '%', v => modTell(m, kMod.slew, v / 100), v => v * 100);
  box.chance = modSlider(card, 'CHANCE', 0, 100, v => Math.round(v) + '%', v => modTell(m, kMod.chance, v / 100), v => v * 100);
  box.stepper.push(box.length.box, box.slew.box, box.chance.box);
  box.speed = modSlider(card, 'SPEED', 2, 2000, v => (v / 100).toFixed(2) + ' HZ', v => modTell(m, kMod.speed, v / 100), v => v * 100);
  box.free.push(box.speed.box);
  const sh = el('h3', '', 'STEPS'); card.append(sh);
  const steps = el('div', 'steps');
  box.steps = [];
  for (let s = 0; s < 16; ++s) {
    const c = modSlider(null, 'S' + (s + 1), -100, 100, v => (v > 0 ? '+' : '') + Math.round(v), v => modTell(m, kMod.fields + s, v / 100), v => v * 100);
    steps.append(c.box); box.steps.push(c);
  }
  card.append(steps);
  box.stepper.push(sh, steps);
  box.m = m; box.lastKind = -1;
  modCards.push(box);
  return card;
}
// A MOD's slider: over lo..hi, told as told(v), read back as back(stored).
function modSlider(card, name, lo, hi, text, told, back) {
  const box = el('div', 'ctl'), lab = el('label'), val = el('b'), input = el('input');
  lab.append(el('span', '', name), val); box.append(lab);
  input.type = 'range'; input.min = lo; input.max = hi; input.step = 1;
  let now = 0;
  const show = () => { val.textContent = text(now); };
  const put = v => { input.value = Math.round(v); now = v; show(); };
  input.addEventListener('input', () => { now = +input.value; show(); told(now); });
  box.append(input);
  if (card) card.append(box);
  return {box, input, show, put, back};
}
function modTell(m, f, v) {
  touched['mod' + m + '_' + f] = Date.now();
  const mod = state && state.mods ? state.mods[m] : null;
  if (mod) {
    if (f === kMod.kind) mod.kind = v; else if (f === kMod.target) mod.target = v; else if (f === kMod.depth) mod.depth = v;
    else if (f === kMod.rate) mod.rate = v; else if (f === kMod.shape) mod.shape = v; else if (f === kMod.phase) mod.phase = v;
    else if (f === kMod.length) mod.length = v; else if (f === kMod.slew) mod.slew = v; else if (f === kMod.chance) mod.chance = v;
    else if (f === kMod.speed) mod.speed = v; else mod.steps[f - kMod.fields] = v;
  }
  tell('mod' + m + '_' + f, 'modset ' + m + ' ' + f + ' ' + v);
}
function buildMods(all) {
  if (!spec.mods) return;
  const s = el('section'); s.append(el('h2', '', 'MODS'));
  const cards = el('div', 'lanes');
  for (let m = 0; m < 2; ++m) cards.append(modCard(m));
  s.append(cards); all.append(s);
}
// Put the handheld's EFFECTS and MODs on the page, except a control being moved right now.
function fillFxMods() {
  if (!state || !state.fx || !state.mods || !spec || !spec.mods || !spec.fx) return;
  const now = Date.now();
  for (const id in fxControls) {
    const c = fxControls[id];
    const v = c.mix !== undefined ? state.mix[c.mix] : state.fx[id];
    if (now - (touched['fx' + id] || 0) > 1500 && document.activeElement !== c.input) c.put(v); else c.show();
  }
  for (const s in fxKinds) { const i = fxKinds[s]; if (document.activeElement !== i) i.value = state.fxkinds[s]; }
  const freeRate = spec.mods.rates.length - 1;
  modCards.forEach(box => {
    const m = box.m, mod = state.mods[m];
    if (document.activeElement !== box.kind) box.kind.value = mod.kind;
    if (document.activeElement !== box.target) box.target.value = mod.target;
    if (box.lastKind !== mod.kind) { // an LFO's RATEs are every one, a STEPPER's in time alone
      box.lastKind = mod.kind;
      box.rate.textContent = '';
      spec.mods.rates.forEach((t, i) => { if (mod.kind !== 2 || i < freeRate) { const o = el('option', '', t); o.value = i; box.rate.append(o); } });
    }
    if (document.activeElement !== box.rate) box.rate.value = mod.rate;
    if (document.activeElement !== box.shape) box.shape.value = mod.shape;
    if (document.activeElement !== box.phase) box.phase.value = mod.phase;
    const lfo = mod.kind === 1, stepper = mod.kind === 2, free = lfo && mod.rate === freeRate;
    for (const b of box.lfo) b.hidden = !lfo || (b === box.phase.parentElement && free);
    for (const b of box.stepper) b.hidden = !stepper;
    for (const b of box.free) b.hidden = !free;
    const put = (c, key, v) => { if (now - (touched['mod' + m + '_' + key] || 0) > 1500 && document.activeElement !== c.input) c.put(c.back(v)); else c.show(); };
    put(box.depth, kMod.depth, mod.depth); put(box.length, kMod.length, mod.length); put(box.slew, kMod.slew, mod.slew);
    put(box.chance, kMod.chance, mod.chance); put(box.speed, kMod.speed, mod.speed);
    box.steps.forEach((c, s) => put(c, kMod.fields + s, mod.steps[s]));
  });
}

// An envelope as a line: each step moves to its level, quicker for a higher rate.
function drawEnvs() {
  if (!state || !state.values) return;
  for (const e of envs) {
    const v = i => state.values[e.items[i].id], end = Math.max(1, v(17)), hold = v(16);
    let x = 0, pts = [[0, 0]], holdX = -1; const widths = [];
    for (let i = 0; i < end && i < 8; ++i) widths.push(8 + (99 - v(i)) * 0.6);
    const total = widths.reduce((a, b) => a + b, 0) + (hold ? 40 : 0) || 1;
    for (let i = 0; i < widths.length; ++i) {
      x += widths[i]; pts.push([x, v(8 + i)]);
      if (hold === i + 1) { holdX = x; x += 40; pts.push([x, v(8 + i)]); }
    }
    const px = p => (p[0] / total * 396 + 2).toFixed(1) + ',' + (96 - p[1] / 99 * 92).toFixed(1);
    e.svg.innerHTML = '<polyline fill="none" stroke="#6fbfa3" stroke-width="2" vector-effect="non-scaling-stroke" points="' + pts.map(px).join(' ') + '"/>' +
      (holdX >= 0 ? '<rect x="' + (holdX / total * 396 + 2).toFixed(1) + '" y="0" width="' + (40 / total * 396).toFixed(1) + '" height="100" fill="#c9a15a" opacity=".12"/>' : '');
  }
}
// Put the handheld's values on the page, except a control being moved right now.
function fill() {
  if (!state || !state.values) return;
  const now = Date.now();
  for (const id in controls) {
    const c = controls[id];
    if (now - (touched[id] || 0) > 1500 && document.activeElement !== c.input) c.put(state.values[id]); else c.show();
  }
  macroControls.forEach((c, m) => { if (now - (touched['m' + m] || 0) > 1500) c.input.value = Math.round(state.macros[m] * 200); c.show(); });
  if (document.activeElement !== $('preset')) $('preset').value = state.preset;
  if (document.activeElement !== $('called')) $('called').value = state.called || '';
  $('what').textContent = 'TRACK ' + state.track + ' · ' + state.name;
  const pick = $('recording');
  if (pick && spec.waves && document.activeElement !== pick) pick.value = spec.waves[0].recording; // (a pick refused: back to what is on)
  if (spec.page === 'kit') for (const w of spec.waves) {
    const p = $('sound' + w.lane); if (p && document.activeElement !== p) p.value = soundOf(w);
    const s = state.lanes && state.lanes[w.lane]; // a synth lane's picture, length and least gap move with its knobs: the state's
    if (w.synth && s && s.synth) { w.peaks = s.synth.peaks; w.seconds = s.synth.seconds; w.least = s.synth.least; }
    const v = $('voice' + w.lane); if (v && s && s.synth && document.activeElement !== v) v.value = s.synth.voice;
  }
  fillFxMods();
  drawEnvs(); drawWaves();
}
async function look() {
  try {
    state = await get('/design/state');
    // The spec is asked for again whenever the handheld says it changed (another synth, a preset
    // saved, another recording, a recording put on the card).
    if (!spec || state.spec !== specSerial) { specSerial = state.spec; spec = await get('/design/spec'); build(); }
    if (!state.name) $('what').textContent = 'TRACK ' + state.track;
    fill();
  } catch (e) { say('The handheld is not answering. Is the WiFi page still on?', 'no'); }
}
$('preset').addEventListener('change', () => { tell('preset', 'preset ' + $('preset').value); touched = {}; $('preset').blur(); setTimeout(look, 250); });
for (const box of [$('called'), $('name')]) box.addEventListener('keydown', e => { if (e.key === 'Enter') box.blur(); });
$('called').addEventListener('change', () => { tell('name', 'name ' + $('called').value.toUpperCase().replace(/[^A-Z0-9 _-]/g, '')); });
$('roll').addEventListener('click', () => { tell('roll', 'roll ' + Math.floor(Math.random() * 1e6)); touched = {}; setTimeout(look, 250); });
$('save').addEventListener('click', () => {
  const name = $('name').value.toUpperCase().replace(/[^A-Z0-9 _-]/g, '').trim();
  if (!name) return say('Give it a name first: letters, numbers and spaces.', 'no');
  tell('save', 'save ' + name); say('Saved as ' + name + '. It is in the handheld\'s preset list, in the USER folder.', 'ok'); setTimeout(look, 400);
});

// ---- the keyboard ----
let octave = 4; const down = new Set();
function note(key, on) { if (key < 0 || key > 127 || on === down.has(key)) return; on ? down.add(key) : down.delete(key); tell('n' + (++serial), 'note ' + key + ' ' + (on ? 1 : 0)); paint(); }
function board() {
  const b = $('board'); b.textContent = ''; const whites = [0, 2, 4, 5, 7, 9, 11], blacks = {1: 1, 3: 2, 6: 4, 8: 5, 10: 6}, n = 21, w = 100 / n;
  for (let o = 0; o < 3; ++o) {
    whites.forEach((s, i) => { const k = el('div', 'w'); k.style.left = (o * 7 + i) * w + '%'; k.style.width = w + '%'; k.dataset.semi = o * 12 + s; b.append(k); });
    for (const s in blacks) { const k = el('div', 'b'); k.style.left = (o * 7 + blacks[s]) * w - w * 0.3 + '%'; k.style.width = w * 0.6 + '%'; k.dataset.semi = o * 12 + +s; b.append(k); }
  }
  $('oct').textContent = 'lowest key: C' + (octave - 1);
}
function paint() { for (const k of $('board').children) k.classList.toggle('on', down.has(octave * 12 + +k.dataset.semi)); }
let pointerKey = -1;
$('board').addEventListener('pointerdown', e => { if (e.target.dataset.semi === undefined) return; pointerKey = octave * 12 + +e.target.dataset.semi; note(pointerKey, true); e.preventDefault(); });
for (const up of ['pointerup', 'pointercancel', 'pointerleave']) window.addEventListener(up, () => { if (pointerKey >= 0) note(pointerKey, false); pointerKey = -1; });
const letters = 'awsedftgyhujkolp;';
window.addEventListener('keydown', e => {
  // Only a box being typed in keeps the keys to itself. Sliders and dropdowns do not.
  if (e.repeat || e.metaKey || e.ctrlKey || e.altKey || (e.target.tagName === 'INPUT' && e.target.type === 'text')) return;
  const k = e.key.toLowerCase();
  if (spec && spec.page === 'kit') { const lane = '12345678'.indexOf(k); if (lane >= 0 && k) { hear(lane, true); e.preventDefault(); } return; }
  if (k === ' ' && spec && spec.page === 'sampler') { note(spec.hear, true); e.preventDefault(); return; }
  if (k === 'z' || k === 'x') { for (const held of [...down]) note(held, false); octave = Math.max(1, Math.min(8, octave + (k === 'x' ? 1 : -1))); board(); return; }
  const i = letters.indexOf(k); if (i >= 0) { note((octave + 1) * 12 + i, true); e.preventDefault(); }
});
window.addEventListener('keyup', e => {
  if (spec && spec.page === 'kit') { const lane = '12345678'.indexOf(e.key); if (lane >= 0 && e.key) hear(lane, false); return; }
  if (e.key === ' ' && spec && spec.page === 'sampler') return note(spec.hear, false);
  const i = letters.indexOf(e.key.toLowerCase()); if (i >= 0) note((octave + 1) * 12 + i, false);
});
window.addEventListener('blur', () => { for (const held of [...down]) note(held, false); for (const lane of [...lanesDown]) hear(lane, false); });

board(); look(); setInterval(look, 800);
</script></body></html>
)page";

} // namespace apotheca
