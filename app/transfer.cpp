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
#include "web_look.hpp"

extern char** environ;

namespace apotheca {

extern const char* const kDesignPage; // design_page.cpp
bool plainName(const std::string& name, std::size_t longest); // (below)

namespace {

// Said at the top of the speed test's page at once, before the first row is timed.
const char* const kBenchSays = "The rows come as they are timed: about four minutes on the handheld. Leave the song stopped.\n\n";
const char* const kTooManyWrong = "too many wrong numbers: wait a minute\n";

// The files page (its look: web_look.cpp). Served as withWebLook(kPage), made once.
const char* const kPage = R"page(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'%3E%3Ccircle cx='20' cy='20' r='18' fill='%2317120d' stroke='%23c9a15a' stroke-width='3'/%3E%3Cpath d='M20 9 30 28H10z' fill='none' stroke='%23e3c47f' stroke-width='3'/%3E%3C/svg%3E">
<style>@@LOOK@@
/* ---- the files page: the recordings the big plate on the left, sending and the rest on the right ---- */
.grow{flex:1}
.line{display:flex;flex-wrap:wrap;align-items:center;gap:8px 10px}
label{font-size:13px;letter-spacing:.12em;color:var(--dim);text-transform:uppercase}
.desk{display:grid;grid-template-columns:minmax(0,1fr) clamp(340px,31vw,520px);gap:14px;padding:14px 16px 16px}
.side{display:flex;flex-direction:column;gap:14px;min-width:0}
ul{list-style:none;padding:0;margin:0}
#find{width:190px;margin-left:auto}
/* the recordings: folders down the left, the folder shown on the right */
.browser{display:grid;grid-template-columns:minmax(150px,210px) minmax(0,1fr);gap:12px}
#folders{display:flex;flex-direction:column;gap:2px;padding-right:10px;border-right:1px solid rgba(110,88,56,.45)}
.fold{display:flex;align-items:center;gap:8px;width:100%;text-align:left;background:transparent;border:1px solid transparent;box-shadow:none;padding:7px 8px;letter-spacing:.1em;font-size:14px;color:var(--text)}
.fold:hover{border-color:var(--line);background:rgba(255,235,200,.04)}
.fold .fname{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis}
.fold.sub{padding-left:24px}
.fold.sub .fname::before{content:"";display:inline-block;width:8px;height:8px;border-left:1px solid var(--brass);border-bottom:1px solid var(--brass);margin:0 6px 3px -12px;vertical-align:middle}
.fold.loose .fname{color:var(--purple)}
.fold.now{background:linear-gradient(90deg,rgba(0,255,180,.14),rgba(0,255,180,.02));border-color:rgba(0,255,180,.45);color:var(--teal)}
.fold.now .fname{color:var(--teal)}
.fold.none{opacity:.45}
.fold.newf{color:var(--brass2);border:1px dashed var(--line);margin-top:6px;justify-content:center}
.folder.over,#drop.over{border-color:var(--teal)!important;background:rgba(0,255,180,.09)!important;box-shadow:0 0 0 1px var(--teal),0 0 18px rgba(0,255,180,.2)}
.group{border:1px solid transparent;border-radius:4px;margin-bottom:10px}
.fhead{display:flex;flex-wrap:wrap;align-items:center;gap:6px 8px;padding:7px 8px;position:sticky;top:0;z-index:2;background:#28211a;border:1px solid var(--line);border-radius:4px}
.fhead .fname{font-size:15px;letter-spacing:.16em;text-transform:uppercase;color:var(--brass2);margin-right:auto;overflow-wrap:anywhere}
.fhead .fname.loose{color:var(--purple)}
.fhead button{padding:5px 9px;font-size:12px}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:6px 8px;border-bottom:1px solid rgba(75,61,47,.55)}
.row:hover{background:rgba(255,235,200,.035)}
.row .name,.row .size,#log .what,#log .state,#plan li,#plan summary b,.chip{font-family:var(--data);letter-spacing:0}
.row .name{flex:1 1 150px;min-width:0;overflow-wrap:anywhere;font-size:15px}
.row .size{font-size:13px;color:var(--dim);min-width:58px;text-align:right}
.acts{display:flex;gap:5px;flex-wrap:wrap}
.acts button,.acts .btn,.row>button{padding:5px 8px;font-size:12px}
button.play{min-width:58px;color:var(--teal)}
button.play.on{background:linear-gradient(180deg,#1fe3aa,#00b07e);border-color:var(--teal);color:#08130e}
input.pick{width:18px;height:18px;margin:0;cursor:pointer;flex:none}
.edit{flex:1 1 100%;display:flex;flex-wrap:wrap;gap:8px;align-items:center;background:var(--well);border:1px solid var(--edge);padding:10px;border-radius:4px}
.edit input{flex:1 1 150px}
.none{padding:10px 8px;color:var(--dim);font-size:14px}
#bulk{position:sticky;top:0;z-index:5;background:linear-gradient(180deg,#203a30,#1a2c25);border:1px solid var(--teal);border-radius:4px;padding:8px 10px;margin:0 0 10px}
#bulk b{color:var(--teal);font-weight:normal;letter-spacing:.08em}
/* sending */
#drop{border-style:solid}
#drop .well{border:1px dashed var(--edge);border-radius:5px;padding:12px;background:rgba(0,0,0,.18);display:flex;flex-direction:column;gap:10px}
#drop .say{margin:0}
.small{font-size:13px}
#activity{display:flex;flex-direction:column}
#log li{display:grid;grid-template-columns:10px minmax(0,1fr) auto;gap:3px 9px;align-items:center;padding:6px 2px;border-bottom:1px solid rgba(75,61,47,.55);font-size:14px}
#log li::before{content:"";width:9px;height:9px;border-radius:50%;background:var(--brass2);box-shadow:0 0 7px var(--brass)}
#log li.ok::before{background:var(--teal);box-shadow:0 0 7px var(--teal)}
#log li.no::before{background:var(--warn);box-shadow:0 0 7px var(--warn)}
#log .what{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#log .state{color:var(--dim);text-align:right}
#log .ok .state{color:var(--teal)}
#log .no .state{color:var(--warn);white-space:normal;grid-column:2/-1;text-align:left}
#log li>button{grid-column:3;padding:4px 8px;font-size:12px}
.bar{grid-column:1/-1;height:4px;background:var(--well);border-radius:2px;overflow:hidden;box-shadow:inset 0 1px 1px #000}
.bar i{display:block;height:4px;width:0;background:linear-gradient(90deg,var(--teal2),var(--teal))}
.no .bar i{background:var(--warn)}
/* songs, bounces, chord packs */
.lib{display:flex;flex-direction:column}
.lib .tabs{border-bottom:1px solid var(--edge);margin:-2px 0 10px}
.lib .tabs button{display:flex;gap:7px;align-items:center}
.tabbody .row .name{flex-basis:120px}
/* a box over the page: the plan of a folder sent whole, and the update */
.dialog{position:fixed;z-index:30;left:50%;top:50%;transform:translate(-50%,-50%);width:min(680px,calc(100vw - 24px));max-height:calc(100vh - 40px);overflow:auto;
box-shadow:0 0 0 100vmax rgba(6,5,4,.74),0 12px 40px #000;padding:18px 20px}
.dialog h2,.dialog h3{font-weight:normal;font-size:17px;letter-spacing:.14em;color:var(--brass2);margin:0 0 10px;text-transform:uppercase}
.dialog p{margin:6px 0;font-size:15px}
#plan details{border-top:1px solid rgba(75,61,47,.7);padding:6px 0;font:14px var(--data)}
#plan summary{cursor:pointer}
#plan summary b{display:inline-block;min-width:200px;font-weight:normal;color:var(--text);letter-spacing:.06em}
#plan ul{margin:6px 0 2px 18px;max-height:220px;overflow:auto}
#plan li{padding:1px 0}
#plan .was{color:var(--dim)}
#plan .warn{color:var(--warn)}
#updateBox ol{margin:8px 0 12px;padding-left:22px;color:var(--dim)}
#updateBox li{margin:3px 0}
#updateBox li b{color:var(--text);font-weight:normal}
/* a computer's screen: the whole page in one screen, each list scrolling in its own plate */
@media (min-width:960px) and (min-height:560px){
 .app{height:100vh;display:flex;flex-direction:column}
 .desk{flex:1;min-height:0}
 .recs{display:flex;flex-direction:column;min-height:0}
 .browser{flex:1;min-height:0}
 #folders,#recordings{overflow:auto;min-height:0}
 .side{min-height:0}
 #activity{max-height:34%;flex:none}
 #log{overflow:auto;min-height:0}
 .lib{flex:1;min-height:180px}
 .tabbody{flex:1;overflow:auto;min-height:0}
}
/* a phone: one column, sending first, then the recordings, folders as a row to swipe along */
@media (max-width:959px){
 .desk{display:flex;flex-direction:column;padding:10px}
 .side{display:contents}
 #drop{order:1}#activity{order:2}.recs{order:3}.lib{order:4}
 .browser{grid-template-columns:1fr}
 #folders{flex-direction:row;overflow-x:auto;border-right:0;padding:0 0 8px;border-bottom:1px solid rgba(110,88,56,.45)}
 .fold{width:auto;flex:none;border-color:var(--line)}
 .fold.sub{padding-left:8px}.fold.sub .fname::before{display:none}
 .fold.newf{margin-top:0}
 #activity #log{max-height:40vh;overflow:auto}
}
@media (max-width:560px){
 .plate{padding:12px}
 #find{width:100%;order:9}
 .row .name{flex:1 1 0}
 .recs .acts{flex-basis:100%}
 .recs .acts>*{flex:1 1 auto;text-align:center}
 .lib .tabs button{padding:7px 8px;letter-spacing:.06em}
 button,.btn{padding:9px 10px}
}
</style></head><body>
<div class="app">
<header class="top">
 <a class="brand" id="home" href="#">@@BRAND@@<span class="page">FILES</span></a>
 <div class="chips"><span class="chip" id="version"></span><span class="chip" id="free"></span></div>
 <nav><a class="btn main" id="design" href="#">Sound designer</a><a class="btn" id="bench" href="#" target="_blank" title="Times the engine on this device. Stop the song first. Takes about four minutes; the rows come as they are timed.">Speed test</a><button id="openUpdate" class="violet">Update Apotheca</button></nav>
</header>
<main class="desk">
<section class="plate recs" data-section="Recordings">
 <div class="head"><h2>Recordings</h2><span class="count" id="nSamples"></span><input id="find" type="search" placeholder="find a recording" aria-label="find"></div>
 <div class="line hid" id="bulk"><b id="nPicked"></b><label for="bulkTo">move to</label><select id="bulkTo"></select><input id="bulkNew" class="hid" type="text" maxlength="49" placeholder="new folder, or folder/inner"><button id="bulkMove" class="main">Move</button><button id="bulkDelete" class="del">Delete</button><button id="bulkNone">Choose none</button></div>
 <div class="browser"><nav id="folders" aria-label="folders"></nav><div id="recordings"></div></div>
</section>
<div class="side">
<section class="plate" id="drop" data-section="Send">
 <div class="head"><h2>Send to the handheld</h2></div>
 <div class="well">
  <div class="line"><button class="main" id="pick">Choose files</button><button id="pickFolder">Choose a folder</button></div>
  <p class="say">or drop files or a whole folder anywhere on the page.</p>
  <div class="line"><label for="into">Recordings go in</label><select id="into"></select><input id="intoNew" class="hid" type="text" maxlength="49" placeholder="new folder, or folder/inner"></div>
 </div>
 <p class="say small" style="margin:10px 0 0">Recordings (.wav), chord packs (.json), songs (.song). 30 MB at most each. Every file is checked before it is kept.</p>
 <input id="files" class="hid" type="file" multiple accept=".wav,.json,.song,audio/wav,audio/x-wav">
 <input id="dir" class="hid" type="file" webkitdirectory multiple>
</section>
<section class="plate hid" id="activity" data-section="Activity">
 <div class="head"><h2>Sent and done</h2><span class="grow"></span><button id="clear">Clear the list</button></div>
 <ul id="log"></ul>
</section>
<section class="plate lib" data-section="Songs">
 <div class="tabs" role="tablist"><button role="tab" data-tab="songs">Songs <span class="count" id="nSongs"></span></button><button role="tab" data-tab="bounces">Bounces <span class="count" id="nBounces"></span></button><button role="tab" data-tab="chords">Chord packs <span class="count" id="nChords"></span></button></div>
 <div class="tabbody">
  <div data-pane="songs"><ul id="songs"></ul></div>
  <div data-pane="bounces"><p class="say">Your songs as sound files, and as MIDI.</p><ul id="bounces"></ul></div>
  <div data-pane="chords"><ul id="chords"></ul></div>
 </div>
</section>
</div>
</main>
</div>
<div id="plan" class="dialog plate hid" role="dialog" aria-modal="true"></div>
<div id="updateBox" class="dialog plate hid" role="dialog" aria-modal="true" data-section="Update">
 <h2>Update Apotheca</h2>
 <p>Choose the new <b>apotheca</b> program file (inside the apotheca folder of the zip).</p>
 <ol><li>It is sent and checked, and only set aside: <b>the network alone installs nothing</b>.</li><li>On the handheld, <b>hold A</b> to accept it (hold B deletes it).</li><li>It is used the next time Apotheca starts.</li></ol>
 <div class="line"><button id="pickUpdate" class="main">Choose the program file</button><button id="closeUpdate">Close</button></div>
 <input id="update" class="hid" type="file">
</div>
<script>@@FONT@@
const base = location.pathname.replace(/\/$/, '');
const $ = id => document.getElementById(id);
const kinds = {wav: 'samples', json: 'chords', song: 'songs'};
const NEW = '*new'; // a folder name cannot have a * in it
let all = {samples: [], songs: [], chords: [], bounces: [], folders: [], sizes: {}};
const picked = new Set(); // recordings ticked, to move or delete together
const made = new Set(); // folders named here with nothing in them yet
let first = true, filter = '', playing = '', audio = null, into = '', shownFolder = null; // shownFolder: the folder on the right ('' UNSORTED)
$('design').href = base + '/design';
$('bench').href = base + '/speed';
$('home').href = base + '/';

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
  if (!(all.folders || []).includes(f)) { made.delete(f); if (shownFolder == f) shownFolder = null; if (into == f) into = ''; draw(); return; } // named here, never on the card
  let r;
  try { const got = await fetch(base + '/folders/' + f.split('/').map(encodeURIComponent).join('/'), {method: 'DELETE'}); r = {ok: got.ok, text: (await got.text()).trim()}; }
  catch (e) { r = {ok: false, text: 'no answer from the handheld'}; }
  note('folder ' + f, r.ok ? r.text : r.text.startsWith('not deleted') ? r.text : 'not deleted: ' + r.text, r.ok ? 'ok' : 'no');
  if (r.ok) { made.delete(f); if (shownFolder == f) shownFolder = null; if (into == f) into = ''; }
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
  li.append(w, s); $('log').prepend(li); $('activity').classList.remove('hid');
  return {li, state: s, what: w};
}
$('clear').onclick = () => { for (const li of [...$('log').children]) if (!li.classList.contains('busy')) li.remove(); if (!$('log').children.length) $('activity').classList.add('hid'); };

// ---- what is on the handheld ----
async function list() {
  try { const r = await fetch(base + '/list'); if (!r.ok) { note('the handheld', (await r.text()).trim(), 'no'); return; } all = await r.json(); } catch (e) { note('the handheld', 'no answer: is the WiFi page still on?', 'no'); return; }
  all.sizes = all.sizes || {};
  for (const f of [...made]) if ((all.folders || []).includes(f)) made.delete(f);
  if (first) { first = false; shownFolder = all.samples.some(n => !n.includes('/')) ? '' : (split(all.samples.find(n => n.includes('/')) || '')[0]); }
  $('version').textContent = all.version ? 'build ' + all.version + (all.built ? ' · built ' + all.built : '') : '';
  $('free').textContent = all.free >= 0 ? (all.free >= 1024 ? (all.free / 1024).toFixed(1) + ' GB' : all.free + ' MB') + ' free on the card' : '';
  $('free').classList.toggle('hid', !(all.free >= 0));
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
// The folders down the left (each one a place to drop on); the folder picked there on the right, or,
// with something in the find box, every match in every folder, under its folder's name.
function drawRecordings() {
  const groups = new Map([['', []]]), totals = new Map([['', 0]]);
  for (const f of folders()) groups.set(f, []), totals.set(f, 0);
  all.samples.forEach((name, i) => {
    const [f, file] = split(name);
    totals.set(f, totals.get(f) + 1);
    if (!filter || name.toLowerCase().includes(filter)) groups.get(f).push([name, file, (all.sizes.samples || [])[i]]);
  });
  if (shownFolder == null || !groups.has(shownFolder)) shownFolder = '';
  $('nSamples').textContent = all.samples.length || '';
  const pane = document.createDocumentFragment();
  for (const [f, rows] of groups) {
    const b = el('button', 'fold folder' + (f.includes('/') ? ' sub' : '') + (f ? '' : ' loose') + (!filter && f == shownFolder ? ' now' : '') + (filter && !rows.length ? ' none' : ''));
    b.dataset.folder = f; b.dataset.act = 'show'; b.title = f ? 'show ' + f + ', or drop recordings on it' : 'recordings in no folder';
    b.append(el('span', 'fname', f ? split(f)[1] || f : 'UNSORTED'), el('span', 'count', filter ? rows.length : totals.get(f)));
    pane.append(b);
  }
  pane.append(btn('+ new folder', 'newfolder', 'fold newf'));
  $('folders').textContent = ''; $('folders').append(pane);
  const page = document.createDocumentFragment();
  let shown = 0;
  for (const [f, rows] of groups) {
    if (filter ? !rows.length : f != shownFolder) continue;
    shown += rows.length;
    const sec = el('section', 'folder group'), head = el('div', 'fhead');
    sec.dataset.folder = f;
    head.append(el('span', 'fname' + (f ? '' : ' loose'), f ? f.replace('/', ' / ') : 'UNSORTED'), el('span', 'count', rows.length), btn(rows.length && rows.every(r => picked.has(r[0])) ? 'choose none' : 'choose all', 'all'), btn('add files', 'add'));
    if (emptyFolder(f)) head.append(btn('delete folder', 'delfolder', 'del')); // a folder stays when its last recording goes: deleted only on purpose
    sec.append(head);
    const ul = el('ul');
    for (const [name, file, size] of rows) ul.append(row('samples', name, file, size));
    if (!rows.length) ul.append(el('li', 'none', f ? 'nothing in it yet: drop recordings here' : 'none: recordings sent with no folder land here'));
    sec.append(ul);
    page.append(sec);
  }
  if (filter && !shown) page.append(el('div', 'none', 'nothing by that name'));
  const box = $('recordings'); box.textContent = ''; box.append(page);
  bulkBar();
}

// ---- several at once ----
// Tick recordings (or CHOOSE ALL on a folder), then move or delete them together. They go one
// after another, each with its own line in the list, and the page is read again once at the end.
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
  let shown = false;
  for (const file of files) {
    const kind = kinds[file.name.split('.').pop().toLowerCase()];
    if (!kind) { note(file.name, 'refused: not a .wav, .json or .song', 'no'); continue; }
    const f = kind == 'samples' ? folder : '', name = (f ? f + '/' : '') + file.name;
    const bad = (f && wrongFolder(f) && 'folder: ' + wrongFolder(f)) || wrong(file.name, 60) || tooLong(f, file.name) || (file.size > 30 * 1048576 && 'too big: 30 MB at most');
    if (bad) { note(name, 'refused: ' + bad, 'no'); continue; }
    add(name, urlOf(kind, name), file, (all[kind] || []).includes(name));
    if (kind == 'samples' && !shown && !filter) { shown = true; shownFolder = f; } // the folder they go into, shown as they land
  }
  if (shown) drawRecordings();
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
function target() { // the folder chosen under RECORDINGS GO IN, or null if it is not a name that will do
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
$('openUpdate').onclick = () => $('updateBox').classList.remove('hid');
$('closeUpdate').onclick = () => $('updateBox').classList.add('hid');
$('pickUpdate').onclick = () => $('update').click();
$('update').onchange = () => { const file = $('update').files[0]; if (file && file.size > 30 * 1048576) note('update: ' + file.name, 'refused: too big: 30 MB at most', 'no'); else if (file) add('update: ' + file.name, base + '/update/apotheca', file, false); $('update').value = ''; if (file) $('updateBox').classList.add('hid'); };
document.addEventListener('keydown', e => { if (e.key == 'Escape') $('updateBox').classList.add('hid'); });

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
  line.style.marginTop = '12px';
  go.id = 'planGo'; no.id = 'planNo';
  go.disabled = !n || (all.free >= 0 && mb > all.free);
  go.onclick = () => { box.classList.add('hid'); sendPlan(p, title); };
  no.onclick = () => { box.classList.add('hid'); box.textContent = ''; };
  line.append(go, no); box.append(line);
  box.classList.remove('hid');
  box.scrollTop = 0;
}
function sendPlan(p, title) {
  if (!p.jobs.length) return;
  const n = note(title, 'waiting', 'busy'), bar = el('div', 'bar'), fill = el('i'), stop = el('button', '', 'Stop');
  n.li.append(stop, bar); bar.append(fill);
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
async function deleteFile(kind, name) {
  if (playing == kind + '/' + name) stop();
  const r = await ask('DELETE', kind, name);
  note(name, r.ok ? r.text : 'not deleted: ' + r.text, r.ok ? 'ok' : 'no');
  list();
}
async function moveOne(from, to) { // one move, and its line in the list; true if it went
  if (playing == 'samples/' + from) stop();
  const r = await ask('POST', 'samples', from, {'X-To': to.split('/').map(encodeURIComponent).join('/')});
  note(from + ' to ' + to, r.ok ? r.text : 'refused: ' + r.text, r.ok ? 'ok' : 'no');
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
// A new folder from the list of folders: named here, shown, and made on the card when the first recording lands in it.
function newFolder(b) {
  const box = el('div', 'edit'), name = el('input'), ok = btn('make', '', 'main'), no = btn('cancel', '');
  name.type = 'text'; name.maxLength = 49; name.placeholder = 'new folder, or folder/inner';
  const go = () => {
    const f = name.value.trim(), bad = f ? wrongFolder(f) : 'it needs a name';
    if (bad) { note('new folder', bad, 'no'); name.focus(); return; }
    made.add(f); shownFolder = f; draw();
  };
  ok.onclick = go; no.onclick = () => box.replaceWith(b);
  box.onkeydown = e => { if (e.key == 'Enter') go(); if (e.key == 'Escape') box.replaceWith(b); };
  box.append(name, ok, no); b.replaceWith(box); name.focus();
}
document.addEventListener('click', e => {
  const t = e.target.closest('[data-act]'); if (!t || !t.dataset.act) return;
  const li = t.closest('li.row'), sec = t.closest('.folder'), act = t.dataset.act;
  if (act == 'show') { shownFolder = sec.dataset.folder; if (filter) { filter = ''; $('find').value = ''; } drawRecordings(); $('recordings').scrollTop = 0; return; }
  if (act == 'newfolder') { newFolder(t); return; }
  if (act == 'add') { e.stopPropagation(); pickFor = sec.dataset.folder; $('files').click(); return; }
  if (act == 'all') { // every recording in this folder (as the find box has narrowed it), or none of them
    const f = sec.dataset.folder, names = all.samples.filter(n => split(n)[0] == f && (!filter || n.toLowerCase().includes(filter)));
    const every = names.length && names.every(n => picked.has(n));
    for (const n of names) if (every) picked.delete(n); else picked.add(n);
    drawRecordings(); return;
  }
  if (act == 'delfolder') { // asks once more, there in its heading
    t.textContent = 'delete it'; t.className = 'sure'; t.dataset.act = 'surefolder';
    const keep = btn('keep', 'keepfolder'); t.after(keep);
    setTimeout(() => { if (t.isConnected && t.dataset.act == 'surefolder') { t.textContent = 'delete folder'; t.className = 'del'; t.dataset.act = 'delfolder'; keep.remove(); } }, 6000);
    return;
  }
  if (act == 'keepfolder') { const d = t.previousSibling; d.textContent = 'delete folder'; d.className = 'del'; d.dataset.act = 'delfolder'; t.remove(); return; }
  if (act == 'surefolder') { removeFolder(sec.dataset.folder); return; }
  if (!li) return;
  const kind = li.dataset.kind, name = li.dataset.name;
  if (act == 'play') play(t, kind, name);
  if (act == 'rename' || act == 'move') edit(li, act == 'move');
  if (act == 'delete') { // asks once more, there in the row
    t.textContent = 'delete it'; t.className = 'sure'; t.dataset.act = 'sure';
    const keep = btn('keep', 'keep'); t.after(keep);
    setTimeout(() => { if (t.isConnected && t.dataset.act == 'sure') { t.textContent = 'delete'; t.className = 'del'; t.dataset.act = 'delete'; keep.remove(); } }, 6000);
  }
  if (act == 'keep') { const d = t.previousSibling; d.textContent = 'delete'; d.className = 'del'; d.dataset.act = 'delete'; t.remove(); }
  if (act == 'sure') deleteFile(kind, name);
});
// SONGS, BOUNCES and CHORD PACKS share a plate, one at a time (the one last looked at is kept, in this browser).
function tab(name) {
  for (const b of document.querySelectorAll('[data-tab]')) b.setAttribute('aria-selected', b.dataset.tab == name);
  for (const p of document.querySelectorAll('[data-pane]')) p.hidden = p.dataset.pane != name;
  try { localStorage.setItem('apotheca.tab', name); } catch (e) {}
}
for (const b of document.querySelectorAll('[data-tab]')) b.onclick = () => tab(b.dataset.tab);
let lastTab = 'songs'; try { lastTab = localStorage.getItem('apotheca.tab') || 'songs'; } catch (e) {}
tab(['songs', 'bounces', 'chords'].includes(lastTab) ? lastTab : 'songs');
list();
</script></body></html>
)page";

// The SPEED TEST's page: /bench's rows as they come, in plates.
const char* const kSpeedPage = R"page(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca speed test</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'%3E%3Ccircle cx='20' cy='20' r='18' fill='%2317120d' stroke='%23c9a15a' stroke-width='3'/%3E%3Cpath d='M20 9 30 28H10z' fill='none' stroke='%23e3c47f' stroke-width='3'/%3E%3C/svg%3E">
<style>@@LOOK@@
.wrap{padding:14px 16px 24px}
.status{display:flex;flex-wrap:wrap;align-items:center;gap:8px 14px;margin-bottom:14px}
.status .what{font-size:16px;letter-spacing:.08em;text-transform:uppercase}
.status .say{margin:0}
#rows{columns:3 380px;column-gap:14px}
.part{break-inside:avoid;margin:0 0 14px;padding:12px 14px}
.part h2{font-weight:normal;font-size:14px;letter-spacing:.12em;text-transform:uppercase;color:var(--brass2);margin:0 0 8px;padding-bottom:6px;border-bottom:1px solid rgba(201,161,90,.32)}
.part table{width:100%;border-collapse:collapse;font-size:15px}
.part td{font-family:var(--data);padding:3px 0;border-bottom:1px solid rgba(75,61,47,.5);vertical-align:top}
.part td+td{text-align:right;color:var(--teal);padding-left:12px;white-space:nowrap}
.part p{margin:2px 0;font:15px var(--data);color:var(--text);white-space:pre-wrap}
.top nav{margin-left:auto}
</style></head><body>
<header class="top">
 <a class="brand" id="home" href="#">@@BRAND@@<span class="page">SPEED TEST</span></a>
 <div class="chips"><span class="chip" id="version"></span></div>
 <nav><a class="btn" id="files" href="#">Files</a><a class="btn" id="save" href="#" download="apotheca speed test.txt">Save as text</a></nav>
</header>
<div class="wrap">
 <div class="status"><span class="lamp busy" id="lamp"></span><span class="what" id="what">timing</span><p class="say" id="says"></p></div>
 <div id="rows"></div>
</div>
<script>@@FONT@@
// The speed test's rows as they are timed (/bench sends them a line at a time), put in plates: a line
// that does not start with a space and ends in ":" begins a part; an indented one is a row, its name
// and its numbers cut where two spaces or more stand between them.
const base = location.pathname.replace(/\/speed\/?$/, '');
const $ = id => document.getElementById(id);
$('home').href = $('files').href = base + '/';
let said = ''; // every line as it came, for SAVE AS TEXT (a second /bench would time it all again)
$('save').onclick = () => { $('save').href = URL.createObjectURL(new Blob([said], {type: 'text/plain'})); };
function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
let part = null, lines = 0;
function partOf(title) { const p = el('section', 'plate part'); p.append(el('h2', '', title)); part = p; $('rows').append(p); return p; }
function line(s) {
  said += s + '\n';
  if (!s.trim()) return;
  lines++;
  if (lines == 1 && /^Apotheca build /.test(s)) { $('version').textContent = s.replace(/^Apotheca build /, 'build '); return; }
  if (s == '(done)') { $('lamp').className = 'lamp ok'; $('what').textContent = 'done'; $('says').textContent = ''; return; }
  if (/^\(.*\)$/.test(s)) { $('lamp').className = 'lamp no'; $('what').textContent = s.slice(1, -1); $('says').textContent = ''; return; }
  if (/^The rows come/.test(s)) { $('says').textContent = s; return; }
  if (!/^\s/.test(s) && /:$/.test(s)) { partOf(s.slice(0, -1)); return; }
  const p = part || partOf('the device');
  const m = /^\s+(.*?\S)\s{2,}(\S.*)$/.exec(s);
  if (m) { let t = p.querySelector('table'); if (!t) p.append(t = el('table')); const tr = el('tr'); tr.append(el('td', '', m[1]), el('td', '', m[2])); t.append(tr); }
  else p.append(el('p', '', s.trim()));
}
(async () => {
  try {
    const r = await fetch(base + '/bench', {cache: 'no-store'});
    const reader = r.body.getReader(), words = new TextDecoder();
    let held = '';
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      held += words.decode(value, {stream: true});
      const cut = held.split('\n'); held = cut.pop();
      cut.forEach(line);
    }
    line(held);
    if ($('lamp').classList.contains('busy')) { $('lamp').className = 'lamp no'; $('what').textContent = 'the speed test did not finish'; }
  } catch (e) { $('lamp').className = 'lamp no'; $('what').textContent = 'no answer from the handheld'; }
})();
</script></body></html>
)page";

// The door: a browser at the address with no number, or a wrong one, is asked for it (still a 403).
const char* const kDoorPage = R"page(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apotheca</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'%3E%3Ccircle cx='20' cy='20' r='18' fill='%2317120d' stroke='%23c9a15a' stroke-width='3'/%3E%3Cpath d='M20 9 30 28H10z' fill='none' stroke='%23e3c47f' stroke-width='3'/%3E%3C/svg%3E">
<style>@@LOOK@@
body{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.door{width:min(440px,100%);text-align:center;padding:26px 24px 22px}
.door .brand{justify-content:center;margin-bottom:18px}
.door .brand .emblem{width:54px;height:54px}
.door p{margin:8px 0;font-size:16px}
form{display:flex;gap:8px;justify-content:center;margin:18px 0 8px}
#pin{width:7.5em;font-size:26px;letter-spacing:.3em;text-align:center;padding:6px 4px 6px 12px}
#wrongNumber{color:var(--warn)}
</style></head><body>
<main class="plate door">
 <div class="brand">@@BRAND@@</div>
 <p id="wrongNumber" hidden>That number is not the one on the screen.</p>
 <p>Open the address shown on the Apotheca screen, with its four-digit number.</p>
 <form id="go"><input id="pin" type="text" inputmode="numeric" pattern="[0-9]*" maxlength="4" autocomplete="off" aria-label="the four-digit number" placeholder="0000" autofocus><button class="main">Open</button></form>
 <p class="say">The number is on the handheld, after the address (SET, WIFI PAGE). Five wrong numbers and the page waits a minute.</p>
</main>
<script>
// (No lettering loaded here: asking for /NUMBER/font.ttf with a wrong number would count as another try.)
// A number in the address that was not the right one: said so. (The server counts it; this only reads the address.)
if (/^\/\d+\/?$/.test(location.pathname)) document.getElementById('wrongNumber').hidden = false;
document.getElementById('go').onsubmit = e => {
  e.preventDefault();
  const n = document.getElementById('pin').value.replace(/\D/g, '');
  if (n.length) location.href = '/' + n + '/';
};
</script></body></html>
)page";

// Each page with its look filled in, once (a function's own static is made once, whichever thread asks first).
const std::string& pageWithLook(int which) {
    static const std::string pages[] = {withWebLook(kPage), withWebLook(kSpeedPage), withWebLook(kDoorPage), withWebLook(kDesignPage)};
    return pages[which];
}
enum { kFilesPage, kSpeedTestPage, kDoorWayPage, kDesignerPage };

bool sendAll(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t sent = ::send(fd, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

// `keep`: the browser may keep it (the lettering: the same bytes every time); everything else is asked for afresh.
void reply(int fd, const char* status, const char* type, const std::string& body, bool keep = false) {
    char head[256];
    const int n = std::snprintf(head, sizeof head,
                                "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: %s\r\n\r\n", status,
                                type, body.size(), keep ? "private, max-age=31536000, immutable" : "no-store");
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
        // A browser at the bare address, or at a wrong number and nothing after it, gets the door: a box for the
        // number (still 403, and counted as above). Anything else gets the sentence alone.
        const bool askNumber = method == "GET" && (path == "/" || (!first.empty() && first.find_first_not_of("0123456789") == std::string::npos &&
                                                               (ends == std::string::npos || ends + 1 == path.size())));
        if (askNumber) return reply(fd, "403 Forbidden", "text/html; charset=utf-8", pageWithLook(kDoorWayPage));
        return reply(fd, "403 Forbidden", "text/plain", "Open the address shown on the Apotheca screen, with its four-digit number.\n");
    }
    path = path.substr(door.size());

    if (method == "GET" && (path.empty() || path == "/")) return reply(fd, "200 OK", "text/html; charset=utf-8", pageWithLook(kFilesPage));
    if (method == "GET" && path == "/speed") return reply(fd, "200 OK", "text/html; charset=utf-8", pageWithLook(kSpeedTestPage));
    // The device's own lettering, for the pages (web_look.cpp). Kept by the browser: sent once.
    if (method == "GET" && path == "/font.ttf") {
        if (lettering_.empty()) return reply(fd, "404 Not Found", "text/plain", "no lettering here\n");
        return reply(fd, "200 OK", "font/ttf", lettering_, true);
    }
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
        if (method == "GET" && path == "/design") return reply(fd, "200 OK", "text/html; charset=utf-8", pageWithLook(kDesignerPage));
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
        std::string first = "Apotheca build " + std::string(kVersion) + ", built " + std::string(kBuilt) + "\n" + kBenchSays;
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
        if (kind == "chords") chordsReceived_.fetch_add(1); // (before received_: the session reads that first)
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
    if (kind == "chords") chordsReceived_.fetch_add(1); // (before received_: the session reads that first)
    received_.fetch_add(1);
    reply(fd, "200 OK", "text/plain", "saved\n");
}

} // namespace apotheca
