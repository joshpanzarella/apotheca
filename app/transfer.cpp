#include "transfer.hpp"

#include <cerrno>

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "chords.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "version.hpp"

extern char** environ;

namespace apotheca {

extern const char* const kDesignPage; // design_page.cpp
bool plainName(const std::string& name, std::size_t longest); // (below)

namespace {

// Said at the top of the speed test's page at once, before the first row is timed.
const char* const kBenchSays = "The rows come as they are timed: about four minutes on the handheld. Leave the song stopped.\n\n";
const char* const kTooManyWrong = "too many wrong numbers: wait a minute\n";

const char* const kPage = R"page(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca</title>
<style>
*{box-sizing:border-box}
body{background:#14110e;color:#e0d5bf;font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;margin:0}
main{max-width:880px;margin:0 auto;padding:20px 16px 60px}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:4px 16px;border-bottom:1px solid #43392f;padding-bottom:12px}
h1{font-size:20px;letter-spacing:.16em;margin:0;font-weight:600}
.mark{height:11px;width:auto;color:#cdbb98;opacity:.8;align-self:center} /* the Shapetaker wordmark, as at the foot of the VCV Rack panels (Clairaudient.svg, Neokurat) */
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
#plan{margin-top:12px;padding:12px;background:#1f1a16;border:1px solid #6fbfa3;border-radius:4px}
#plan h3{margin:0 0 8px;font-size:15px;letter-spacing:.06em}
#plan p{margin:4px 0;font-size:13px}
#plan details{border-top:1px solid #2a231d;padding:5px 0;font-size:13px}
#plan summary{cursor:pointer}
#plan summary b{display:inline-block;min-width:210px}
#plan ul{margin:6px 0 2px 18px;padding:0;list-style:none;max-height:220px;overflow:auto}
#plan li{padding:1px 0}
#plan .was{color:#8a7f72}
#plan .warn{color:#e0a458}
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
 .fhead{flex-wrap:wrap;row-gap:6px}
 .fhead .fname{flex:1 1 60%}
}
</style></head><body><main>
<header><h1>APOTHECA</h1><svg class="mark" viewBox="39.390 125.100 22.820 2.141" role="img" aria-label="Shapetaker"><path fill="currentColor" d="m 40.636498,126.14073 c -0.16369,-0.0705 -0.285044,-0.10724 -0.434622,-0.14111 -0.206023,-0.048 -0.400756,-0.127 -0.400756,-0.29351 0,-0.0169 0.0028,-0.0339 0.0056,-0.0508 0.03104,-0.15804 0.220134,-0.26811 0.437445,-0.27093 0.0028,0 0.0028,0 0.0056,0 0.180622,0 0.372533,0.0621 0.510822,0.2032 l 0.174977,-0.16369 c -0.121355,-0.13547 -0.386644,-0.27375 -0.680155,-0.27375 -0.0056,0 -0.01129,0.003 -0.01693,0.003 -0.454377,0.006 -0.660399,0.30198 -0.680155,0.48824 -0.0028,0.0169 -0.0056,0.0367 -0.0056,0.0536 0,0.2286 0.180622,0.42898 0.462844,0.50518 0.183444,0.0508 0.333022,0.079 0.530577,0.16087 0.09596,0.0423 0.194733,0.11571 0.194733,0.24553 0,0.0254 -0.0028,0.0536 -0.01129,0.0819 -0.05644,0.1778 -0.253999,0.254 -0.437444,0.26246 -0.01129,0.003 -0.0254,0.003 -0.03669,0.003 -0.259644,0 -0.533399,-0.11571 -0.62371,-0.22577 l -0.191911,0.15522 c 0.149577,0.19473 0.536222,0.30762 0.82691,0.30762 0.285045,0 0.612422,-0.13547 0.697089,-0.43744 0.01693,-0.0508 0.02258,-0.1016 0.02258,-0.14676 0,-0.2032 -0.124177,-0.37253 -0.349955,-0.46567 z m 2.473677,-0.94826 v 0.76764 h -1.114777 v -0.76764 H 41.75551 v 1.9558 h 0.239888 V 126.2 h 1.114777 v 0.94827 h 0.239889 v -1.9558 z m 1.948742,-0.0395 -1.018822,1.99531 h 0.268111 l 0.333022,-0.65194 h 0.832555 l 0.333022,0.65194 h 0.270933 z m -0.299156,1.11477 0.299155,-0.58702 0.299155,0.58702 z m 3.385253,-0.92851 c -0.112888,-0.0903 -0.259645,-0.14675 -0.417688,-0.14675 h -0.959555 v 1.9558 h 0.239888 v -0.61807 h 0.719666 c 0.369711,0 0.668866,-0.29915 0.668866,-0.66886 0,-0.21167 -0.09878,-0.39794 -0.251177,-0.52212 z m 0.01129,0.52212 c 0,0.23706 -0.191911,0.42897 -0.428977,0.42897 h -0.719666 v -0.85513 h 0.719666 c 0.237065,0 0.428977,0.19191 0.428977,0.42616 z m 1.231901,1.04704 v -0.6604 h 1.03011 v -0.23989 h -1.03011 v -0.57291 h 1.219199 v -0.24271 h -1.459088 v 1.9558 h 1.498599 v -0.23989 z m 1.849967,-1.71591 v 0.24271 h 0.694267 v 1.71309 h 0.239888 v -1.71309 h 0.694266 v -0.24271 z m 2.865963,-0.0395 -1.018821,1.99531 h 0.26811 l 0.333022,-0.65194 h 0.832555 l 0.333022,0.65194 h 0.270933 z m -0.299155,1.11477 0.299155,-0.58702 0.299156,0.58702 z m 2.727676,-0.0931 0.97931,-0.98213 h -0.341488 l -0.809977,0.80998 -0.307622,-0.30763 v -0.50235 h -0.242711 v 1.9558 h 0.242711 v -1.11478 l 1.114776,1.11478 h 0.341488 z m 1.920522,0.73378 v -0.6604 h 1.03011 v -0.23989 h -1.03011 v -0.57291 h 1.219199 v -0.24276 h -1.459089 v 1.9558 h 1.498599 v -0.23989 z m 3.706988,0.23989 -0.451555,-0.44874 c 0.231422,-0.14393 0.383822,-0.39793 0.383822,-0.69144 0,-0.44873 -0.364067,-0.81562 -0.815622,-0.81562 h -0.812799 v 1.95862 h 0.239889 v -1.13453 l 1.13171,1.13453 z m -0.307622,-1.14018 c 0,0.20038 -0.110066,0.40922 -0.318911,0.51647 l -0.829732,-0.82974 v -0.25964 h 0.57291 c 0.316089,0 0.575733,0.25682 0.575733,0.57291 z"/></svg><span class="meta" id="version"></span><span class="meta" id="free"></span><a class="btn" id="bench" href="#" target="_blank" title="Times the engine on this device. Stop the song first. Takes about four minutes; the rows come as they are timed.">Speed test</a><a class="btn" id="design" href="#">Open the sound designer</a></header>

<div id="drop">
<div class="line"><button class="main" id="pick">Choose files</button><button id="pickFolder">Choose a folder</button><span class="dim">or drop files or a whole folder on the page.</span></div>
<div class="line" style="margin-top:12px"><label for="into">Recordings go in</label><select id="into"></select><input id="intoNew" class="hid" type="text" maxlength="49" placeholder="new folder, or folder/inner"></div>
<p style="margin:10px 0 0;font-size:13px">Recordings (.wav), chord packs (.json), songs (.song). 30 MB at most each. Every file is checked before it is kept.</p>
<input id="files" class="hid" type="file" multiple accept=".wav,.json,.song,audio/wav,audio/x-wav">
<input id="dir" class="hid" type="file" webkitdirectory multiple>
<div id="plan" class="hid"></div>
<ul id="log"></ul>
<div class="line hid" id="logEnd" style="margin-top:8px"><button id="clear">Clear the list</button></div>
</div>

<h2>Recordings <span class="count" id="nSamples"></span><input id="find" type="text" placeholder="find"></h2>
<div class="line hid" id="bulk"><b id="nPicked"></b><label for="bulkTo">move to</label><select id="bulkTo"></select><input id="bulkNew" class="hid" type="text" maxlength="49" placeholder="new folder, or folder/inner"><button id="bulkMove" class="main">Move</button><button id="bulkDelete" class="del">Delete</button><button id="bulkNone">Choose none</button></div>
<div id="recordings"></div>
<h2>Bounces <span class="count" id="nBounces"></span></h2><p>Your songs as sound files, and as MIDI.</p><div class="panel"><ul id="bounces"></ul></div>
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
$('bench').href = base + '/bench';

function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
function btn(text, act, cls) { const b = el('button', cls, text); b.dataset.act = act; return b; }
function urlOf(kind, name) { return base + '/' + kind + '/' + name.split('/').map(encodeURIComponent).join('/'); }
function split(name) { const i = name.lastIndexOf('/'); return i < 0 ? ['', name] : [name.slice(0, i), name.slice(i + 1)]; }
function sizeText(n) { return n == null ? '' : n < 1024 ? n + ' B' : n < 1048576 ? Math.round(n / 1024) + ' KB' : (n / 1048576).toFixed(1) + ' MB'; }
function bytes(s) { return new TextEncoder().encode(s).length; }
// The same rules the handheld holds a name to. '' if it is fine, or what is wrong with it.
function wrong(name, longest) {
  if (!name) return 'it needs a name';
  if (bytes(name) > longest) return 'name too long: ' + longest + ' letters at most';
  if (/^[. ]/.test(name) || name.includes('..') || /[\x00-\x1f\x7f\/\\:"<>|?*]/.test(name)) return 'a name cannot start with a dot or a space, or have .. / \\ : " < > | ? * in it';
  return '';
}
// A folder: one name, or one inside another ("drums/kicks"), two deep and no further.
function wrongFolder(f) {
  const parts = f.split('/');
  if (parts.length > 2) return 'folders go two deep at most';
  for (const part of parts) if (wrong(part, 24)) return wrong(part, 24);
  return '';
}
// A recording's whole name, folders and all but not its ending, is kept in 63 letters.
function tooLong(folder, file) { return folder && bytes(folder + '/' + file.replace(/\.wav$/i, '')) > 63 ? 'too long with its folders: 63 letters at most in all' : ''; }
function folders() {
  const s = new Set(all.folders || []);
  for (const n of all.samples) { const f = split(n)[0]; if (f) s.add(f); }
  for (const f of made) s.add(f);
  for (const f of [...s]) if (f.includes('/')) s.add(split(f)[0]); // the folder a folder is in
  return [...s].sort((a, b) => a.localeCompare(b));
}
// A folder with nothing in it: no recording, no folder inside it. (One with a file that is no recording is refused by the handheld.)
function emptyFolder(f) { return f && !all.samples.some(n => split(n)[0] == f || n.startsWith(f + '/')) && !folders().some(g => g.startsWith(f + '/')); }
async function removeFolder(f) {
  if (!(all.folders || []).includes(f)) { made.delete(f); open.delete(f); if (into == f) into = ''; draw(); return; } // named here, never on the card
  let r;
  try { const got = await fetch(base + '/folders/' + f.split('/').map(encodeURIComponent).join('/'), {method: 'DELETE'}); r = {ok: got.ok, text: (await got.text()).trim()}; }
  catch (e) { r = {ok: false, text: 'no answer from the handheld'}; }
  note('folder ' + f, r.ok ? r.text : r.text.startsWith('not deleted') ? r.text : 'not deleted: ' + r.text, r.ok ? 'ok' : 'no');
  if (r.ok) { made.delete(f); open.delete(f); if (into == f) into = ''; }
  list();
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
  return {li, state: s, what: w};
}
$('clear').onclick = () => { for (const li of [...$('log').children]) if (!li.classList.contains('busy')) li.remove(); if (!$('log').children.length) $('logEnd').classList.add('hid'); };

// ---- what is on the handheld ----
async function list() {
  try { const r = await fetch(base + '/list'); if (!r.ok) { note('the handheld', (await r.text()).trim(), 'no'); return; } all = await r.json(); } catch (e) { note('the handheld', 'no answer: is the WiFi page still on?', 'no'); return; }
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
  if (kind == 'samples' || (kind == 'bounces' && !/\.mid$/i.test(name))) { // (a MIDI file: download only, nothing to play)
    const on = playing == kind + '/' + name;
    li.append(btn(on ? 'stop' : 'play', 'play', on ? 'play on' : 'play'));
  }
  const acts = el('span', 'acts'), get = el('a', 'btn', 'download');
  get.href = urlOf(kind, name); get.download = label;
  acts.append(get);
  if (kind == 'samples') { li.draggable = true; acts.append(btn('rename', 'rename'), btn('move', 'move')); }
  acts.append(btn('delete', 'delete', 'del'));
  li.append(el('span', 'name', label.replace(/\.(wav|song|json|mid)$/i, '')), el('span', 'size', sizeText(size)), acts); // the title, without its ending
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
    if (emptyFolder(f)) head.append(btn('delete folder', 'delfolder', 'del')); // a folder stays when its last recording goes: deleted only on purpose
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
  if (f && wrongFolder(f)) { note('folder ' + f, 'refused: ' + wrongFolder(f), 'no'); return; }
  const names = [...picked].sort();
  for (const from of names) {
    const to = (f ? f + '/' : '') + split(from)[1];
    if (to == from) { picked.delete(from); continue; } // already there
    if (tooLong(f, split(from)[1])) { note(from + ' to ' + to, 'refused: ' + tooLong(f, split(from)[1]), 'no'); continue; }
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
    const bad = (f && wrongFolder(f) && 'folder: ' + wrongFolder(f)) || wrong(file.name, 60) || tooLong(f, file.name) || (file.size > 30 * 1048576 && 'too big: 30 MB at most');
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
    const job = queue.shift(), lot = job.lot;
    if (lot && lot.stopped) continue;
    if (lot) lot.n.what.textContent = lot.title + ': ' + (lot.done + lot.failed + 1) + ' of ' + lot.total + ', ' + job.name;
    job.n.state.textContent = 'sending';
    const r = await new Promise(done => {
      const x = new XMLHttpRequest();
      x.open('PUT', job.url);
      x.upload.onprogress = e => { if (!e.lengthComputable) return; const p = lot ? Math.round(100 * (lot.sent + e.loaded) / (lot.bytes || 1)) : Math.round(100 * e.loaded / e.total); job.fill.style.width = p + '%'; job.n.state.textContent = lot || p < 100 ? 'sending ' + p + '%' : 'checking'; };
      x.onload = () => done({ok: x.status == 200, text: x.responseText.trim()});
      x.onerror = x.ontimeout = () => done({ok: false, text: 'no answer from the handheld'});
      x.send(job.file);
    });
    if (lot) { // one row for the whole folder; only what was refused gets a row of its own
      lot.sent += job.file.size;
      if (r.ok) lot.done++; else { lot.failed++; note(job.name, 'refused: ' + r.text, 'no'); }
      if (r.text == 'no answer from the handheld' && ++lot.silent >= 3) { lot.stopped = true; lot.failed += lot.total - lot.done - lot.failed; } // the WiFi page went off: not a thousand more tries
      else if (r.ok) lot.silent = 0;
      if (lot.done + lot.failed < lot.total) continue;
      lot.n.what.textContent = lot.title;
      lot.n.li.className = lot.failed ? 'no' : 'ok';
      lot.n.state.textContent = lot.done + ' saved' + (lot.failed ? ', ' + lot.failed + (lot.stopped ? ' not sent: no answer from the handheld' : ' refused') : '');
      job.fill.style.width = '100%';
      if (lot.stop) lot.stop.remove();
      continue;
    }
    job.fill.style.width = '100%';
    job.n.li.className = r.ok ? 'ok' : 'no';
    job.n.state.textContent = r.ok ? r.text + (job.replaces && r.text == 'saved' ? ' (in place of the old one)' : '') : 'refused: ' + r.text;
  }
  busy = false;
  list();
}
function target() { // the folder chosen at the top, or null if it is not a name that will do
  if ($('into').value != NEW) return $('into').value;
  const f = $('intoNew').value.trim(), bad = f ? wrongFolder(f) : wrong(f, 24);
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

// ---- a whole folder: every recording in it, however deep, each into ONE folder here ----
// Each folder there becomes ONE folder here, so "Drums/Kicks/808.wav" arrives as folder
// "Kicks", file "808". (The handheld takes folders two deep since 2026-11-14; the import does not
// make them yet.) Names are made to fit, and nothing is sent until it has been shown.
function tidy(s) { return s.replace(/[\x00-\x1f\x7f\/\\:"<>|?*]/g, ' ').replace(/\.\.+/g, '.').replace(/\s+/g, ' ').replace(/^[. ]+/, '').trim(); }
function fit(s, room) { // to `room` bytes. Whole words go first, from the middle: a pack's names differ at the start and the end ("Kick ... 01")
  s = tidy(s);
  if (bytes(s) <= room) return s;
  const words = s.split(/(?<=[ _\-.])/); // each word keeps the mark after it
  while (words.length > 2 && bytes(words.join('')) > room) words.splice(Math.max(1, words.length - 2), 1);
  s = tidy(words.join(''));
  if (bytes(s) <= room) return s;
  const c = [...s], keep = Math.min(8, Math.floor(room / 3)), end = c.slice(c.length - keep).join(''); // still too long: letters go, the start and the end stay
  let head = c.slice(0, c.length - keep);
  while (head.length && bytes(head.join('').trimEnd() + end) > room) head.pop();
  return tidy(head.join('').trimEnd() + end);
}
// entries: [{file, path}], path as on the computer ("Pack/Drums/Kicks/808.wav"). loose: the folder
// here for files that came with no folder of their own. have: the names already on the handheld.
function planFolders(entries, loose, have) {
  const p = {jobs: [], other: 0, big: [], changed: 0, replaces: 0, bytes: 0, folders: new Map()};
  const byDir = new Map();
  for (const e of entries) {
    const parts = e.path.split('/').filter(Boolean), file = parts.pop() || '', dir = parts.join('/');
    if (!/\.wav$/i.test(file) || file.startsWith('._')) { p.other++; continue; }
    if (e.file.size > 30 * 1048576) { p.big.push(e.path); continue; }
    if (!byDir.has(dir)) byDir.set(dir, []);
    byDir.get(dir).push({file: e.file, was: file, path: e.path});
  }
  // A folder here for each folder there: its own name, or with its parent's in front where two share one.
  const dirs = [...byDir.keys()].sort(), last = d => fit(d.split('/').pop(), 24) || 'imported', count = new Map();
  for (const d of dirs) if (d) count.set(last(d).toLowerCase(), (count.get(last(d).toLowerCase()) || 0) + 1);
  const used = new Set(), named = new Map();
  for (const d of dirs) {
    if (!d) { named.set(d, loose); continue; }
    let name = last(d);
    if (count.get(name.toLowerCase()) > 1 && d.includes('/')) {
      const parent = d.split('/').slice(-2)[0], room = 24 - 1 - bytes(name);
      if (room >= 3) name = tidy(fit(parent, room) + ' ' + name);
    }
    for (let n = 2, plain = name; used.has(name.toLowerCase()); n++) name = fit(plain, 24 - 1 - String(n).length) + ' ' + n;
    used.add(name.toLowerCase()); named.set(d, name);
  }
  const taken = new Map(); // folder -> the names in it, in lower case (a card does not tell Kick from kick)
  const old = new Set((have || []).map(n => n.toLowerCase()));
  for (const d of dirs) {
    const folder = named.get(d), key = folder.toLowerCase(), room = folder ? 32 : 56;
    if (!taken.has(key)) taken.set(key, new Set());
    const names = taken.get(key);
    if (!p.folders.has(folder)) p.folders.set(folder, {from: [], files: []});
    const row = p.folders.get(folder);
    row.from.push(d || '(loose files)');
    for (const f of byDir.get(d).sort((a, b) => a.was.localeCompare(b.was))) {
      const stem = f.was.replace(/\.wav$/i, '');
      let name = fit(stem, room) || 'recording';
      for (let n = 2, plain = name; names.has(name.toLowerCase()); n++) name = fit(plain, room - 1 - String(n).length) + ' ' + n;
      names.add(name.toLowerCase());
      const to = (folder ? folder + '/' : '') + name + '.wav', differs = name != stem, replaces = old.has(to.toLowerCase());
      p.changed += differs; p.replaces += replaces; p.bytes += f.file.size;
      row.files.push({was: f.was, name, differs});
      p.jobs.push({file: f.file, to, folder});
    }
  }
  return p;
}
function showPlan(p, title) {
  const box = $('plan'), n = p.jobs.length, mb = p.bytes / 1048576;
  box.textContent = '';
  box.append(el('h3', '', title + ': ' + n + ' recording' + (n == 1 ? '' : 's') + ' into ' + p.folders.size + ' folder' + (p.folders.size == 1 ? '' : 's') + ', ' + sizeText(p.bytes)));
  if (all.free >= 0 && mb > all.free) box.append(el('p', 'warn', 'TOO BIG FOR THE CARD: ' + sizeText(p.bytes) + ' to send, ' + all.free + ' MB free. Choose a smaller folder.'));
  else if (all.free >= 0) box.append(el('p', '', 'Free on the card: ' + (all.free >= 1024 ? (all.free / 1024).toFixed(1) + ' GB' : all.free + ' MB') + '.'));
  box.append(el('p', '', 'Sub-folders become one folder each here.'));
  if (p.changed) box.append(el('p', 'warn', p.changed + ' name' + (p.changed == 1 ? ' is' : 's are') + ' shortened or tidied to fit (32 letters at most). Open a folder below to see them.'));
  if (p.replaces) box.append(el('p', 'warn', p.replaces + ' will take the place of a recording of the same name already on the handheld.'));
  if (p.other) box.append(el('p', '', p.other + ' other file' + (p.other == 1 ? '' : 's') + ' left out (not .wav).'));
  if (p.big.length) box.append(el('p', 'warn', p.big.length + ' left out, over 30 MB: ' + p.big.slice(0, 5).join(', ') + (p.big.length > 5 ? ', ...' : '')));
  for (const [folder, row] of [...p.folders].sort((a, b) => a[0].localeCompare(b[0]))) {
    const d = el('details'), sum = el('summary'), changed = row.files.filter(f => f.differs).length;
    sum.append(el('b', '', folder || 'UNSORTED'), el('span', '', row.files.length + ' file' + (row.files.length == 1 ? '' : 's') + (changed ? ', ' + changed + ' renamed' : '')), el('span', 'was', '  from ' + row.from.join(', ')));
    d.append(sum);
    d.addEventListener('toggle', () => { // a folder's files are drawn when it is opened: thousands stay quick
      if (!d.open || d.querySelector('ul')) return;
      const ul = el('ul');
      for (const f of row.files) { const li = el('li', '', f.name); if (f.differs) li.append(el('span', 'was', '   was ' + f.was)); ul.append(li); }
      d.append(ul);
    });
    box.append(d);
  }
  const line = el('div', 'line'), go = el('button', 'main', 'Send ' + n), no = el('button', '', 'Cancel');
  line.style.marginTop = '10px';
  go.id = 'planGo'; no.id = 'planNo';
  go.disabled = !n || (all.free >= 0 && mb > all.free);
  go.onclick = () => { box.classList.add('hid'); sendPlan(p, title); };
  no.onclick = () => { box.classList.add('hid'); box.textContent = ''; };
  line.append(go, no); box.append(line);
  box.classList.remove('hid');
  box.scrollIntoView({block: 'nearest'});
}
function sendPlan(p, title) {
  if (!p.jobs.length) return;
  const n = note(title, 'waiting', 'busy'), bar = el('div', 'bar'), fill = el('i'), stop = el('button', '', 'Stop');
  bar.append(fill); n.li.append(bar, stop);
  const lot = {title, total: p.jobs.length, done: 0, failed: 0, silent: 0, bytes: p.bytes, sent: 0, n, stop, stopped: false};
  stop.onclick = () => { // what has gone stays; the rest is not sent
    if (lot.stopped) return;
    lot.stopped = true;
    n.what.textContent = title; n.li.className = 'no'; n.state.textContent = lot.done + ' saved, stopped with ' + (lot.total - lot.done - lot.failed) + ' not sent'; stop.remove();
  };
  for (const j of p.jobs) { queue.push({url: urlOf('samples', j.to), file: j.file, name: j.to, n, fill, lot}); if (j.folder) made.add(j.folder); }
  if (!busy) run();
}
// What was dropped, read to the bottom of every folder in it.
async function readDropped(roots) {
  const out = [];
  async function walk(entry, path) {
    if (entry.isFile) out.push({file: await new Promise((ok, no) => entry.file(ok, no)), path: path + entry.name});
    else if (entry.isDirectory) {
      const reader = entry.createReader();
      for (;;) { const some = await new Promise((ok, no) => reader.readEntries(ok, no)); if (!some.length) break; for (const e of some) await walk(e, path + entry.name + '/'); }
    }
  }
  for (const r of roots) await walk(r, '');
  return out;
}
function looseFolder() { return $('into').value == NEW ? ($('intoNew').value.trim() && !wrong($('intoNew').value.trim(), 24) ? $('intoNew').value.trim() : '') : $('into').value; }
$('pickFolder').onclick = () => $('dir').click();
$('dir').onchange = () => {
  const files = [...$('dir').files];
  if (files.length) showPlan(planFolders(files.map(f => ({file: f, path: f.webkitRelativePath || f.name})), looseFolder(), all.samples), 'folder ' + (files[0].webkitRelativePath || '').split('/')[0]);
  $('dir').value = '';
};

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
  // A folder among what was dropped: all of it is read, planned and shown before anything is sent.
  const roots = [...(e.dataTransfer.items || [])].map(i => i.webkitGetAsEntry && i.webkitGetAsEntry()).filter(Boolean);
  if (roots.some(r => r.isDirectory)) {
    const loose = sec ? sec.dataset.folder : looseFolder(), dirs = roots.filter(r => r.isDirectory);
    readDropped(roots).then(got => showPlan(planFolders(got, loose, all.samples), dirs.length == 1 ? 'folder ' + dirs[0].name : dirs.length + ' folders'), () => note('the folder', 'could not be read by the browser', 'no'));
    return;
  }
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
  name.type = fresh.type = 'text'; name.value = file.slice(0, dot); name.maxLength = 56; fresh.maxLength = 49; fresh.placeholder = 'new folder, or folder/inner';
  folderSelect(sel, f);
  sel.onchange = () => { fresh.classList.toggle('hid', sel.value != NEW); if (sel.value == NEW) fresh.focus(); };
  const go = () => {
    const folder = sel.value == NEW ? fresh.value.trim() : sel.value, to = name.value.trim() + file.slice(dot);
    const bad = (folder && wrongFolder(folder) && 'folder: ' + wrongFolder(folder)) || wrong(to, 60) || tooLong(folder, to);
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
  if (act == 'delfolder') { // asks once more, there in its heading
    t.textContent = 'delete it'; t.className = 'sure'; t.dataset.act = 'surefolder';
    const keep = btn('keep', 'keepfolder'); t.after(keep);
    setTimeout(() => { if (t.isConnected && t.dataset.act == 'surefolder') { t.textContent = 'delete folder'; t.className = 'del'; t.dataset.act = 'delfolder'; keep.remove(); } }, 6000);
    return;
  }
  if (act == 'keepfolder') { const d = t.previousSibling; d.textContent = 'delete folder'; d.className = 'del'; d.dataset.act = 'delfolder'; t.remove(); return; }
  if (act == 'surefolder') { removeFolder(sec.dataset.folder); return; }
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
// (for "folder/name" or "folder/inner/name") not inside a link either. Only such a file is deleted or moved.
bool realFile(const std::string& folder, const std::string& name) {
    struct stat info;
    for (std::size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1))
        if (lstat((folder + "/" + name.substr(0, slash)).c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return false;
    return lstat((folder + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// The folders a recording's name asks for, made if they are not there (the outer one first), and
// each one made just now kept in `made`. False if one of them is not a folder that can be used (a link, a file).
// A folder that loses its last recording is KEPT (owner, 2026-11-15: "Keep them"), as on the
// handheld: a folder goes only when it is deleted on purpose (DELETE /folders/...).
bool foldersMade(const std::string& folder, const std::string& name, std::vector<std::string>& made) {
    for (std::size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1)) {
        const std::string inside = folder + "/" + name.substr(0, slash);
        struct stat there;
        if (mkdir(inside.c_str(), 0755) == 0) made.push_back(inside); // (fine if it is already there)
        if (lstat(inside.c_str(), &there) != 0 || !S_ISDIR(there.st_mode)) return false;
    }
    return true;
}
// What was refused does not leave the folders made for it on the way (the inner first); a folder
// that was there before is never touched.
void dropMade(const std::vector<std::string>& made) {
    for (std::size_t i = made.size(); i-- > 0;) rmdir(made[i].c_str()); // does nothing while anything is in it
}

// A folder of the recordings by its whole name ("kicks", "drums/kicks"), two deep at most, each part
// a plain name: "" if it is not one this takes.
std::string safeFolderPath(const std::string& fromAddress) {
    const std::string name = unescape(fromAddress);
    std::size_t parts = 0;
    for (std::size_t from = 0; from <= name.size();) {
        const std::size_t end = std::min(name.find('/', from), name.size());
        if (!plainName(name.substr(from, end - from), 24) || ++parts > 2) return "";
        from = end + 1;
    }
    return name;
}

// A song deleted from the page goes where one deleted on the handheld goes:
// into "deleted" in the song folder, with its revs (binSong in song_file.cpp).
// It is deleted if the song itself has gone from the folder (a rev that would not move stays behind).
bool binSongFile(const std::string& folder, const std::string& file) {
    binSong(folder, file.substr(0, file.size() - 5));
    struct stat info;
    return lstat((folder + "/" + file).c_str(), &info) != 0;
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

// A recording may be in a folder, or in a folder in a folder: "kicks/808.wav",
// "drums/kicks/808.wav" (two levels and no further). Each folder and the file
// are held to the same rules, and the whole of it, without its ending, must be a
// name a song can keep (sampleNameOk: kSampleNameRoom). "" if it is not such a name.
std::string safeSamplePath(const std::string& fromAddress) {
    const std::string name = unescape(fromAddress);
    const std::size_t slash = name.rfind('/');
    if (slash == std::string::npos) return safeFileName(fromAddress, ".wav");
    const std::string folders = name.substr(0, slash), file = name.substr(slash + 1);
    for (std::size_t from = 0; from <= folders.size();) {
        const std::size_t end = std::min(folders.find('/', from), folders.size());
        if (!plainName(folders.substr(from, end - from), 24)) return "";
        from = end + 1;
    }
    if (file.size() <= 4 || !plainName(file, 60)) return ""; // (as long as a loose one: the handheld carries any recording into a folder, and the whole is held below)
    std::string end = file.substr(file.size() - 4);
    for (char& c : end) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return end == ".wav" && sampleNameOk(name.substr(0, name.size() - 4)) ? name : ""; // (which is also what holds it to two folders)
}

namespace {
std::vector<std::string> plainFoldersIn(const std::string& folder) {
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
} // namespace

// The folders inside the recordings, empty ones too: each, then the folders inside it ("drums", "drums/kicks").
std::vector<std::string> foldersIn(const std::string& folder) {
    std::vector<std::string> folders;
    for (const std::string& inside : plainFoldersIn(folder)) {
        folders.push_back(inside);
        for (const std::string& under : plainFoldersIn(folder + "/" + inside)) folders.push_back(inside + "/" + under);
    }
    return folders;
}

// The recordings: the loose ones, then those in each folder as "folder/name.wav" (and "folder/inner/name.wav").
std::vector<std::string> recordingsIn(const std::string& folder) {
    std::vector<std::string> names = filesEnding(folder, ".wav");
    for (const std::string& inside : foldersIn(folder))
        for (const std::string& file : filesEnding(folder + "/" + inside, ".wav"))
            if (file.size() <= 60 && sampleNameOk(inside + "/" + file.substr(0, file.size() - 4))) names.push_back(inside + "/" + file); // (only what the page could ask for by name)
    return names;
}

bool Transfer::renameBroken = false;
std::string Transfer::benchCommand;

std::string Transfer::selfCommand(const std::string& args) {
    char path[4096];
    const ssize_t got = readlink("/proc/self/exe", path, sizeof path - 1);
    if (got <= 0) return "";
    std::string quoted = "'";
    for (const char c : std::string(path, static_cast<std::size_t>(got))) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "' " + args;
}

void Transfer::noteChange(const std::string& fromFile, const std::string& toFile) {
    const auto bare = [](const std::string& file) { return file.empty() ? file : file.substr(0, file.size() - 4); }; // every one ends ".wav"
    std::lock_guard<std::mutex> lock(changesMutex_);
    if (changes_.size() < 4096) changes_.push_back({bare(fromFile), bare(toFile)}); // nobody is reading (it cannot happen while the program runs): do not grow for ever
}

std::vector<Transfer::Change> Transfer::takeChanges() {
    std::lock_guard<std::mutex> lock(changesMutex_);
    std::vector<Change> taken;
    taken.swap(changes_);
    return taken;
}

int Transfer::lockoutMs = 60000;
int Transfer::requestMs = 120000;

void Transfer::setOpenSong(const std::string& name) {
    std::lock_guard<std::mutex> hold(openMutex_);
    openSong_ = name;
}

std::string Transfer::folderFor(const std::string& kind) const { return kind == "samples" ? samples_ : kind == "songs" ? songs_ : kind == "chords" ? chords_ : kind == "bounces" ? bounces_ : ""; }

bool Transfer::start(const std::string& sampleFolder, const std::string& songFolder, const std::string& chordFolder, int port, unsigned seed) {
    stop();
    samples_ = sampleFolder;
    songs_ = songFolder;
    chords_ = chordFolder;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    fcntl(fd, F_SETFD, FD_CLOEXEC); // a program started from here (a speed test, the WiFi's) does not keep port 80
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
    wrong_ = 0; // a new number: the count starts again
    lastWrongMs_ = 0;
    lockedUntilMs_ = 0;
    stopping_.store(false);
    listening_ = fd;
    thread_ = std::thread([this] { serve(); });
    return true;
}

void Transfer::stop() {
    if (listening_ < 0) return;
    stopping_.store(true);
    {
        // A request under way ends now: its receives and sends come back at once, and a speed test
        // is ended. (The server looks at `stopping_` five times a second besides.)
        std::lock_guard<std::mutex> lock(servingMutex_);
        if (serving_ >= 0) shutdown(serving_, SHUT_RDWR);
        if (const int group = benchGroup_.load()) kill(-group, SIGTERM);
    }
    if (thread_.joinable()) thread_.join();
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
    return host + (port_ == 80 ? "" : ":" + std::to_string(port_)) + "/" + pin_; // (a browser asks port 80 itself)
}

void Transfer::serve() {
    while (!stopping_.load()) {
        pollfd waiting = {listening_, POLLIN, 0};
        if (poll(&waiting, 1, 200) <= 0) continue;
        const int connection = accept(listening_, nullptr, nullptr);
        if (connection < 0) continue;
        fcntl(connection, F_SETFD, FD_CLOEXEC); // (not handed to a speed test or the WiFi's programs)
        timeval patience = {5, 0}; // a browser that goes quiet does not hold the door
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof patience);
        {
            std::lock_guard<std::mutex> lock(servingMutex_);
            serving_ = connection;
            if (stopped()) shutdown(connection, SHUT_RDWR); // `stop` came between: nothing to wait for
        }
        try {
            answer(connection);
        } catch (const std::bad_alloc&) { // out of RAM mid-request: the door shuts, the song plays on
        } catch (...) {
        }
        shutdown(connection, SHUT_WR);
        // Let the browser finish reading before the door shuts: a second at most and 64 KB at most,
        // so a client that keeps sending cannot hold the server (or `stop`) for good.
        timeval brief = {0, 200000};
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &brief, sizeof brief);
        const auto drainFrom = std::chrono::steady_clock::now();
        char rest[512];
        std::size_t drained = 0;
        while (!stopped() && drained < 65536 && std::chrono::steady_clock::now() - drainFrom < std::chrono::seconds(1)) {
            const ssize_t got = recv(connection, rest, sizeof rest, 0);
            if (got <= 0) break;
            drained += static_cast<std::size_t>(got);
        }
        {
            std::lock_guard<std::mutex> lock(servingMutex_);
            serving_ = -1;
            close(connection);
        }
    }
}

void Transfer::answer(int fd) {
    // One request may take this long altogether (a trickle of bytes must not hold the door forever).
    const auto started = std::chrono::steady_clock::now();
    const auto tooSlow = [&] {
        return stopped() || std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() > requestMs;
    };
    // The request line and headers: up to 8 KB, ending in a blank line.
    std::string head;
    std::size_t end = std::string::npos;
    char block[2048];
    while (end == std::string::npos && head.size() < 8192 && !tooSlow()) {
        const ssize_t got = recv(fd, block, sizeof block, 0);
        if (got <= 0) return;
        head.append(block, static_cast<std::size_t>(got));
        end = head.find("\r\n\r\n");
    }
    if (tooSlow()) return;
    if (end == std::string::npos) return reply(fd, "431 Too Much", "text/plain", "too much before the file\n");
    std::string body = head.substr(end + 4); // the start of the file, if one is coming
    head.resize(end);

    const std::size_t space = head.find(' '), space2 = head.find(' ', space + 1);
    if (space == std::string::npos || space2 == std::string::npos) return reply(fd, "400 Bad Request", "text/plain", "not a request\n");
    const std::string method = head.substr(0, space);
    std::string path = head.substr(space + 1, space2 - space - 1);
    if (const std::size_t query = path.find('?'); query != std::string::npos) path.resize(query);

    // The number from the device's screen comes first, or nothing is said. Too many wrong ones
    // (from anyone: guessing is guessing) and nothing is said to anyone for a minute.
    const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (nowMs < lockedUntilMs_) return reply(fd, "429 Too Many", "text/plain", kTooManyWrong);
    const std::string door = "/" + pin_;
    if (path.compare(0, door.size(), door) != 0 || (path.size() > door.size() && path[door.size()] != '/')) {
        // Only a number counts as a try: a browser asks for /favicon.ico by itself.
        const std::size_t ends = path.find('/', 1);
        const std::string first = path.substr(1, ends == std::string::npos ? std::string::npos : ends - 1);
        // A right number in between does not start the count again (the owner's page asks with it
        // all the time, and a guesser beside it would never be stopped); a quiet minute does. So
        // the most tries are kWrongMost a minute, either way.
        if (!first.empty() && first.find_first_not_of("0123456789") == std::string::npos) {
            if (nowMs - lastWrongMs_ >= lockoutMs) wrong_ = 0;
            lastWrongMs_ = nowMs;
            if (++wrong_ >= kWrongMost) {
                wrong_ = 0;
                lockedUntilMs_ = nowMs + lockoutMs;
                return reply(fd, "429 Too Many", "text/plain", kTooManyWrong);
            }
        }
        return reply(fd, "403 Forbidden", "text/plain", "Open the address shown on the Apotheca screen, with its four-digit number.\n");
    }
    path = path.substr(door.size());

    if (method == "GET" && (path.empty() || path == "/")) return reply(fd, "200 OK", "text/html; charset=utf-8", kPage);
    if (method == "GET" && path == "/list") {
        const std::vector<std::string> samples = recordingsIn(samples_), songs = filesEnding(songs_, ".song"), chords = filesEnding(chords_, ".json");
        std::vector<std::string> bounces = filesEnding(bounces_, ".wav");
        for (const std::string& mid : filesEnding(bounces_, ".mid")) bounces.push_back(mid);
        std::sort(bounces.begin(), bounces.end());
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
        while (body.size() < size && !tooSlow()) {
            const ssize_t got = recv(fd, block, sizeof block, 0);
            if (got <= 0) return;
            body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(size) - body.size()));
        }
        for (char c : body)
            if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return reply(fd, "400 Bad Request", "text/plain", "plain letters only\n");
        return desk_->ask(body) ? reply(fd, "200 OK", "text/plain", "ok\n") : reply(fd, "400 Bad Request", "text/plain", "that makes no sense\n");
    }

    // The speed test: the program run again by itself, only to time things, and what it printed,
    // sent a line at a time as it comes (it runs two minutes or more on the handheld: sent whole at
    // the end, the page stood blank all that while). No length: the closed connection ends it.
    if (method == "GET" && path == "/bench") {
        const char* head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
        std::string first = "Apotheca " + std::string(kVersion) + ", built " + std::string(kBuilt) + "\n" + kBenchSays;
        if (!sendAll(fd, head, std::strlen(head)) || !sendAll(fd, first.data(), first.size())) return;
        const std::string command = benchCommand.empty() ? selfCommand("--bench 2>&1") : benchCommand;
        // Run through /bin/sh in a process group of its own (so `stop` can end it, shell and all),
        // its output read as it comes, and given up on the moment the server is asked to stop.
        int out[2] = {-1, -1};
        pid_t child = 0;
        bool started = !command.empty() && pipe(out) == 0;
        if (started) {
            fcntl(out[0], F_SETFD, FD_CLOEXEC);
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, out[1], 1);
            posix_spawn_file_actions_addclose(&actions, out[1]);
            posix_spawnattr_t attributes;
            posix_spawnattr_init(&attributes);
            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
            posix_spawnattr_setpgroup(&attributes, 0);
            std::string shell = "/bin/sh", dashC = "-c", text = command;
            char* argv[] = {&shell[0], &dashC[0], &text[0], nullptr};
            started = posix_spawn(&child, "/bin/sh", &actions, &attributes, argv, environ) == 0;
            posix_spawn_file_actions_destroy(&actions);
            posix_spawnattr_destroy(&attributes);
            close(out[1]);
            if (!started) close(out[0]);
        }
        if (!started) {
            const char* no = "could not start the speed test\n";
            sendAll(fd, no, std::strlen(no));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(servingMutex_);
            benchGroup_.store(child);
            if (stopped()) kill(-child, SIGTERM); // `stop` came between
        }
        char said[1024];
        std::size_t sent = 0;
        bool listening = true;
        for (;;) { // read to the end even if the page went away, so the run is not left stuck
            pollfd ready = {out[0], POLLIN, 0};
            const int seen = poll(&ready, 1, 200); // (`stop` kills the group: the pipe then ends)
            if (seen == 0 || (seen < 0 && errno == EINTR)) continue;
            const ssize_t n = read(out[0], said, sizeof said);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            if (listening && sent < 65536) listening = sendAll(fd, said, static_cast<std::size_t>(n)), sent += static_cast<std::size_t>(n);
        }
        close(out[0]);
        {
            // Let go of the group before it is reaped (unreaped, its number cannot be given to another).
            std::lock_guard<std::mutex> lock(servingMutex_);
            benchGroup_.store(0);
        }
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        const bool finished = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        const char* end = finished ? "(done)\n" : "(the speed test did not finish)\n";
        if (listening) sendAll(fd, end, std::strlen(end));
        return;
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
        while (body.size() < size && !tooSlow()) {
            const ssize_t got = recv(fd, block, sizeof block, 0);
            if (got <= 0) return;
            body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(size) - body.size()));
        }
        if (!isProgramForThisMachine(body)) return reply(fd, "400 Bad Request", "text/plain", "not an Apotheca program for this handheld\n");
        if (!writeTextFile(program_ + "/apotheca.pending", body)) return reply(fd, "500 Could Not Save", "text/plain", "could not save it: is the card full?\n");
        updates_.fetch_add(1);
        return reply(fd, "200 OK", "text/plain", "sent. Now accept it on the handheld: hold A.\n");
    }

    // Deleting a folder of the recordings: DELETE /folders/<name> ("kicks", "drums/kicks"). Only an
    // EMPTY one, as hold B on the handheld: anything at all in it (a file that is no recording, a
    // folder) and it stays. Not a link, nor a folder inside one.
    if (path.compare(0, 9, "/folders/") == 0) {
        if (method != "DELETE") return reply(fd, "405 Not Allowed", "text/plain", "folders are only deleted here\n");
        const std::string name = safeFolderPath(path.substr(9));
        if (samples_.empty() || name.empty()) return reply(fd, "400 Bad Request", "text/plain", "not a folder name this takes\n");
        struct stat info;
        for (std::size_t slash = name.find('/'); ; slash = name.find('/', slash + 1)) {
            if (lstat((samples_ + "/" + name.substr(0, slash)).c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return reply(fd, "404 Not Found", "text/plain", "no such folder\n");
            if (slash == std::string::npos) break;
        }
        if (rmdir((samples_ + "/" + name).c_str()) != 0)
            return errno == ENOTEMPTY || errno == EEXIST ? reply(fd, "409 Not Empty", "text/plain", "not deleted: it is not empty\n")
                                                         : reply(fd, "500 Could Not Delete", "text/plain", "could not delete it\n");
        received_.fetch_add(1);
        return reply(fd, "200 OK", "text/plain", "deleted\n");
    }

    // /samples/<name>.wav, /songs/<name>.song, /chords/<name>.json
    const std::size_t slash = path.find('/', 1);
    const std::string kind = slash == std::string::npos ? "" : path.substr(1, slash - 1);
    const bool mid = kind == "bounces" && path.size() >= 4 && path.compare(path.size() - 4, 4, ".mid") == 0;
    const std::string ending = kind == "samples" ? ".wav" : kind == "bounces" ? (mid ? ".mid" : ".wav") : kind == "songs" ? ".song" : kind == "chords" ? ".json" : "";
    const std::string folder = folderFor(kind);
    // A recording may be in a folder ("samples/kicks/808.wav"); nothing else may.
    const std::string name = ending.empty() ? "" : kind == "samples" ? safeSamplePath(path.substr(slash + 1)) : safeFileName(path.substr(slash + 1), ending);
    if (folder.empty() || name.empty()) return reply(fd, "400 Bad Request", "text/plain", "not a file name this takes\n");
    const std::string file = folder + "/" + name;

    if (method == "GET") {
        struct stat info;
        if (stat(file.c_str(), &info) == 0 && static_cast<unsigned long long>(info.st_size) > kLargestRead)
            return reply(fd, "413 Too Large", "text/plain", "too big to fetch\n");
        std::string contents;
        if (!readTextFile(file, contents)) return reply(fd, "404 Not Found", "text/plain", "no such file\n");
        return reply(fd, "200 OK", ending == ".wav" ? "audio/wav" : "application/octet-stream", contents); // a browser can play a recording
    }

    // Deleting: only a file that is there, by a name that passed the same rules as one sent.
    if (method == "DELETE") {
        if (!realFile(folder, name)) return reply(fd, "404 Not Found", "text/plain", "no such file\n");
        std::unique_lock<std::mutex> songs(songFiles_, std::defer_lock);
        if (kind == "songs") songs.lock(); // not while the session is going through them
        const bool gone = kind == "songs" ? binSongFile(folder, name) : unlink(file.c_str()) == 0;
        if (kind == "songs") songs.unlock();
        if (!gone) return reply(fd, "500 Could Not Delete", "text/plain", "could not delete it\n");
        if (kind == "samples") noteChange(name, ""); // (its folder stays, empty or not)
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
        std::vector<std::string> made;
        if (!foldersMade(folder, to, made)) {
            dropMade(made); // (what was made on the way does not linger)
            return reply(fd, "400 Bad Request", "text/plain", "not a folder this can use\n");
        }
        // Some cards will not rename a file from one folder into another (it was refused on the
        // handheld's own card). Then it is moved the long way: written again where it goes, and
        // the old one taken away only once the new one is safely there.
        if (renameBroken || std::rename(file.c_str(), target.c_str()) != 0) {
            const int why = renameBroken ? EXDEV : errno;
            std::string contents;
            const bool copied = readTextFile(file, contents) && writeTextFile(target, contents);
            if (!copied || unlink(file.c_str()) != 0) {
                if (copied) unlink(target.c_str()); // the old one would not go: do not leave two
                dropMade(made);                     // a folder made for it just now does not linger
                return reply(fd, "500 Could Not Move", "text/plain", std::string("could not move it: ") + std::strerror(copied ? errno : why) + "\n");
            }
        }
        noteChange(name, to); // (the folder it left stays, empty or not)
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
    while (body.size() < length && !tooSlow()) {
        const ssize_t got = recv(fd, block, sizeof block, 0);
        if (got <= 0) return; // gave up part-way: nothing is kept
        body.append(block, std::min(static_cast<std::size_t>(got), static_cast<std::size_t>(length) - body.size()));
    }

    // The song open on the handheld is not written over from here: the autosave would put the
    // handheld's back a moment later, and the one sent would be lost without a word.
    if (kind == "songs") {
        std::string open;
        {
            std::lock_guard<std::mutex> hold(openMutex_);
            open = openSong_.empty() ? "" : openSong_ + ".song";
        }
        std::string a = name, b = open; // a card may not tell "Quiet Road" from "quiet road"
        for (char& c : a) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        for (char& c : b) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        if (!b.empty() && a == b) return reply(fd, "409 Open", "text/plain", "that song is open on the handheld\n");
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
        // A song is what parseSong would take: its first line (the only thing it refuses one for).
        // Not parsed here (review 2026-11-23): a `preset N` line reads the app's preset lists, which
        // the app thread replaces when a preset is saved, under this thread's feet.
        good = isSongText(body);
    }
    if (!good) {
        return reply(fd, "400 Bad Request", "text/plain",
                     kind == "samples" ? "not a WAV file this can read\n" : kind == "chords" ? "not a chord pack\n" : "not an Apotheca song\n");
    }
    mkdir(folder.c_str(), 0755); // fine if it is already there
    std::vector<std::string> made;
    if (!foldersMade(folder, name, made)) { // and the folders inside it
        dropMade(made);
        return reply(fd, "400 Bad Request", "text/plain", "not a folder this can use\n");
    }
    std::unique_lock<std::mutex> songs(songFiles_, std::defer_lock);
    if (kind == "songs") songs.lock(); // not while the session is going through them
    const bool written = writeTextFile(file, body);
    if (kind == "songs") songs.unlock();
    if (!written) return reply(fd, "500 Could Not Save", "text/plain", "could not save it: is the card full?\n");
    if (kind == "samples") noteChange("", name);
    received_.fetch_add(1);
    reply(fd, "200 OK", "text/plain", "saved\n");
}

} // namespace apotheca
