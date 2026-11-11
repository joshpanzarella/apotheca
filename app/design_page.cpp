// The sound-designer page, as served by the WiFi server (transfer.cpp). What
// it does and how it talks to the program: designer.hpp.
//
// Nothing about any synth is written here. The page asks the program what
// the synth on the track has (its pages, settings, ranges and the names of
// their values) and builds itself from the answer, so a new synth or a new
// setting shows up without touching this file.

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
#said{font-size:12px;min-height:18px} .ok{color:var(--teal)} .no{color:#d98a6a}
#nothing{padding:40px 0;color:var(--dim);font-size:16px}
</style></head><body>
<header>
  <h1>SOUND DESIGNER</h1><span id="what" class="dim"></span>
  <span class="grow"></span>
  <label class="dim">TRACK NAME <input id="called" type="text" maxlength="10" size="11" placeholder="ITS INSTRUMENT"></label>
  <label class="dim">PRESET <select id="preset"></select></label>
  <input id="name" type="text" maxlength="24" placeholder="NAME FOR A NEW PRESET" size="24">
  <button id="roll" class="quiet" hidden>ROLL A NEW SOUND</button>
  <button id="save">SAVE AS PRESET</button>
  <a id="files" href="#">files</a>
</header>
<div id="said" class="dim">The handheld makes the sound. Turn it up, or plug headphones into it.</div>
<div id="nothing" hidden>The track being worked on is not a synth. On the handheld, go to the SONG screen and move to a PHAZED or TWIN track (or add one with + TRACK).</div>
<div id="all"></div>
<div id="keys">
  <div class="row"><span>PLAY: click the keys, or use the computer keyboard: A W S E D F T G Y H U J K. Z and X: octave down / up.</span><span id="oct"></span></div>
  <div id="board"></div>
</div>
<script>
const base = location.pathname.replace(/\/design\/?$/, '');
document.getElementById('files').href = base + '/';
const $ = id => document.getElementById(id);
let spec = null, state = null, controls = {}, macroControls = [], touched = {}, envs = [];

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
function shown(item, v) { return item.texts ? item.texts[v - item.min] : String(v); }
function control(item, label) {
  const box = el('div', 'ctl'), lab = el('label'), val = el('b');
  lab.append(el('span', '', label || item.name), val); box.append(lab);
  let input;
  if (item.texts && item.max - item.min <= 8) {
    input = el('select');
    item.texts.forEach((t, i) => { const o = el('option', '', t); o.value = item.min + i; input.append(o); });
  } else {
    input = el('input'); input.type = 'range'; input.min = item.min; input.max = item.max; input.step = 1;
  }
  const show = () => { val.textContent = input.tagName === 'SELECT' ? '' : shown(item, +input.value); };
  input.addEventListener('input', () => { touched[item.id] = Date.now(); show(); tell('set' + item.id, 'set ' + item.id + ' ' + input.value); if (state) state.values[item.id] = +input.value; drawEnvs(); });
  // Once a choice is made the dropdown lets go of the keyboard, so the next
  // key pressed is a note again (and not a letter typed into the dropdown).
  if (input.tagName === 'SELECT') input.addEventListener('change', () => input.blur());
  box.append(input);
  controls[item.id] = {input, show};
  return box;
}
function build() {
  const all = $('all'); all.textContent = ''; controls = {}; macroControls = []; envs = [];
  const none = !spec || !spec.name;
  $('nothing').hidden = !none; $('keys').hidden = none; $('roll').hidden = none || !spec.roll;
  if (none) return;
  // presets, in their folders
  const sel = $('preset'); sel.textContent = ''; let group = null, folder = null;
  spec.presets.forEach((p, i) => {
    if (p.folder !== folder) { folder = p.folder; group = el('optgroup'); group.label = folder; sel.append(group); }
    const o = el('option', '', p.name); o.value = i; group.append(o);
  });
  // macros
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
    if (now - (touched[id] || 0) > 1500 && document.activeElement !== c.input) c.input.value = state.values[id];
    c.show();
  }
  macroControls.forEach((c, m) => { if (now - (touched['m' + m] || 0) > 1500) c.input.value = Math.round(state.macros[m] * 200); c.show(); });
  if (document.activeElement !== $('preset')) $('preset').value = state.preset;
  if (document.activeElement !== $('called')) $('called').value = state.called || '';
  $('what').textContent = 'TRACK ' + state.track + ' · ' + state.name;
  drawEnvs();
}
async function look() {
  try {
    state = await get('/design/state');
    if (!spec || spec.name !== state.name || (state.name && spec.presets.length !== state.presets)) { spec = await get('/design/spec'); build(); }
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
  if (k === 'z' || k === 'x') { for (const held of [...down]) note(held, false); octave = Math.max(1, Math.min(8, octave + (k === 'x' ? 1 : -1))); board(); return; }
  const i = letters.indexOf(k); if (i >= 0) { note((octave + 1) * 12 + i, true); e.preventDefault(); }
});
window.addEventListener('keyup', e => { const i = letters.indexOf(e.key.toLowerCase()); if (i >= 0) note((octave + 1) * 12 + i, false); });
window.addEventListener('blur', () => { for (const held of [...down]) note(held, false); });

board(); look(); setInterval(look, 800);
</script></body></html>
)page";

} // namespace apotheca
