#!/usr/bin/env python3
"""Turn Digital Dream Labs' Vector Web Setup (MIT license) into JamesOS Setup.

    patch_vws.py VECTOR_WEB_SETUP_FOLDER

Safe to run again: every change checks whether it is already there.
Only text, colors and the end screen change - the Bluetooth/Wi-Fi/OTA code stays DDL's.
"""
import json, os, re, sys

D = sys.argv[1]
def p(*a): return os.path.join(D, *a)
def read(f): return open(p(f), encoding="utf-8").read()
def write(f, s): open(p(f), "w", encoding="utf-8").write(s)

GOLD = "#e2b54a"
changed = []

# --- only one stack: JamesOS (its OTA files live in site/firmware/jamesos/)
settings = {"stacks": {"jamesos": {}}}
for f in ("default-data/settings.json",):
    if json.loads(read(f)) != settings:
        write(f, json.dumps(settings, indent=2) + "\n"); changed.append(f)

# --- the page
f = "templates/main.ejs"; s = read(f); o = s
s = s.replace("<title>Vector Setup</title>", "<title>JamesOS Setup</title>")
s = s.replace('href="/static/css/vecSetup.css" />',
              'href="/static/css/vecSetup.css" />\n    <link rel="stylesheet" type="text/css" href="/static/css/jamesos.css" />')
s = s.replace('href="/static/css/jamesos.css" />\n    <link rel="stylesheet" type="text/css" href="/static/css/jamesos.css" />',
              'href="/static/css/jamesos.css" />')
s = re.sub(r'2\. Double-press Vector\'s button and then click "PAIR WITH\s+VECTOR\."',
           "2. Hold Vector's button for 15 seconds until his screen shows a web address (recovery mode), then click \"PAIR WITH VECTOR.\"", s)
s = re.sub(r'(<div id="containerOtaComplete" class="vec-container">\s*<p>)\s*Update completed\.[^<]*(</p>)',
           r'\1JamesOS is installed! Vector restarts by himself now (a few minutes). '
           r'When his eyes show, finish setting him up at '
           r'<a href="https://v.pvic.xyz" target="_blank">v.pvic.xyz</a> (Wi-Fi and voice), like any WireOS robot.\2', s)
s = re.sub(r'No OTA\'s present in firmware folder\.[^<]*<a href="/static/data/inventory\.json">click here</a>',
           "No JamesOS OTA found. On the laptop run <code>./jamesos.sh ota</code>, then "
           "<code>./jamesos.sh setup-site</code> again.", s)
if s != o: write(f, s); changed.append(f)

# --- JamesOS logo (favicon + footer), the retry tip, the welcome and credits screens
import shutil
HERE = os.path.dirname(os.path.abspath(__file__))
src_logo = os.path.join(HERE, "jamesos-logo.svg")
if not os.path.exists(p("site/images/jamesos-logo.svg")) or open(src_logo, "rb").read() != open(p("site/images/jamesos-logo.svg"), "rb").read():
    shutil.copyfile(src_logo, p("site/images/jamesos-logo.svg")); changed.append("site/images/jamesos-logo.svg")
f = "templates/main.ejs"; s = read(f); o = s
s = s.replace('<link rel="icon" type="image/png" href="/static/images/ddl_logo_sm.png" />',
              '<link rel="icon" type="image/svg+xml" href="/static/images/jamesos-logo.svg" />')
s = s.replace('<img src="/static/images/ddl_logo.png" />', '<img src="/static/images/jamesos-logo.svg" alt="JamesOS" />')
if 'class="jos-tip"' not in s:
    s = re.sub(r'(id="btnDiscoverVector"\s*value="Pair with Vector"\s*class="vec-btn"\s*/>\s*</div>)',
               r'\1\n          <p class="jos-tip">Vector shows a <b>cloud with !</b> after an install try? '
               r'That means the download did not start. Just click <b>PAIR WITH VECTOR</b> again.</p>', s, count=1)
# footer: one short line; the full credits and licenses (DDL, Font Awesome, WireOS) are on the Credits screen
s = re.sub(r'(<div class="vec-ui-footer-middle">\s*<span>).*?(</span>\s*</div>)',
           r'\1JamesOS Setup, based on Vector Web Setup by Digital Dream Labs. '
           r'<a href="#" data-open="jamesCredits" onclick="return false">Credits and licenses</a>\2',
           s, count=1, flags=re.S)
overlay = open(os.path.join(HERE, "overlay.html"), encoding="utf-8").read()
if "<!-- JamesOS: welcome + credits screens -->" in s:
    s = re.sub(r'<!-- JamesOS: welcome \+ credits screens -->.*?<!-- /JamesOS -->\n?', lambda m: overlay, s, count=1, flags=re.S)
else:
    s = s.replace('<div id="serverIp" class="vec-hidden">', overlay + '      <div id="serverIp" class="vec-hidden">', 1)
if 'class="jos-tip"' not in s or 'id="jamesWelcome"' not in s:
    sys.exit("couldn't add the JamesOS welcome screen or tip (different Vector Web Setup version?)")
if s != o: write(f, s); changed.append(f + " (welcome, credits, logo)")

# --- when the OTA is done, show the JamesOS "installed" screen (DDL's goes back to pairing)
f = "rts-js/main.js"; s = read(f); o = s
s = re.sub(r'(// handle OTA complete\s*toggleIcon\("iconOta", true\);\s*)setPhase\("containerDiscover"\);',
           r'\1setPhase("containerOtaComplete");', s)
# Chrome no longer lets a page call http://0.0.0.0 - ask the page's own server instead
s = s.replace("url: `http://${_serverIp}:${_serverPort}/firmware`,", 'url: "/firmware",')
if 'url: "/firmware",' not in s:
    sys.exit("couldn't patch the firmware list call in rts-js/main.js (different Vector Web Setup version?)")
# online version: the page can carry its own OTA links (window.JAMESOS_OTAS) instead of asking this laptop
if "window.JAMESOS_OTAS" not in s:
    s = s.replace("function getOtasPresent(env) {\n  return new Promise((resolve, reject) => {",
                  "function getOtasPresent(env) {\n  if (window.JAMESOS_OTAS) { return Promise.resolve({ message: window.JAMESOS_OTAS }); }  // JamesOS online\n  return new Promise((resolve, reject) => {")
s = s.replace("var obj = parseURL(localUrlPrefix + endpoint);",
              "var obj = parseURL(/^https?:/.test(endpoint) ? endpoint : localUrlPrefix + endpoint);")
if "window.JAMESOS_OTAS" not in s or "/^https?:/.test(endpoint)" not in s:
    sys.exit("couldn't add the online OTA links to rts-js/main.js (different Vector Web Setup version?)")
if 'setPhase("containerOtaComplete");' not in s:
    sys.exit("couldn't find the OTA-complete step in rts-js/main.js (different Vector Web Setup version?)")
if s != o: write(f, s); changed.append(f)

# --- the robot downloads the OTA from this address: let jamesos.sh pick the right one
f = "utils/ip.js"; s = read(f); o = s
s = s.replace("const getNetworkIp = () => {\n  return address;\n};",
              "const getNetworkIp = () => {\n  return process.env.JAMESOS_LAN_IP || address;  // JamesOS\n};")
if "JAMESOS_LAN_IP" not in s:
    sys.exit("couldn't patch utils/ip.js (different Vector Web Setup version?)")
if s != o: write(f, s); changed.append(f)

# --- JamesOS colors: gold instead of DDL green
css = """/* JamesOS Setup */
/* status icons: gold when a step is done (DDL's were green) */
.vec-icon-done { filter: brightness(0) saturate(100%) invert(76%) sepia(43%) saturate(708%) hue-rotate(358deg) brightness(93%) contrast(89%); }
.vec-ui-footer-middle span:first-child { color: #999; }
.vec-ui-footer-middle a, .jos-tip a { color: #e2b54a; }
/* the old "Pair with Vector" button on the done screen does nothing - the next step is v.pvic.xyz */
#containerOtaComplete #btnDiscoverVectorAgain { display: none; }
#containerOtaComplete a { color: #e2b54a; text-decoration: underline; }
.jos-tip { margin: 18px auto 0; max-width: 34em; color: #b9b3a3; font-size: 14px; text-align: center; }
.jos-tip b { color: #e2b54a; }
/* welcome + credits screens */
.jos-overlay { position: fixed; inset: 0; z-index: 50; background: #000; display: flex; align-items: center; justify-content: center; padding: 24px 16px; overflow-y: auto; }
.jos-hidden { display: none !important; }
.jos-card { max-width: 560px; width: 100%; color: #eee; text-align: left; }
.jos-card h1 { font-size: 34px; font-weight: 700; color: #e2b54a; margin: 6px 0 4px; letter-spacing: .02em; }
.jos-logo { width: 72px; height: 72px; }
.jos-lead { color: #cfc9b8; margin: 0 0 18px; }
.jos-list, .jos-need { list-style: none; padding: 0; margin: 0 0 16px; display: grid; gap: 8px; }
.jos-list li { padding-left: 14px; border-left: 2px solid #e2b54a; color: #ccc; }
.jos-list b { display: block; color: #fff; }
.jos-need-title { margin: 8px 0 6px; font-weight: 700; color: #fff; }
.jos-need li { color: #ccc; padding-left: 14px; position: relative; }
.jos-need li::before { content: ""; position: absolute; left: 0; top: .6em; width: 6px; height: 6px; border-radius: 50%; background: #e2b54a; }
.jos-actions { display: flex; gap: 18px; align-items: center; margin-top: 18px; flex-wrap: wrap; }
.jos-link { background: none; border: 0; color: #e2b54a; text-decoration: underline; cursor: pointer; font: inherit; padding: 0; }
.jos-credits { display: grid; grid-template-columns: max-content 1fr; gap: 8px 16px; margin: 0; }
.jos-credits dt { color: #e2b54a; font-weight: 700; }
.jos-credits dd { margin: 0; color: #ccc; }
.jos-credits a { color: #e2b54a; }
@media (max-width: 560px) { .jos-credits { grid-template-columns: 1fr; } .jos-credits dd { margin-bottom: 6px; } }
"""
f = "site/css/vecSetup.css"; s = read(f); o = s
s = s.replace("#4fed94", GOLD).replace("rgba(2, 23, 11, 0.9)", "rgba(26, 20, 6, 0.92)")
if s != o: write(f, s); changed.append(f)
if not os.path.exists(p("site/css/jamesos.css")) or read("site/css/jamesos.css") != css:
    write("site/css/jamesos.css", css); changed.append("site/css/jamesos.css")

print("JamesOS Setup: " + (", ".join(changed) + " updated" if changed else "already up to date"))
