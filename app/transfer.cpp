#include "transfer.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "chords.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "version.hpp"

namespace apotheca {

extern const char* const kDesignPage; // design_page.cpp

namespace {

const char* const kPage = R"page(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca</title>
<style>
*{box-sizing:border-box}
body{background:#14110e;color:#e0d5bf;font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;margin:0}
main{max-width:880px;margin:0 auto;padding:20px 16px 60px}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:4px 16px;border-bottom:1px solid #43392f;padding-bottom:12px}
h1{font-size:20px;letter-spacing:.16em;margin:0;font-weight:600}
.dim,.size,.count{color:#8f8370}
.meta{font-size:12px;letter-spacing:.06em;color:#8f8370;text-transform:uppercase}
header a{margin-left:auto}
h2{font-size:12px;font-weight:600;letter-spacing:.16em;color:#8f8370;margin:30px 0 8px;text-transform:uppercase;display:flex;align-items:center;gap:10px}
h2 .count{font-weight:400;letter-spacing:.06em}
p{color:#8f8370;margin:0 0 10px}
a{color:#6fbfa3}
button,.btn{background:none;color:#e0d5bf;border:1px solid #43392f;border-radius:3px;padding:5px 10px;font:inherit;font-size:11px;letter-spacing:.1em;text-transform:uppercase;cursor:pointer;text-decoration:none;white-space:nowrap;line-height:1.5}
button:hover,.btn:hover{border-color:#8f8370}
button.main{background:#6fbfa3;border-color:#6fbfa3;color:#14110e;font-weight:600;padding:9px 16px;font-size:12px}
button.del{color:#c45446}
button.sure{background:#c45446;border-color:#c45446;color:#14110e;font-weight:600}
button.play{color:#6fbfa3;min-width:54px}
button.play.on{background:#6fbfa3;border-color:#6fbfa3;color:#14110e}
input[type=text],select{background:#14110e;color:#e0d5bf;border:1px solid #43392f;border-radius:3px;padding:6px 8px;font:inherit;max-width:100%}
input[type=text]:focus,select:focus{outline:0;border-color:#6fbfa3}
.hid{display:none!important}
#drop{background:#1f1a16;border:1px dashed #43392f;border-radius:4px;padding:16px;margin-top:18px}
#drop.over,.folder.over{border-color:#6fbfa3;background:#232a25}
.line{display:flex;flex-wrap:wrap;align-items:center;gap:8px 10px}
.line label{font-size:11px;letter-spacing:.1em;color:#8f8370;text-transform:uppercase}
ul{list-style:none;padding:0;margin:0}
#log{margin-top:10px}
#log li{display:grid;grid-template-columns:1fr auto;gap:2px 12px;padding:6px 0;border-top:1px solid #2a231d;font-size:14px}
#log .what{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#log .state{color:#8f8370}
#log .ok .state{color:#6fbfa3}
#log .no .state{color:#c9a15a;white-space:normal;text-align:right}
.bar{grid-column:1/3;height:3px;background:#2a231d}
.bar i{display:block;height:3px;width:0;background:#6fbfa3}
.no .bar i{background:#c9a15a}
.folder,.panel{background:#1f1a16;border:1px solid #2a231d;border-radius:4px;margin-bottom:6px}
.fhead{display:flex;align-items:center;gap:10px;padding:9px 12px;cursor:pointer;user-select:none}
.fhead .tw{width:10px;color:#c9a15a;font-weight:600}
.fhead .fname{letter-spacing:.12em;font-size:13px;text-transform:uppercase;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fhead .loose{color:#9a76b0}
.fhead .count{font-size:12px;margin-right:auto}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:7px 12px;border-top:1px solid #2a231d}
.row .name{flex:1 1 180px;min-width:0;overflow-wrap:anywhere}
.row .size{font-size:12px;min-width:56px;text-align:right}
.acts{display:flex;gap:6px;flex-wrap:wrap}
.edit{flex:1 1 100%;display:flex;flex-wrap:wrap;gap:8px;align-items:center;background:#2a231d;padding:10px;border-radius:3px}
.edit input{flex:1 1 150px;min-width:0}
input.pick{width:17px;height:17px;margin:0;accent-color:#6fbfa3;cursor:pointer;flex:none}
#bulk{position:sticky;top:0;z-index:5;background:#2a231d;border:1px solid #6fbfa3;border-radius:3px;padding:9px 12px;margin:0 0 8px}
#bulk b{color:#6fbfa3;font-weight:600;letter-spacing:.06em}
.none{padding:8px 12px;color:#8f8370;font-size:13px;border-top:1px solid #2a231d}
.panel>.none:first-child,.panel>ul>.row:first-child{border-top:0}
#find{margin-left:auto;width:150px;font-size:13px;padding:4px 8px;letter-spacing:0;text-transform:none}
@media (max-width:560px){
 button,.btn{padding:8px 9px}
 .row .name{flex:1 1 0}
 .acts{flex-basis:100%}
 .acts>*{flex:1 1 auto;text-align:center}
 #find{width:110px}
}
</style></head><body><main>
<header><h1>APOTHECA</h1><span class="meta" id="version"></span><span class="meta" id="free"></span><a class="btn" id="design" href="#">Open the sound designer</a></header>

<div id="drop">
<div class="line"><button class="main" id="pick">Choose files</button><span class="dim">or drop them on the page, or on a folder.</span></div>
<div class="line" style="margin-top:12px"><label for="into">Recordings go in</label><select id="into"></select><input id="intoNew" class="hid" type="text" maxlength="24" placeholder="new folder name"></div>
<p style="margin:10px 0 0;font-size:13px">Recordings (.wav), chord packs (.json), songs (.song). 30 MB at most each. Every file is checked before it is kept.</p>
<input id="files" class="hid" type="file" multiple accept=".wav,.json,.song,audio/wav,audio/x-wav">
<ul id="log"></ul>
<div class="line hid" id="logEnd" style="margin-top:8px"><button id="clear">Clear the list</button></div>
</div>

<h2>Recordings <span class="count" id="nSamples"></span><input id="find" type="text" placeholder="find"></h2>
<div class="line hid" id="bulk"><b id="nPicked"></b><label for="bulkTo">move to</label><select id="bulkTo"></select><input id="bulkNew" class="hid" type="text" maxlength="24" placeholder="new folder name"><button id="bulkMove" class="main">Move</button><button id="bulkDelete" class="del">Delete</button><button id="bulkNone">Choose none</button></div>
<div id="recordings"></div>
<h2>Bounces <span class="count" id="nBounces"></span></h2><p>Your songs as sound files.</p><div class="panel"><ul id="bounces"></ul></div>
<h2>Songs <span class="count" id="nSongs"></span></h2><div class="panel"><ul id="songs"></ul></div>
<h2>Chord packs <span class="count" id="nChords"></span></h2><div class="panel"><ul id="chords"></ul></div>

<h2>Update Apotheca</h2>
<p>Choose the new <b>apotheca</b> program file (inside the apotheca folder of the zip). It is only sent here; you accept it on the handheld by holding A, and it is used the next time Apotheca starts.</p>
<button id="pickUpdate">Choose the program file</button><input id="update" class="hid" type="file">
</main>
<script>
const base = location.pathname.replace(/\/$/, '');
const $ = id => document.getElementById(id);
const kinds = {wav: 'samples', json: 'chords', song: 'songs'};
const NEW = '*new'; // a folder name cannot have a * in it
let all = {samples: [], songs: [], chords: [], bounces: [], folders: [], sizes: {}};
const picked = new Set(); // recordings ticked, to move or delete together
const open = new Set(), made = new Set(); // folders showing their files; folders named here with nothing in them yet
let first = true, filter = '', playing = '', audio = null, into = '';
$('design').href = base + '/design';

function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
function btn(text, act, cls) { const b = el('button', cls, text); b.dataset.act = act; return b; }
function urlOf(kind, name) { return base + '/' + kind + '/' + name.split('/').map(encodeURIComponent).join('/'); }
function split(name) { const i = name.indexOf('/'); return i < 0 ? ['', name] : [name.slice(0, i), name.slice(i + 1)]; }
function sizeText(n) { return n == null ? '' : n < 1024 ? n + ' B' : n < 1048576 ? Math.round(n / 1024) + ' KB' : (n / 1048576).toFixed(1) + ' MB'; }
function bytes(s) { return new TextEncoder().encode(s).length; }
// The same rules the handheld holds a name to. '' if it is fine, or what is wrong with it.
function wrong(name, longest) {
  if (!name) return 'it needs a name';
  if (bytes(name) > longest) return 'name too long: ' + longest + ' letters at most';
  if (/^[. ]/.test(name) || name.includes('..') || /[\x00-\x1f\x7f\/\\:"<>|?*]/.test(name)) return 'a name cannot start with a dot or a space, or have .. / \\ : " < > | ? * in it';
  return '';
}
function folders() {
  const s = new Set(all.folders || []);
  for (const n of all.samples) { const f = split(n)[0]; if (f) s.add(f); }
  for (const f of made) s.add(f);
  return [...s].sort((a, b) => a.localeCompare(b));
}
function folderSelect(sel, now) {
  sel.textContent = '';
  for (const f of ['', ...folders(), NEW]) { const o = el('option', '', f == '' ? 'UNSORTED' : f == NEW ? 'NEW FOLDER...' : f); o.value = f; sel.append(o); }
  sel.value = now; if (sel.value != now) sel.value = '';
}

// ---- the list of what was done ----
function note(what, state, cls) {
  const li = el('li', cls), w = el('span', 'what', what), s = el('span', 'state', state);
  li.append(w, s); $('log').prepend(li); $('logEnd').classList.remove('hid');
  return {li, state: s};
}
$('clear').onclick = () => { for (const li of [...$('log').children]) if (!li.classList.contains('busy')) li.remove(); if (!$('log').children.length) $('logEnd').classList.add('hid'); };

// ---- what is on the handheld ----
async function list() {
  try { all = await (await fetch(base + '/list')).json(); } catch (e) { note('the handheld', 'no answer: is the WiFi page still on?', 'no'); return; }
  all.sizes = all.sizes || {};
  for (const f of [...made]) if ((all.folders || []).includes(f)) made.delete(f);
  if (first) { first = false; if (all.samples.filter(n => !n.includes('/')).length <= 60) open.add(''); }
  $('version').textContent = all.version ? 'version ' + all.version + (all.built ? ', built ' + all.built : '') : '';
  $('free').textContent = all.free >= 0 ? (all.free >= 1024 ? (all.free / 1024).toFixed(1) + ' GB' : all.free + ' MB') + ' free on the card' : '';
  draw();
}
function draw() {
  folderSelect($('into'), $('intoNew').classList.contains('hid') ? into : NEW);
  drawRecordings();
  for (const [kind, id, n] of [['bounces', 'bounces', 'nBounces'], ['songs', 'songs', 'nSongs'], ['chords', 'chords', 'nChords']]) {
    const ul = $(id), names = all[kind] || []; ul.textContent = '';
    $(n).textContent = names.length || '';
    names.forEach((name, i) => ul.append(row(kind, name, name, (all.sizes[kind] || [])[i])));
    if (!names.length) ul.append(el('li', 'none', 'none yet'));
  }
}
function row(kind, name, label, size) {
  const li = el('li', 'row'); li.dataset.kind = kind; li.dataset.name = name;
  if (kind == 'samples') { const tick = el('input', 'pick'); tick.type = 'checkbox'; tick.checked = picked.has(name); tick.title = 'choose it, to move or delete several together'; li.append(tick); }
  if (kind == 'samples' || kind == 'bounces') {
    const on = playing == kind + '/' + name;
    li.append(btn(on ? 'stop' : 'play', 'play', on ? 'play on' : 'play'));
  }
  const acts = el('span', 'acts'), get = el('a', 'btn', 'download');
  get.href = urlOf(kind, name); get.download = label;
  acts.append(get);
  if (kind == 'samples') { li.draggable = true; acts.append(btn('rename', 'rename'), btn('move', 'move')); }
  acts.append(btn('delete', 'delete', 'del'));
  li.append(el('span', 'name', label.replace(/\.(wav|song|json)$/i, '')), el('span', 'size', sizeText(size)), acts); // the title, without its ending
  return li;
}
function drawRecordings() {
  const box = $('recordings'), groups = new Map([['', []]]);
  for (const f of folders()) groups.set(f, []);
  all.samples.forEach((name, i) => {
    const [f, file] = split(name);
    if (!filter || name.toLowerCase().includes(filter)) groups.get(f).push([name, file, (all.sizes.samples || [])[i]]);
  });
  $('nSamples').textContent = all.samples.length || '';
  const page = document.createDocumentFragment();
  let shown = 0;
  for (const [f, rows] of groups) {
    if (filter && !rows.length) continue;
    shown += rows.length;
    const isOpen = filter ? true : open.has(f), sec = el('section', 'folder'), head = el('div', 'fhead');
    sec.dataset.folder = f; head.dataset.act = 'toggle';
    head.append(el('span', 'tw', isOpen ? '-' : '+'), el('span', 'fname' + (f ? '' : ' loose'), f || 'UNSORTED'), el('span', 'count', rows.length), btn(rows.length && rows.every(r => picked.has(r[0])) ? 'choose none' : 'choose all', 'all'), btn('add files', 'add'));
    sec.append(head);
    if (isOpen) { // a shut folder draws no rows, so hundreds of files stay quick
      const ul = el('ul');
      for (const [name, file, size] of rows) ul.append(row('samples', name, file, size));
      if (!rows.length) ul.append(el('li', 'none', f ? 'nothing in it yet: drop recordings here' : 'none'));
      sec.append(ul);
    }
    page.append(sec);
  }
  if (filter && !shown) page.append(el('div', 'none', 'nothing by that name'));
  box.textContent = ''; box.append(page);
  bulkBar();
}

// ---- several at once ----
// Tick recordings (or CHOOSE ALL on a folder), then move or delete them together. They go one
// after another, each with its own line in the list above, and the page is read again once at the end.
function bulkBar() {
  for (const n of [...picked]) if (!all.samples.includes(n)) picked.delete(n);
  $('bulk').classList.toggle('hid', !picked.size);
  $('nPicked').textContent = picked.size + ' chosen';
  $('bulkDelete').textContent = 'delete'; $('bulkDelete').dataset.sure = '';
  const now = $('bulkTo').value;
  folderSelect($('bulkTo'), $('bulkNew').classList.contains('hid') ? now : NEW);
}
$('bulkTo').onchange = () => { $('bulkNew').classList.toggle('hid', $('bulkTo').value != NEW); if ($('bulkTo').value == NEW) $('bulkNew').focus(); };
$('bulkNone').onclick = () => { picked.clear(); drawRecordings(); };
$('bulkMove').onclick = async () => {
  const f = $('bulkTo').value == NEW ? $('bulkNew').value.trim() : $('bulkTo').value;
  if (f && wrong(f, 24)) { note('folder ' + f, 'refused: ' + wrong(f, 24), 'no'); return; }
  const names = [...picked].sort();
  for (const from of names) {
    const to = (f ? f + '/' : '') + split(from)[1];
    if (to == from) { picked.delete(from); continue; } // already there
    if (await moveOne(from, to)) picked.delete(from);
  }
  if (f) open.add(f);
  $('bulkNew').value = ''; $('bulkNew').classList.add('hid');
  list();
};
$('bulkDelete').onclick = async () => {
  const b = $('bulkDelete');
  if (!b.dataset.sure) { b.dataset.sure = '1'; b.textContent = 'delete all ' + picked.size; setTimeout(() => { b.dataset.sure = ''; b.textContent = 'delete'; }, 6000); return; }
  for (const name of [...picked].sort()) {
    if (playing == 'samples/' + name) stop();
    const r = await ask('DELETE', 'samples', name);
    note(name, r.ok ? r.text : 'not deleted: ' + r.text, r.ok ? 'ok' : 'no');
    if (r.ok) picked.delete(name);
  }
  list();
};
document.addEventListener('change', e => {
  if (!e.target.classList || !e.target.classList.contains('pick')) return;
  const name = e.target.closest('li').dataset.name;
  if (e.target.checked) picked.add(name); else picked.delete(name);
  // Only the bar and this folder's button change: the list is not drawn again for a tick.
  const sec = e.target.closest('.folder'), ticks = [...sec.querySelectorAll('input.pick')];
  sec.querySelector('[data-act=all]').textContent = ticks.length && ticks.every(t => t.checked) ? 'choose none' : 'choose all';
  bulkBar();
});
$('find').oninput = () => { filter = $('find').value.trim().toLowerCase(); drawRecordings(); };

// ---- sending ----
const queue = []; let busy = false;
function send(files, folder) {
  for (const file of files) {
    const kind = kinds[file.name.split('.').pop().toLowerCase()];
    if (!kind) { note(file.name, 'refused: not a .wav, .json or .song', 'no'); continue; }
    const f = kind == 'samples' ? folder : '', name = (f ? f + '/' : '') + file.name;
    const bad = (f && wrong(f, 24) && 'folder: ' + wrong(f, 24)) || wrong(file.name, f ? 36 : 60) || (file.size > 30 * 1048576 && 'too big: 30 MB at most');
    if (bad) { note(name, 'refused: ' + bad, 'no'); continue; }
    add(name, urlOf(kind, name), file, (all[kind] || []).includes(name));
    if (f) open.add(f);
  }
}
function add(name, url, file, replaces) {
  const n = note(name, 'waiting', 'busy'), bar = el('div', 'bar'), fill = el('i');
  bar.append(fill); n.li.append(bar);
  queue.push({url, file, replaces, n, fill});
  if (!busy) run();
}
async function run() { // one after another: the handheld takes one at a time
  busy = true;
  while (queue.length) {
    const job = queue.shift();
    job.n.state.textContent = 'sending';
    const r = await new Promise(done => {
      const x = new XMLHttpRequest();
      x.open('PUT', job.url);
      x.upload.onprogress = e => { if (e.lengthComputable) { const p = Math.round(100 * e.loaded / e.total); job.fill.style.width = p + '%'; job.n.state.textContent = p < 100 ? 'sending ' + p + '%' : 'checking'; } };
      x.onload = () => done({ok: x.status == 200, text: x.responseText.trim()});
      x.onerror = x.ontimeout = () => done({ok: false, text: 'no answer from the handheld'});
      x.send(job.file);
    });
    job.fill.style.width = '100%';
    job.n.li.className = r.ok ? 'ok' : 'no';
    job.n.state.textContent = r.ok ? r.text + (job.replaces && r.text == 'saved' ? ' (in place of the old one)' : '') : 'refused: ' + r.text;
  }
  busy = false;
  list();
}
function target() { // the folder chosen at the top, or null if it is not a name that will do
  if ($('into').value != NEW) return $('into').value;
  const f = $('intoNew').value.trim(), bad = wrong(f, 24);
  if (bad) { note('new folder', bad, 'no'); $('intoNew').focus(); return null; }
  made.add(f); into = f; $('intoNew').value = ''; $('intoNew').classList.add('hid'); draw(); // it is on the card once a file is in it
  return f;
}
let pickFor = null;
$('pick').onclick = () => { pickFor = null; $('files').click(); };
$('files').onchange = () => { const f = pickFor != null ? pickFor : target(); if (f != null) send([...$('files').files], f); $('files').value = ''; };
$('into').onchange = () => { const isNew = $('into').value == NEW; $('intoNew').classList.toggle('hid', !isNew); if (isNew) $('intoNew').focus(); else into = $('into').value; };
$('pickUpdate').onclick = () => $('update').click();
$('update').onchange = () => { const file = $('update').files[0]; if (file && file.size > 30 * 1048576) note('update: ' + file.name, 'refused: too big: 30 MB at most', 'no'); else if (file) add('update: ' + file.name, base + '/update/apotheca', file, false); $('update').value = ''; };

// ---- dropping: files from the computer, or a recording dragged to another folder ----
let over = null;
function mark(e) { if (over) over.classList.remove('over'); over = e; if (over) over.classList.add('over'); }
function ours(e) { return [...e.dataTransfer.types].some(t => t == 'Files' || t == 'text/x-recording'); }
document.addEventListener('dragstart', e => { const li = e.target.closest && e.target.closest('li.row'); if (li && li.dataset.kind == 'samples') { e.dataTransfer.setData('text/x-recording', li.dataset.name); e.dataTransfer.effectAllowed = 'move'; } });
document.addEventListener('dragover', e => { if (!ours(e)) return; e.preventDefault(); const moving = !e.dataTransfer.types.includes('Files'); mark(e.target.closest('.folder') || (moving ? null : $('drop'))); });
document.addEventListener('dragleave', e => { if (!e.relatedTarget) mark(null); });
document.addEventListener('drop', e => {
  if (!ours(e)) return;
  e.preventDefault(); mark(null);
  const sec = e.target.closest('.folder'), from = e.dataTransfer.getData('text/x-recording');
  if (from) { if (sec && sec.dataset.folder != split(from)[0]) move(from, (sec.dataset.folder ? sec.dataset.folder + '/' : '') + split(from)[1]); return; }
  const f = sec ? sec.dataset.folder : target();
  if (f != null) send([...e.dataTransfer.files], f);
});

// ---- play, delete, rename, move ----
function stop() { if (audio) { audio.pause(); URL.revokeObjectURL(audio.src); audio = null; } playing = ''; for (const b of document.querySelectorAll('button.play')) { b.textContent = 'play'; b.className = 'play'; } }
async function play(b, kind, name) {
  const key = kind + '/' + name, was = playing;
  stop();
  if (was == key) return;
  playing = key; b.textContent = '...';
  try {
    const got = await fetch(urlOf(kind, name));
    if (!got.ok) throw 0;
    const sound = new Blob([await got.arrayBuffer()], {type: 'audio/wav'}); // fetched whole, so every browser plays it
    if (playing != key) return;
    audio = new Audio(URL.createObjectURL(sound));
    audio.onended = stop;
    await audio.play();
    for (const p of document.querySelectorAll('button.play')) { const li = p.closest('li'); if (li.dataset.kind + '/' + li.dataset.name == key) { p.textContent = 'stop'; p.className = 'play on'; } }
  } catch (e) { if (playing == key) { stop(); note(name, 'this browser cannot play it', 'no'); } }
}
async function ask(method, kind, name, headers) {
  try { const r = await fetch(urlOf(kind, name), {method, headers}); return {ok: r.ok, text: (await r.text()).trim()}; }
  catch (e) { return {ok: false, text: 'no answer from the handheld'}; }
}
async function remove(kind, name) {
  if (playing == kind + '/' + name) stop();
  const r = await ask('DELETE', kind, name);
  note(name, r.ok ? r.text : 'not deleted: ' + r.text, r.ok ? 'ok' : 'no');
  list();
}
async function moveOne(from, to) { // one move, and its line in the list; true if it went
  if (playing == 'samples/' + from) stop();
  const r = await ask('POST', 'samples', from, {'X-To': to.split('/').map(encodeURIComponent).join('/')});
  note(from + ' to ' + to, r.ok ? r.text : 'refused: ' + r.text, r.ok ? 'ok' : 'no');
  if (r.ok && split(to)[0]) open.add(split(to)[0]);
  return r.ok;
}
async function move(from, to) { await moveOne(from, to); list(); }
function edit(li, moving) { // one small form for both: the name, and the folder
  const old = li.querySelector('.edit'); if (old) { old.remove(); return; }
  for (const e of document.querySelectorAll('.edit')) e.remove();
  const [f, file] = split(li.dataset.name), dot = file.lastIndexOf('.');
  const box = el('div', 'edit'), name = el('input'), sel = el('select'), fresh = el('input', 'hid'), save = btn('save', '', 'main'), cancel = btn('cancel', '');
  name.type = fresh.type = 'text'; name.value = file.slice(0, dot); name.maxLength = 56; fresh.maxLength = 24; fresh.placeholder = 'new folder name';
  folderSelect(sel, f);
  sel.onchange = () => { fresh.classList.toggle('hid', sel.value != NEW); if (sel.value == NEW) fresh.focus(); };
  const go = () => {
    const folder = sel.value == NEW ? fresh.value.trim() : sel.value, to = name.value.trim() + file.slice(dot);
    const bad = (folder && wrong(folder, 24) && 'folder: ' + wrong(folder, 24)) || wrong(to, folder ? 36 : 60);
    if (bad) { note(li.dataset.name, bad, 'no'); return; }
    const whole = (folder ? folder + '/' : '') + to;
    if (whole == li.dataset.name) { box.remove(); return; }
    move(li.dataset.name, whole);
  };
  save.onclick = go; cancel.onclick = () => box.remove();
  box.onkeydown = e => { if (e.key == 'Enter') go(); if (e.key == 'Escape') box.remove(); };
  box.append(name, sel, fresh, save, cancel); li.append(box);
  li.draggable = false; // so the words in the box can be picked out with the mouse
  if (moving) sel.focus(); else { name.focus(); name.select(); }
}
document.addEventListener('click', e => {
  const t = e.target.closest('[data-act]'); if (!t || !t.dataset.act) return;
  const li = t.closest('li.row'), sec = t.closest('.folder'), act = t.dataset.act;
  if (act == 'add') { e.stopPropagation(); pickFor = sec.dataset.folder; $('files').click(); return; }
  if (act == 'all') { // every recording in this folder (as the find box has narrowed it), or none of them
    const f = sec.dataset.folder, names = all.samples.filter(n => split(n)[0] == f && (!filter || n.toLowerCase().includes(filter)));
    const every = names.length && names.every(n => picked.has(n));
    for (const n of names) if (every) picked.delete(n); else picked.add(n);
    drawRecordings(); return;
  }
  if (act == 'toggle') { const f = sec.dataset.folder; if (open.has(f)) open.delete(f); else open.add(f); drawRecordings(); return; }
  const kind = li.dataset.kind, name = li.dataset.name;
  if (act == 'play') play(t, kind, name);
  if (act == 'rename' || act == 'move') edit(li, act == 'move');
  if (act == 'delete') { // asks once more, there in the row
    t.textContent = 'delete it'; t.className = 'sure'; t.dataset.act = 'sure';
    const keep = btn('keep', 'keep'); t.after(keep);
    setTimeout(() => { if (t.isConnected && t.dataset.act == 'sure') { t.textContent = 'delete'; t.className = 'del'; t.dataset.act = 'delete'; keep.remove(); } }, 6000);
  }
  if (act == 'keep') { const d = t.previousSibling; d.textContent = 'delete'; d.className = 'del'; d.dataset.act = 'delete'; t.remove(); }
  if (act == 'sure') remove(kind, name);
});
list();
</script></body></html>
)page";

bool sendAll(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t sent = ::send(fd, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

void reply(int fd, const char* status, const char* type, const std::string& body) {
    char head[256];
    const int n = std::snprintf(head, sizeof head,
                                "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n", status,
                                type, body.size());
    if (sendAll(fd, head, static_cast<std::size_t>(n))) sendAll(fd, body.data(), body.size());
}

std::string unescape(const std::string& in) { // %20 and the like
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

std::vector<std::string> filesEnding(const std::string& folder, const std::string& ending) {
    std::vector<std::string> names;
    if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (!safeFileName(file, ending).empty()) names.push_back(file);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

std::string jsonList(const std::vector<std::string>& names) {
    std::string out = "[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        out += i ? ",\"" : "\"";
        for (char c : names[i]) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
    }
    return out + "]";
}

// How big each of these files is, in bytes, in the same order.
std::string jsonSizes(const std::string& folder, const std::vector<std::string>& names) {
    std::string out = "[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        struct stat info;
        const bool there = stat((folder + "/" + names[i]).c_str(), &info) == 0;
        out += (i ? "," : "") + std::to_string(there ? static_cast<long long>(info.st_size) : 0LL);
    }
    return out + "]";
}

// Megabytes left on the card these folders are on; -1 if that cannot be told.
long long megabytesFree(const std::string& folder) {
    struct statvfs card;
    if (folder.empty() || statvfs(folder.c_str(), &card) != 0) return -1;
    return static_cast<long long>(static_cast<unsigned long long>(card.f_bavail) * card.f_frsize / (1024ull * 1024ull));
}

// A file that is really there, as itself: not a link to somewhere else, and
// (for "folder/name") not inside a link either. Only such a file is deleted or moved.
bool realFile(const std::string& folder, const std::string& name) {
    struct stat info;
    const std::size_t slash = name.find('/');
    if (slash != std::string::npos && (lstat((folder + "/" + name.substr(0, slash)).c_str(), &info) != 0 || !S_ISDIR(info.st_mode))) return false;
    return lstat((folder + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// A folder inside the recordings that has just lost a file goes too if that was its last.
void dropIfEmpty(const std::string& folder, const std::string& name) {
    if (const std::size_t slash = name.find('/'); slash != std::string::npos) rmdir((folder + "/" + name.substr(0, slash)).c_str()); // does nothing while files are in it
}

// A song deleted from the page goes where one deleted on the handheld goes:
// into "deleted" in the song folder, with its revs ("<name>.<n>.rev").
bool binSong(const std::string& folder, const std::string& file) {
    const std::string bin = folder + "/deleted", name = file.substr(0, file.size() - 5);
    mkdir(bin.c_str(), 0755); // fine if it is already there
    std::vector<std::string> revs;
    if (DIR* dir = opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string rev = entry->d_name;
            if (rev.size() > name.size() + 5 && rev.compare(0, name.size() + 1, name + ".") == 0 && rev.compare(rev.size() - 4, 4, ".rev") == 0 &&
                rev.find_first_not_of("0123456789", name.size() + 1) == rev.size() - 4)
                revs.push_back(rev);
        }
        closedir(dir);
    }
    if (std::rename((folder + "/" + file).c_str(), (bin + "/" + file).c_str()) != 0) return false;
    for (const std::string& rev : revs) std::rename((folder + "/" + rev).c_str(), (bin + "/" + rev).c_str());
    return true;
}

// What a request says after `name` in its headers ("x-to"), or "" if it does not say.
std::string headerValue(const std::string& head, const std::string& name) {
    std::string lower = head;
    for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    const std::size_t at = lower.find("\r\n" + name + ":");
    if (at == std::string::npos) return "";
    std::size_t from = at + name.size() + 3, to = head.find("\r\n", from);
    if (to == std::string::npos) to = head.size();
    while (from < to && head[from] == ' ') ++from;
    while (to > from && head[to - 1] == ' ') --to;
    return head.substr(from, to - from);
}

} // namespace

bool isProgramForThisMachine(const std::string& b) {
#if defined(__aarch64__)
    const unsigned machine = 183; // 64-bit ARM: the handheld
#elif defined(__x86_64__)
    const unsigned machine = 62;  // a desk computer, for testing
#else
    const unsigned machine = 0;   // unknown here: nothing is accepted
#endif
    if (b.size() < 64u * 1024u) return false; // Apotheca is hundreds of kilobytes
    const auto byte = [&](std::size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(b[i])); };
    if (byte(0) != 0x7F || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') return false; // the format Linux programs are in
    if (byte(4) != 2 || byte(5) != 1) return false;                                 // 64-bit, low byte first
    const unsigned type = byte(16) | byte(17) << 8, made = byte(18) | byte(19) << 8;
    return machine != 0 && made == machine && (type == 2 || type == 3);            // a program, for this processor
}

// A name with nothing in it that could reach outside its folder or upset a card's file system.
bool plainName(const std::string& name, std::size_t longest) {
    if (name.empty() || name.size() > longest || name[0] == '.' || name[0] == ' ') return false;
    for (char c : name)
        if (static_cast<unsigned char>(c) < 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*') return false;
    return name.find("..") == std::string::npos;
}

std::string safeFileName(const std::string& fromAddress, const std::string& ending) {
    const std::string name = unescape(fromAddress);
    if (name.size() <= ending.size() || !plainName(name, 60)) return "";
    std::string end = name.substr(name.size() - ending.size());
    for (char& c : end) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return end == ending ? name : "";
}

// A recording may be in a folder, one level down: "kicks/808.wav". The folder
// and the file are each held to the same rules. "" if it is not such a name.
std::string safeSamplePath(const std::string& fromAddress) {
    const std::string name = unescape(fromAddress);
    const std::size_t slash = name.find('/');
    if (slash == std::string::npos) return safeFileName(fromAddress, ".wav");
    const std::string folder = name.substr(0, slash), file = name.substr(slash + 1);
    if (!plainName(folder, 24) || file.size() <= 4 || !plainName(file, 36)) return "";
    std::string end = file.substr(file.size() - 4);
    for (char& c : end) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return end == ".wav" ? name : "";
}

// The folders inside the recordings, empty ones too.
std::vector<std::string> foldersIn(const std::string& folder) {
    std::vector<std::string> folders;
    if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            struct stat info;
            const std::string name = entry->d_name;
            if (plainName(name, 24) && stat((folder + "/" + name).c_str(), &info) == 0 && S_ISDIR(info.st_mode)) folders.push_back(name);
        }
        closedir(dir);
    }
    std::sort(folders.begin(), folders.end());
    return folders;
}

// The recordings: the loose ones, then those in each folder as "folder/name.wav".
std::vector<std::string> recordingsIn(const std::string& folder) {
    std::vector<std::string> names = filesEnding(folder, ".wav");
    for (const std::string& inside : foldersIn(folder))
        for (const std::string& file : filesEnding(folder + "/" + inside, ".wav"))
            if (file.size() <= 36) names.push_back(inside + "/" + file);
    return names;
}

std::string Transfer::folderFor(const std::string& kind) const { return kind == "samples" ? samples_ : kind == "songs" ? songs_ : kind == "chords" ? chords_ : kind == "bounces" ? bounces_ : ""; }

bool Transfer::start(const std::string& sampleFolder, const std::string& songFolder, const std::string& chordFolder, int port, unsigned seed) {
    stop();
    samples_ = sampleFolder;
    songs_ = songFolder;
    chords_ = chordFolder;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in where = {};
    where.sin_family = AF_INET;
    where.sin_addr.s_addr = htonl(INADDR_ANY);
    where.sin_port = htons(static_cast<uint16_t>(port));
    socklen_t size = sizeof where;
    if (bind(fd, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0 || listen(fd, 4) != 0 ||
        getsockname(fd, reinterpret_cast<sockaddr*>(&where), &size) != 0) {
        close(fd);
        return false;
    }
    port_ = ntohs(where.sin_port);
    seed = seed * 1664525u + 1013904223u;
    char pin[8];
    std::snprintf(pin, sizeof pin, "%04u", (seed >> 8) % 10000u);
    pin_ = pin;
    stopping_.store(false);
    listening_ = fd;
    thread_ = std::thread([this] { serve(); });
    return true;
}

void Transfer::stop() {
    if (listening_ < 0) return;
    stopping_.store(true);
    if (thread_.joinable()) thread_.join(); // it looks at `stopping_` five times a second
    close(listening_);
    listening_ = -1;
}

std::string Transfer::address() const {
    std::string host;
    ifaddrs* all = nullptr;
    if (getifaddrs(&all) == 0) {
        for (ifaddrs* a = all; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            const in_addr ip = reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr;
            if ((ntohl(ip.s_addr) >> 24) == 127) continue; // this machine talking to itself
            char text[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &ip, text, sizeof text)) host = text;
            break;
        }
        freeifaddrs(all);
    }
    return host + ":" + std::to_string(port_) + "/" + pin_;
}

void Transfer::serve() {
    while (!stopping_.load()) {
        pollfd waiting = {listening_, POLLIN, 0};
        if (poll(&waiting, 1, 200) <= 0) continue;
        const int connection = accept(listening_, nullptr, nullptr);
        if (connection < 0) continue;
        timeval patience = {5, 0}; // a browser that goes quiet does not hold the door
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof patience);
        answer(connection);
        shutdown(connection, SHUT_WR);
        char rest[512]; // let the browser finish reading before the door shuts
        while (recv(connection, rest, sizeof rest, 0) > 0) {}
        close(connection);
    }
}

void Transfer::answer(int fd) {
    // The request line and headers: up to 8 KB, ending in a blank line.
    std::string head;
    std::size_t end = std::string::npos;
    char block[2048];
    while (end == std::string::npos && head.size() < 8192) {
        const ssize_t got = recv(fd, block, sizeof block, 0);
        if (got <= 0) return;
        head.append(block, static_cast<std::size_t>(got));
        end = head.find("\r\n\r\n");
    }
    if (end == std::string::npos) return reply(fd, "431 Too Much", "text/plain", "too much before the file\n");
    std::string body = head.substr(end + 4); // the start of the file, if one is coming
    head.resize(end);

    const std::size_t space = head.find(' '), space2 = head.find(' ', space + 1);
    if (space == std::string::npos || space2 == std::string::npos) return reply(fd, "400 Bad Request", "text/plain", "not a request\n");
    const std::string method = head.substr(0, space);
    std::string path = head.substr(space + 1, space2 - space - 1);
    if (const std::size_t query = path.find('?'); query != std::string::npos) path.resize(query);

    // The number from the device's screen comes first, or nothing is said.
    const std::string door = "/" + pin_;
    if (path.compare(0, door.size(), door) != 0 || (path.size() > door.size() && path[door.size()] != '/'))
        return reply(fd, "403 Forbidden", "text/plain", "Open the address shown on the Apotheca screen, with its four-digit number.\n");
    path = path.substr(door.size());

    if (method == "GET" && (path.empty() || path == "/")) return reply(fd, "200 OK", "text/html; charset=utf-8", kPage);
    if (method == "GET" && path == "/list") {
        const std::vector<std::string> samples = recordingsIn(samples_), songs = filesEnding(songs_, ".song"), chords = filesEnding(chords_, ".json"),
                                       bounces = filesEnding(bounces_, ".wav");
        // "free" is megabytes left on the card; "sizes" are bytes, in the order of the names.
        return reply(fd, "200 OK", "application/json",
                     "{\"version\":\"" + std::string(kVersion) + "\",\"built\":\"" + std::string(kBuilt) + "\",\"free\":" + std::to_string(megabytesFree(exists(samples_) ? samples_ : songs_)) +
                         ",\"folders\":" + jsonList(foldersIn(samples_)) + ",\"sizes\":{\"samples\":" + jsonSizes(samples_, samples) + ",\"songs\":" + jsonSizes(songs_, songs) +
                         ",\"chords\":" + jsonSizes(chords_, chords) + ",\"bounces\":" + jsonSizes(bounces_, bounces) + "},\"samples\":" + jsonList(samples) + ",\"songs\":" + jsonList(songs) +
                         ",\"chords\":" + jsonList(chords) + ",\"bounces\":" + jsonList(bounces) + "}");
    }

    // The sound-designer page and what it says to the program.
    if (path == "/design" || path.compare(0, 8, "/design/") == 0) {
        if (!desk_) return reply(fd, "404 Not Found", "text/plain", "no sound designer here\n");
        if (method == "GET" && path == "/design") return reply(fd, "200 OK", "text/html; charset=utf-8", kDesignPage);
        if (method == "GET" && path == "/design/spec") return reply(fd, "200 OK", "application/json", desk_->spec());
        if (method == "GET" && path == "/design/state") return reply(fd, "200 OK", "application/json", desk_->state());
        if (path != "/design/do" || (method != "POST" && method != "PUT")) return reply(fd, "404 Not Found", "text/plain", "no such thing\n");
        // One short line of text.
        std::string lowerHead = head;
        for (char& c : lowerHead) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        const std::size_t where = lowerHead.find("\r\ncontent-length:");
        const unsigned long long size = where == std::string::npos ? 0 : std::strtoull(lowerHead.c_str() + where + 17, nullptr, 10);
        if (size == 0 || size > 200) return reply(fd, "400 Bad Request", "text/plain", "one short line\n");
        if (body.size() > size) body.resize(static_cast<std::size_t>(size));
        while (body.size() < size) {
            const ssize_t got = recv(fd, block, sizeof block, 0);
            if (got <= 0) return;
            body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(size) - body.size()));
        }
        for (char c : body)
            if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return reply(fd, "400 Bad Request", "text/plain", "plain letters only\n");
        return desk_->ask(body) ? reply(fd, "200 OK", "text/plain", "ok\n") : reply(fd, "400 Bad Request", "text/plain", "that makes no sense\n");
    }

    // A new Apotheca: set aside, never installed from here.
    if (path == "/update/apotheca") {
        if (method != "PUT") return reply(fd, "405 Not Allowed", "text/plain", "only PUT\n");
        if (program_.empty()) return reply(fd, "400 Bad Request", "text/plain", "this Apotheca cannot be updated this way\n");
        std::string lowerHead = head;
        for (char& c : lowerHead) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        const std::size_t where = lowerHead.find("\r\ncontent-length:");
        if (where == std::string::npos) return reply(fd, "411 Length Required", "text/plain", "how big is it?\n");
        const unsigned long long size = std::strtoull(lowerHead.c_str() + where + 17, nullptr, 10);
        if (size > kLargestFile) return reply(fd, "413 Too Large", "text/plain", "too big: 30 MB at most\n");
        if (lowerHead.find("\r\nexpect: 100-continue") != std::string::npos) sendAll(fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
        if (body.size() > size) body.resize(static_cast<std::size_t>(size));
        while (body.size() < size) {
            const ssize_t got = recv(fd, block, sizeof block, 0);
            if (got <= 0) return;
            body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(size) - body.size()));
        }
        if (!isProgramForThisMachine(body)) return reply(fd, "400 Bad Request", "text/plain", "not an Apotheca program for this handheld\n");
        if (!writeTextFile(program_ + "/apotheca.pending", body)) return reply(fd, "500 Could Not Save", "text/plain", "could not save it: is the card full?\n");
        updates_.fetch_add(1);
        return reply(fd, "200 OK", "text/plain", "sent. Now accept it on the handheld: hold A.\n");
    }

    // /samples/<name>.wav, /songs/<name>.song, /chords/<name>.json
    const std::size_t slash = path.find('/', 1);
    const std::string kind = slash == std::string::npos ? "" : path.substr(1, slash - 1);
    const std::string ending = kind == "samples" || kind == "bounces" ? ".wav" : kind == "songs" ? ".song" : kind == "chords" ? ".json" : "";
    const std::string folder = folderFor(kind);
    // A recording may be in a folder ("samples/kicks/808.wav"); nothing else may.
    const std::string name = ending.empty() ? "" : kind == "samples" ? safeSamplePath(path.substr(slash + 1)) : safeFileName(path.substr(slash + 1), ending);
    if (folder.empty() || name.empty()) return reply(fd, "400 Bad Request", "text/plain", "not a file name this takes\n");
    const std::string file = folder + "/" + name;

    if (method == "GET") {
        std::string contents;
        if (!readTextFile(file, contents)) return reply(fd, "404 Not Found", "text/plain", "no such file\n");
        return reply(fd, "200 OK", ending == ".wav" ? "audio/wav" : "application/octet-stream", contents); // a browser can play a recording
    }

    // Deleting: only a file that is there, by a name that passed the same rules as one sent.
    if (method == "DELETE") {
        if (!realFile(folder, name)) return reply(fd, "404 Not Found", "text/plain", "no such file\n");
        const bool gone = kind == "songs" ? binSong(folder, name) : unlink(file.c_str()) == 0;
        if (!gone) return reply(fd, "500 Could Not Delete", "text/plain", "could not delete it\n");
        if (kind == "samples") dropIfEmpty(folder, name);
        received_.fetch_add(1);
        return reply(fd, "200 OK", "text/plain", "deleted\n");
    }

    // Renaming or moving a recording: POST to where it is, with "X-To: <where it goes>"
    // written the way an address is. The new name is held to the rules of one sent.
    if (method == "POST") {
        if (kind != "samples") return reply(fd, "405 Not Allowed", "text/plain", "only recordings are renamed or moved\n");
        const std::string to = safeSamplePath(headerValue(head, "x-to"));
        if (to.empty()) return reply(fd, "400 Bad Request", "text/plain", "not a file name this takes\n");
        if (!realFile(folder, name)) return reply(fd, "404 Not Found", "text/plain", "no such file\n");
        const std::string target = folder + "/" + to;
        struct stat here, there;
        // Nothing is written over. (A card that takes "Kick" and "kick" for one name answers with the file itself.)
        if (lstat(target.c_str(), &there) == 0 && (stat(file.c_str(), &here) != 0 || here.st_ino != there.st_ino || here.st_dev != there.st_dev))
            return reply(fd, "409 Taken", "text/plain", "there is one of that name already\n");
        if (const std::size_t in = to.find('/'); in != std::string::npos) {
            const std::string inside = folder + "/" + to.substr(0, in);
            mkdir(inside.c_str(), 0755); // fine if it is already there
            if (lstat(inside.c_str(), &there) != 0 || !S_ISDIR(there.st_mode)) return reply(fd, "400 Bad Request", "text/plain", "not a folder this can use\n");
        }
        if (std::rename(file.c_str(), target.c_str()) != 0) {
            dropIfEmpty(folder, to); // a folder made for it just now does not linger
            return reply(fd, "500 Could Not Move", "text/plain", "could not move it\n");
        }
        dropIfEmpty(folder, name);
        received_.fetch_add(1);
        return reply(fd, "200 OK", "text/plain", "moved\n");
    }
    if (method != "PUT") return reply(fd, "405 Not Allowed", "text/plain", "not something this does\n");
    if (kind == "bounces") return reply(fd, "405 Not Allowed", "text/plain", "bounces are only fetched and deleted\n");

    // How big it says it is.
    std::string lower = head;
    for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    const std::size_t at = lower.find("\r\ncontent-length:");
    if (at == std::string::npos) return reply(fd, "411 Length Required", "text/plain", "how big is it?\n");
    const unsigned long long length = std::strtoull(lower.c_str() + at + 17, nullptr, 10);
    if (length > kLargestFile) return reply(fd, "413 Too Large", "text/plain", "too big: 30 MB at most\n");
    if (lower.find("\r\nexpect: 100-continue") != std::string::npos) sendAll(fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    if (body.size() > length) body.resize(static_cast<std::size_t>(length));
    while (body.size() < length) {
        const ssize_t got = recv(fd, block, sizeof block, 0);
        if (got <= 0) return; // gave up part-way: nothing is kept
        body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(length) - body.size()));
    }

    // Is it what it says it is?
    bool good = false;
    if (kind == "samples") {
        std::vector<float> sound;
        good = parseWav(body, sound) && !sound.empty();
    } else if (kind == "chords") {
        ChordPack pack;
        good = parseChordPack(body, pack);
    } else {
        std::unique_ptr<Project> song(new Project);
        good = parseSong(body, *song);
    }
    if (!good) {
        return reply(fd, "400 Bad Request", "text/plain",
                     kind == "samples" ? "not a WAV file this can read\n" : kind == "chords" ? "not a chord pack\n" : "not an Apotheca song\n");
    }
    mkdir(folder.c_str(), 0755); // fine if it is already there
    if (const std::size_t in = name.find('/'); in != std::string::npos) mkdir((folder + "/" + name.substr(0, in)).c_str(), 0755); // and the folder inside it
    if (!writeTextFile(file, body)) return reply(fd, "500 Could Not Save", "text/plain", "could not save it: is the card full?\n");
    received_.fetch_add(1);
    reply(fd, "200 OK", "text/plain", "saved\n");
}

} // namespace apotheca
