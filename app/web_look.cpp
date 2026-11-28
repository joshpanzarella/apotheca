// The look the WiFi pages share: web_look.hpp. (The texts are raw strings marked "page", as the pages
// themselves are: words_test reads them for a maker's name only.)

#include "web_look.hpp"

namespace apotheca {

namespace {

// The style sheet. The colours are the Shapetaker family's (the VCV Rack panels: near-black leather,
// brass, cream lettering, teal #00ffb4 for channel A, purple #b400ff for channel B) on the handheld's
// BROWN theme (ui.hpp). Lettering on a dark ground is never dimmer than --dim (6:1 or more).
const char* const kLook = R"page(
:root{--bg:#15120f;--well:#0e0b09;--panel:#221c16;--panel2:#2c241c;--line:#4b3d2f;--edge:#6e5838;
--brass:#c9a15a;--brass2:#e3c47f;--text:#ece1c8;--dim:#b8aa8e;--teal:#00ffb4;--teal2:#00c48c;--purple:#c77dff;--purple2:#b400ff;
--stop:#ef6f5a;--warn:#f0b35a;--font:Lettering,"Routed Gothic Narrow",system-ui,-apple-system,"Segoe UI",sans-serif;--data:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color-scheme:dark}
*{box-sizing:border-box}
html{background:var(--bg)}
body{margin:0;color:var(--text);font:16px/1.4 var(--font);font-synthesis:none;-webkit-text-size-adjust:100%;
background-color:var(--bg);
background-image:radial-gradient(ellipse 80% 60% at 50% -10%,rgba(201,161,90,.09),transparent 70%),
url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='240' height='240'%3E%3Cfilter id='a'%3E%3CfeTurbulence type='turbulence' baseFrequency='.045 .06' numOctaves='2' seed='4' stitchTiles='stitch'/%3E%3CfeColorMatrix values='0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -2.2 1.05'/%3E%3C/filter%3E%3Cfilter id='b'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='.85' numOctaves='2' stitchTiles='stitch'/%3E%3CfeColorMatrix values='0 0 0 0 .55 0 0 0 0 .48 0 0 0 0 .38 0 0 0 .5 -.18'/%3E%3C/filter%3E%3Crect width='240' height='240' filter='url(%23a)' opacity='.55'/%3E%3Crect width='240' height='240' filter='url(%23b)' opacity='.35'/%3E%3C/svg%3E");
background-attachment:fixed}
a{color:var(--teal)}
/* the device's lettering is for the page's own words; a file's name or a number typed is in the browser's plain letters, where a 1 is never an I */
.data{font-family:var(--data);letter-spacing:0}
.hid,[hidden]{display:none!important}
::selection{background:rgba(0,255,180,.3)}
/* the header: emblem, wordmark, APOTHECA, as the start-up picture */
.top{display:flex;flex-wrap:wrap;align-items:center;gap:8px 18px;padding:10px 18px;border-bottom:1px solid var(--edge);
background:linear-gradient(180deg,rgba(43,35,27,.92),rgba(24,20,16,.92));box-shadow:0 1px 0 rgba(0,0,0,.6),0 6px 18px rgba(0,0,0,.35)}
.brand{display:flex;align-items:center;gap:12px;color:var(--text);text-decoration:none}
.brand .emblem{width:38px;height:38px;flex:none}
.brand .words{display:flex;flex-direction:column;align-items:center;gap:3px}
.brand .mark{height:8px;width:auto;color:var(--brass)}
.brand b{font-weight:normal;font-size:25px;line-height:1;letter-spacing:.2em;margin-right:-.2em;color:#f1e6cc;text-shadow:0 1px 0 #000,0 0 14px rgba(227,196,127,.18)}
.brand .page{font-size:13px;letter-spacing:.18em;color:var(--brass2);border-left:1px solid var(--edge);padding-left:12px;align-self:stretch;display:flex;align-items:center}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--dim);border:1px solid var(--line);border-radius:12px;padding:3px 10px;background:rgba(0,0,0,.25);white-space:nowrap}
.chip b{font-weight:normal;color:var(--text)}
.top nav{margin-left:auto;display:flex;flex-wrap:wrap;gap:8px}
/* a plate: a panel as on a rack module, brass-edged, a screw in each corner */
.plate{position:relative;background:linear-gradient(180deg,rgba(40,33,26,.96),rgba(27,22,18,.96));border:1px solid var(--edge);border-radius:6px;
box-shadow:inset 0 1px 0 rgba(255,236,200,.07),inset 0 0 0 1px rgba(0,0,0,.35),0 3px 14px rgba(0,0,0,.45);padding:14px 16px;min-width:0}
.plate::before{content:"";position:absolute;inset:0;pointer-events:none;border-radius:6px;
background:radial-gradient(circle at 7px 7px,#a58b5e 0 1.6px,#3a2f22 2.6px,transparent 3.2px),radial-gradient(circle at calc(100% - 7px) 7px,#a58b5e 0 1.6px,#3a2f22 2.6px,transparent 3.2px),
radial-gradient(circle at 7px calc(100% - 7px),#a58b5e 0 1.6px,#3a2f22 2.6px,transparent 3.2px),radial-gradient(circle at calc(100% - 7px) calc(100% - 7px),#a58b5e 0 1.6px,#3a2f22 2.6px,transparent 3.2px)}
.plate>h2,.plate>.head>h2{font-weight:normal;font-size:15px;letter-spacing:.2em;text-transform:uppercase;color:var(--brass2);margin:0}
.head{display:flex;flex-wrap:wrap;align-items:center;gap:8px 10px;padding-bottom:10px;margin-bottom:10px;border-bottom:1px solid rgba(201,161,90,.32)}
.count{font-size:13px;letter-spacing:.06em;color:var(--dim);border:1px solid var(--line);border-radius:10px;padding:0 7px;min-width:22px;text-align:center}
.count:empty{display:none}
.say,.dim{color:var(--dim)}
.say{font-size:14px;margin:0 0 8px}
/* keys: hardware buttons */
button,.btn{font:inherit;font-size:13px;line-height:1.2;letter-spacing:.12em;text-transform:uppercase;color:var(--text);
background:linear-gradient(180deg,#342a20,#241d17);border:1px solid #5d4b33;border-radius:4px;padding:7px 11px;cursor:pointer;
text-decoration:none;white-space:nowrap;box-shadow:inset 0 1px 0 rgba(255,240,210,.09),0 1px 2px rgba(0,0,0,.55);display:inline-block}
button:hover,.btn:hover{border-color:var(--brass);color:#fff6e0}
button:active,.btn:active{transform:translateY(1px);box-shadow:inset 0 1px 3px rgba(0,0,0,.6)}
button:focus-visible,.btn:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--teal);outline-offset:1px}
button:disabled{opacity:.45;cursor:default}
button.main,.btn.main{background:linear-gradient(180deg,#1fe3aa,#00b07e);border-color:var(--teal);color:#08130e;box-shadow:inset 0 1px 0 rgba(255,255,255,.35),0 0 14px rgba(0,255,180,.22)}
button.main:hover,.btn.main:hover{color:#000;border-color:#9fffe0}
button.violet,.btn.violet{border-color:#8a4fc0;color:#e6c8ff;background:linear-gradient(180deg,#3a2650,#271a36)}
button.del{color:var(--stop)}
button.sure{background:linear-gradient(180deg,#f07a62,#c9503b);border-color:var(--stop);color:#1a0a06}
button.on{background:linear-gradient(180deg,#1fe3aa,#00b07e);border-color:var(--teal);color:#08130e}
/* wells: what is typed or picked */
input[type=text],input[type=search],input[type=number],select{font:15px/1.3 var(--data);color:var(--text);background:var(--well);border:1px solid var(--line);border-radius:4px;
padding:6px 9px;box-shadow:inset 0 1px 3px rgba(0,0,0,.7);max-width:100%;min-width:0}
select{appearance:none;-webkit-appearance:none;padding-right:26px;cursor:pointer;
background:var(--well) url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='10' height='6'%3E%3Cpath d='M0 0h10L5 6z' fill='%23c9a15a'/%3E%3C/svg%3E") no-repeat right 9px center}
select:disabled{opacity:.5}
optgroup{color:var(--brass2);font-style:normal}
option{color:var(--text);background:#1a1511}
input::placeholder{color:#8f8370}
input[type=checkbox]{accent-color:var(--teal2)}
/* a fader: a groove lit teal as far as it is turned (--p, set by the page), a cream cap */
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:22px;margin:2px 0;background:transparent;cursor:pointer;--p:0%}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;border:1px solid #3d3226;background:linear-gradient(90deg,var(--teal2) var(--p),var(--well) var(--p));box-shadow:inset 0 1px 2px #000}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:18px;margin-top:-7px;border-radius:2px;border:1px solid #6b5a40;
background:linear-gradient(90deg,#d8cbae,#f4ead2 45%,#b7a582 50%,#efe3c6);box-shadow:0 1px 3px rgba(0,0,0,.8)}
input[type=range]::-moz-range-track{height:6px;border-radius:3px;border:1px solid #3d3226;background:var(--well)}
input[type=range]::-moz-range-progress{height:6px;border-radius:3px;background:var(--teal2)}
input[type=range]::-moz-range-thumb{width:12px;height:18px;border-radius:2px;border:1px solid #6b5a40;background:linear-gradient(90deg,#d8cbae,#f4ead2 45%,#b7a582 50%,#efe3c6)}
input[type=range]:focus-visible{outline:0}
input[type=range]:focus-visible::-webkit-slider-thumb{box-shadow:0 0 0 2px var(--teal)}
/* tabs, as the handheld's L2 / R2 */
.tabs{display:flex;flex-wrap:wrap;gap:4px}
.tabs button{border-radius:4px 4px 0 0;border-bottom-color:transparent;background:transparent;box-shadow:none;color:var(--dim)}
.tabs button[aria-selected=true]{color:var(--teal);border-color:var(--edge);border-bottom-color:var(--panel);background:linear-gradient(180deg,#30271f,#261f18);box-shadow:inset 0 2px 0 var(--teal)}
/* a lamp: a small jewel that is lit */
.lamp{display:inline-block;width:9px;height:9px;border-radius:50%;background:#3a3026;box-shadow:inset 0 1px 1px rgba(0,0,0,.8);flex:none}
.lamp.ok{background:var(--teal);box-shadow:0 0 8px var(--teal)}
.lamp.no{background:var(--warn);box-shadow:0 0 8px var(--warn)}
.lamp.busy{background:var(--brass2);box-shadow:0 0 8px var(--brass);animation:blink 1s infinite alternate}
@keyframes blink{to{opacity:.35}}
::-webkit-scrollbar{width:10px;height:10px}
::-webkit-scrollbar-thumb{background:#4a3c2d;border-radius:5px;border:2px solid var(--panel)}
::-webkit-scrollbar-track{background:transparent}
*{scrollbar-color:#4a3c2d transparent}
@media (max-width:700px){.top{padding:8px 12px}.brand b{font-size:21px}.brand .emblem{width:32px;height:32px}.top nav{margin-left:0;width:100%}.top nav>*{flex:1 1 auto;text-align:center}}
@media (prefers-reduced-motion:reduce){.lamp.busy{animation:none}}
)page";

// The emblem (the start-up picture's astrolabe, small: brass rings, the triangle and its bar, a teal
// mark above and a purple one below), the Shapetaker wordmark as at the foot of the VCV Rack panels,
// and APOTHECA.
const char* const kBrand = R"page(<svg class="emblem" viewBox="0 0 40 40" aria-hidden="true"><circle cx="20" cy="20" r="18" fill="#17120d" stroke="#c9a15a" stroke-width="1.3"/><circle cx="20" cy="20" r="14.2" fill="none" stroke="#8a6d3c" stroke-width=".7" stroke-dasharray="1 1.6"/><circle cx="20" cy="20" r="10.5" fill="none" stroke="#c9a15a" stroke-width=".8"/><path d="M20 12.2 27 25.4H13z" fill="none" stroke="#e3c47f" stroke-width="1.2" stroke-linejoin="round"/><path d="M15.4 21.2h9.2" stroke="#e3c47f" stroke-width="1.1"/><path d="M20 1.2 22.3 4.9H17.7z" fill="#00ffb4"/><path d="M20 38.8 22.3 35.1H17.7z" fill="#b400ff"/></svg><span class="words"><svg class="mark" viewBox="39.390 125.100 22.820 2.141" role="img" aria-label="Shapetaker"><path fill="currentColor" d="m 40.636498,126.14073 c -0.16369,-0.0705 -0.285044,-0.10724 -0.434622,-0.14111 -0.206023,-0.048 -0.400756,-0.127 -0.400756,-0.29351 0,-0.0169 0.0028,-0.0339 0.0056,-0.0508 0.03104,-0.15804 0.220134,-0.26811 0.437445,-0.27093 0.0028,0 0.0028,0 0.0056,0 0.180622,0 0.372533,0.0621 0.510822,0.2032 l 0.174977,-0.16369 c -0.121355,-0.13547 -0.386644,-0.27375 -0.680155,-0.27375 -0.0056,0 -0.01129,0.003 -0.01693,0.003 -0.454377,0.006 -0.660399,0.30198 -0.680155,0.48824 -0.0028,0.0169 -0.0056,0.0367 -0.0056,0.0536 0,0.2286 0.180622,0.42898 0.462844,0.50518 0.183444,0.0508 0.333022,0.079 0.530577,0.16087 0.09596,0.0423 0.194733,0.11571 0.194733,0.24553 0,0.0254 -0.0028,0.0536 -0.01129,0.0819 -0.05644,0.1778 -0.253999,0.254 -0.437444,0.26246 -0.01129,0.003 -0.0254,0.003 -0.03669,0.003 -0.259644,0 -0.533399,-0.11571 -0.62371,-0.22577 l -0.191911,0.15522 c 0.149577,0.19473 0.536222,0.30762 0.82691,0.30762 0.285045,0 0.612422,-0.13547 0.697089,-0.43744 0.01693,-0.0508 0.02258,-0.1016 0.02258,-0.14676 0,-0.2032 -0.124177,-0.37253 -0.349955,-0.46567 z m 2.473677,-0.94826 v 0.76764 h -1.114777 v -0.76764 H 41.75551 v 1.9558 h 0.239888 V 126.2 h 1.114777 v 0.94827 h 0.239889 v -1.9558 z m 1.948742,-0.0395 -1.018822,1.99531 h 0.268111 l 0.333022,-0.65194 h 0.832555 l 0.333022,0.65194 h 0.270933 z m -0.299156,1.11477 0.299155,-0.58702 0.299155,0.58702 z m 3.385253,-0.92851 c -0.112888,-0.0903 -0.259645,-0.14675 -0.417688,-0.14675 h -0.959555 v 1.9558 h 0.239888 v -0.61807 h 0.719666 c 0.369711,0 0.668866,-0.29915 0.668866,-0.66886 0,-0.21167 -0.09878,-0.39794 -0.251177,-0.52212 z m 0.01129,0.52212 c 0,0.23706 -0.191911,0.42897 -0.428977,0.42897 h -0.719666 v -0.85513 h 0.719666 c 0.237065,0 0.428977,0.19191 0.428977,0.42616 z m 1.231901,1.04704 v -0.6604 h 1.03011 v -0.23989 h -1.03011 v -0.57291 h 1.219199 v -0.24271 h -1.459088 v 1.9558 h 1.498599 v -0.23989 z m 1.849967,-1.71591 v 0.24271 h 0.694267 v 1.71309 h 0.239888 v -1.71309 h 0.694266 v -0.24271 z m 2.865963,-0.0395 -1.018821,1.99531 h 0.26811 l 0.333022,-0.65194 h 0.832555 l 0.333022,0.65194 h 0.270933 z m -0.299155,1.11477 0.299155,-0.58702 0.299156,0.58702 z m 2.727676,-0.0931 0.97931,-0.98213 h -0.341488 l -0.809977,0.80998 -0.307622,-0.30763 v -0.50235 h -0.242711 v 1.9558 h 0.242711 v -1.11478 l 1.114776,1.11478 h 0.341488 z m 1.920522,0.73378 v -0.6604 h 1.03011 v -0.23989 h -1.03011 v -0.57291 h 1.219199 v -0.24276 h -1.459089 v 1.9558 h 1.498599 v -0.23989 z m 3.706988,0.23989 -0.451555,-0.44874 c 0.231422,-0.14393 0.383822,-0.39793 0.383822,-0.69144 0,-0.44873 -0.364067,-0.81562 -0.815622,-0.81562 h -0.812799 v 1.95862 h 0.239889 v -1.13453 l 1.13171,1.13453 z m -0.307622,-1.14018 c 0,0.20038 -0.110066,0.40922 -0.318911,0.51647 l -0.829732,-0.82974 v -0.25964 h 0.57291 c 0.316089,0 0.575733,0.25682 0.575733,0.57291 z"/></svg><b>APOTHECA</b></span>)page";

// The device's own lettering (Routed Gothic Narrow, under the SIL Open Font License: the file is served
// whole and unchanged, its licence beside it in the zip), loaded from /PIN/font.ttf. The browser keeps
// it (Transfer sends it with a long Cache-Control), so a weak WiFi pays for it once. Until it comes, or
// if it never does, the browser's own letters stand in.
const char* const kFont = R"page(
(function () {
  try {
    const pin = location.pathname.split('/')[1];
    if (!pin || !window.FontFace) return;
    new FontFace('Lettering', 'url(/' + pin + '/font.ttf)').load().then(f => document.fonts.add(f), () => {});
  } catch (e) {}
})();
)page";

void fill(std::string& page, const std::string& mark, const char* with) {
    for (std::size_t at = page.find(mark); at != std::string::npos; at = page.find(mark, at)) {
        page.replace(at, mark.size(), with);
        at += std::string(with).size();
    }
}

} // namespace

std::string withWebLook(const char* page) {
    std::string out = page;
    fill(out, "@@LOOK@@", kLook);
    fill(out, "@@BRAND@@", kBrand);
    fill(out, "@@FONT@@", kFont);
    return out;
}

} // namespace apotheca
