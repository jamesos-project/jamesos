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

GOLD = "#e2b54a"   # the old JamesOS color (earlier versions of this patch)
GOLD2 = "#F2B544"   # JamesOS gold (v28 look)
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
               r'That means the download did not start. Just click <b>Pair with Vector</b> again.</p>', s, count=1)
# the steps name the button the way it's written on it
s = re.sub(r'click "PAIR WITH\s+VECTOR\."', 'click <b>Pair with Vector</b>.', s)
s = s.replace("<b>PAIR WITH VECTOR</b>", "<b>Pair with Vector</b>")
# footer: one short line; the full credits and licenses (DDL, Font Awesome, WireOS) are on the Credits screen
s = re.sub(r'(<div class="vec-ui-footer-middle">\s*<span>).*?(</span>\s*</div>)',
           r'\1JamesOS Setup, based on Vector Web Setup by Digital Dream Labs. '
           r'<a href="#" data-open="jamesCredits" onclick="return false">Credits and licenses</a> · '
           r'<a href="https://github.com/jamesos-project/jamesos" target="_blank" rel="noopener">Source code</a>\2',
           s, count=1, flags=re.S)
overlay = open(os.path.join(HERE, "overlay.html"), encoding="utf-8").read()
if "<!-- JamesOS: welcome + credits screens -->" in s:
    s = re.sub(r'<!-- JamesOS: welcome \+ credits screens -->.*?<!-- /JamesOS -->\n?', lambda m: overlay, s, count=1, flags=re.S)
else:
    s = s.replace('<div id="serverIp" class="vec-hidden">', overlay + '      <div id="serverIp" class="vec-hidden">', 1)
if 'class="jos-tip"' not in s or 'id="jamesWelcome"' not in s:
    sys.exit("couldn't add the JamesOS welcome screen or tip (different Vector Web Setup version?)")
if 'class="jos-done-tip"' not in s:
    s = re.sub(r'(<div id="containerOtaComplete" class="vec-container">\s*<p>.*?</p>)',
               r'\1\n          <p class="jos-done-tip">Tip: on a phone on the same Wi-Fi, open '
               r'<b>http://HIS-IP:8889/jamesos/</b> to change his settings.</p>', s, count=1, flags=re.S)
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
# v29: "Just change his Wi-Fi" - always show the Wi-Fi list, and stop after Wi-Fi (no OTA)
if "JAMESOS_WIFI_ONLY" not in s:
    s = s.replace('      toggleIcon("iconWifi", true);\n      setupOTAFiles();',
                  '      toggleIcon("iconWifi", true);\n      if (window.JAMESOS_WIFI_ONLY) { window.jamesWifiDone(ssid); return; }  // JamesOS\n      setupOTAFiles();', 1)
    s = s.replace('      if (m.value.wifiState == 1 || m.value.wifiState == 2) {\n        toggleIcon("iconWifi", true);\n        // skip wifi scan',
                  '      if (!window.JAMESOS_WIFI_ONLY && (m.value.wifiState == 1 || m.value.wifiState == 2)) {  // JamesOS\n        toggleIcon("iconWifi", true);\n        // skip wifi scan', 1)
if s.count("JAMESOS_WIFI_ONLY") < 2:
    sys.exit("couldn't add 'Just change his Wi-Fi' to rts-js/main.js (different Vector Web Setup version?)")
if s != o: write(f, s); changed.append(f)

# --- the robot downloads the OTA from this address: let jamesos.sh pick the right one
f = "utils/ip.js"; s = read(f); o = s
s = s.replace("const getNetworkIp = () => {\n  return address;\n};",
              "const getNetworkIp = () => {\n  return process.env.JAMESOS_LAN_IP || address;  // JamesOS\n};")
if "JAMESOS_LAN_IP" not in s:
    sys.exit("couldn't patch utils/ip.js (different Vector Web Setup version?)")
if s != o: write(f, s); changed.append(f)

# --- JamesOS look: graphite and gold like the phone page (jamesos.css)
css = """/* JamesOS Setup - graphite and gold, like the JamesOS phone page */
@import url("https://fonts.googleapis.com/css2?family=Rubik:wght@400;500;600&display=swap");
:root { --jos-bg:#16181B; --jos-panel:#202327; --jos-raised:#2A2E33; --jos-edge:#30343A;
  --jos-text:#F3F1EC; --jos-muted:#A3A49F; --jos-faint:#74777A; --jos-gold:#F2B544; --jos-ink:#2B1A00; --jos-tint:rgba(242,181,68,.14); }
body, .vec-panel-container { background: var(--jos-bg) !important; color: var(--jos-text);
  font-family: Rubik, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif !important; -webkit-font-smoothing: antialiased; }
a { color: var(--jos-gold); }
:focus-visible { outline: 2px solid var(--jos-gold); outline-offset: 3px; }

/* status icons at the top: small and quiet, gold when a step is done */
.vec-status { gap: 6px; padding-top: 16px; }
.vec-icon { width: 22px; margin: 4px 10px; }
.vec-icon-done { filter: opacity(100%) brightness(0) saturate(100%) invert(76%) sepia(43%) saturate(708%) hue-rotate(358deg) brightness(97%) contrast(92%); }
.vec-info-box { border: 1px solid var(--jos-edge) !important; background: var(--jos-panel) !important; border-radius: 999px; font-size: 13px; }

/* the steps */
#discoverFirstTime:not(.vec-hidden), #discoverReconnect:not(.vec-hidden) { display: flex; flex-direction: column; max-width: 600px;
  margin: 0 auto 24px; background: var(--jos-panel); border-radius: 22px; overflow: hidden; }
.vec-instruction { height: auto !important; align-items: center; gap: 18px; padding: 18px 20px; text-align: left; }
.vec-instruction + .vec-instruction { border-top: 1px solid var(--jos-edge); }
.vec-instruction > div { line-height: 1.5 !important; font-size: 16px; }
.vec-card { width: 56px !important; padding: 0 !important; opacity: .85; flex-shrink: 0; }

/* buttons and fields */
.vec-btn, .vec-field[type="button"] { background: var(--jos-gold) !important; color: var(--jos-ink) !important; border: 0 !important;
  border-radius: 999px !important; height: 50px !important; min-width: 240px; font-weight: 600; text-transform: none !important;
  font-size: 16px; cursor: pointer; }
.vec-btn:hover, .vec-field[type="button"]:hover { filter: brightness(1.06); color: var(--jos-ink) !important; border: 0 !important; }
.vec-field { background: var(--jos-panel) !important; border: 1px solid var(--jos-edge) !important; border-radius: 14px !important;
  height: 50px !important; color: var(--jos-text) !important; }
.vec-field:focus { border-color: var(--jos-gold) !important; color: var(--jos-text) !important; }
#txtPin { font-size: 22px; letter-spacing: .3em; text-align: center; }

/* tip under PAIR WITH VECTOR */
.jos-tip { margin: 20px auto 0; max-width: 34em; color: var(--jos-muted); font-size: 14px; text-align: center; line-height: 1.5; }
.jos-tip b { color: var(--jos-text); font-weight: 500; }

/* footer: one quiet line, no logos */
.vec-ui-footer-side img { display: none; }
.vec-ui-footer-middle span:first-child { color: var(--jos-faint); }
.vec-ui-footer-middle a { color: var(--jos-muted); text-decoration: underline; }

/* done screen */
#containerOtaComplete #btnDiscoverVectorAgain { display: none; }
#containerOtaComplete p { max-width: 34em; margin: 0 auto 10px; line-height: 1.55; }
.jos-done-tip { color: var(--jos-muted); font-size: 14px; }

/* welcome + credits */
.jos-overlay { position: fixed; inset: 0; z-index: 50; background: var(--jos-bg); display: flex; justify-content: center;
  padding: 0 24px 48px; overflow-y: auto; }
.jos-hidden { display: none !important; }
.jos-card { max-width: 640px; width: 100%; color: var(--jos-text); text-align: left; }
.jos-card.jos-wide { max-width: 1040px; }
.jos-top { display: flex; align-items: center; justify-content: space-between; height: 76px; margin-bottom: 40px; }
.jos-word { font-size: 19px; font-weight: 600; letter-spacing: -.01em; }
.jos-card h1 { font-size: 44px; line-height: 1.08; font-weight: 600; letter-spacing: -.025em; color: var(--jos-text); margin: 0 0 14px; max-width: 14em; }
.jos-lead { color: var(--jos-muted); margin: 0 0 30px; font-size: 18px; line-height: 1.5; max-width: 30em; }
.jos-hero { display: grid; grid-template-columns: 1.15fr 1fr; gap: 48px; align-items: center; }
.jos-check { background: var(--jos-panel); border-radius: 22px; padding: 24px; }
.jos-check-title { margin: 0 0 16px; font-weight: 500; font-size: 17px; }
.jos-check ul { list-style: none; padding: 0; margin: 0; display: grid; gap: 14px; }
.jos-check li { display: flex; gap: 12px; align-items: flex-start; color: var(--jos-muted); font-size: 15px; line-height: 1.45; }
.jos-check li b { color: var(--jos-text); font-weight: 500; }
.jos-tick { width: 22px; height: 22px; border-radius: 50%; background: var(--jos-tint); flex-shrink: 0; position: relative; }
.jos-tick::after { content: ""; position: absolute; left: 8px; top: 5px; width: 5px; height: 9px; border: solid var(--jos-gold); border-width: 0 2px 2px 0; transform: rotate(45deg); }
.jos-primary { display: inline-block; min-width: 220px; height: 54px; padding: 0 36px; border: 0; border-radius: 27px; background: var(--jos-gold); color: var(--jos-ink);
  font: inherit; font-size: 17px; font-weight: 600; cursor: pointer; }
.jos-primary:hover { filter: brightness(1.06); }
.jos-secondary { display: inline-block; min-width: 200px; height: 50px; margin-top: 24px; border: 1px solid var(--jos-edge); border-radius: 25px;
  background: transparent; color: var(--jos-text); font: inherit; font-weight: 500; cursor: pointer; }
.jos-small { margin: 16px 0 0; font-size: 14px; color: var(--jos-faint); }
.jos-small a { color: var(--jos-muted); }
.jos-link { background: none; border: 0; color: var(--jos-muted); cursor: pointer; font: inherit; font-size: 15px; padding: 8px 0; }
.jos-link:hover { color: var(--jos-text); text-decoration: underline; }
.jos-features { list-style: none; padding: 32px 0 0; margin: 56px 0 0; border-top: 1px solid var(--jos-edge);
  display: grid; grid-template-columns: repeat(4, 1fr); gap: 28px; }
.jos-features b { display: block; font-weight: 500; font-size: 16px; margin-bottom: 4px; }
.jos-features span { color: var(--jos-muted); font-size: 14px; line-height: 1.45; }
.jos-mark { width: 28px; height: 28px; vertical-align: -7px; margin-right: 8px; }
.jos-new { margin: 40px 0 0; padding: 24px; background: var(--jos-panel); border-radius: 22px; }
.jos-new h2 { margin: 0 0 12px; font-size: 18px; font-weight: 500; color: var(--jos-text); }
.jos-new ul { margin: 0; padding-left: 20px; color: var(--jos-muted); font-size: 15px; line-height: 1.7; }
.jos-new li::marker { color: var(--jos-gold); }
.jos-credits { margin: 0; background: var(--jos-panel); border-radius: 22px; overflow: hidden; }
.jos-credits > div { display: grid; grid-template-columns: 190px 1fr; gap: 16px; padding: 16px 20px; border-top: 1px solid var(--jos-edge); }
.jos-credits > div:first-child { border-top: 0; }
.jos-credits dt { color: var(--jos-text); font-weight: 500; font-size: 15px; }
.jos-credits dd { margin: 0; color: var(--jos-muted); font-size: 15px; line-height: 1.5; }
.jos-actions { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; }
.jos-actions .jos-secondary { margin-top: 0; height: 54px; padding: 0 28px; }
.jos-actions .jos-secondary:hover { border-color: var(--jos-muted); }
@media (max-width: 860px) {
  .jos-hero { grid-template-columns: 1fr; gap: 28px; }
  .jos-features { grid-template-columns: 1fr 1fr; }
}
@media (max-width: 560px) {
  .jos-overlay { padding: 0 20px 40px; }
  .jos-top { height: 64px; margin-bottom: 20px; }
  .jos-card h1 { font-size: 34px; }
  .jos-lead { font-size: 16px; }
  .jos-primary, .jos-secondary { width: 100%; }
  .jos-features { grid-template-columns: 1fr; gap: 18px; margin-top: 36px; }
  .jos-credits > div { grid-template-columns: 1fr; gap: 4px; }
  .vec-instruction { padding: 16px; }
}
"""
f = "site/css/vecSetup.css"; s = read(f); o = s
s = s.replace("#4fed94", GOLD2).replace("#4ADE80", GOLD2).replace(GOLD, GOLD2)
s = s.replace("rgba(2, 23, 11, 0.9)", "rgba(22, 24, 27, 0.94)").replace("rgba(26, 20, 6, 0.92)", "rgba(22, 24, 27, 0.94)").replace("rgba(6, 20, 12, 0.92)", "rgba(22, 24, 27, 0.94)")
if s != o: write(f, s); changed.append(f)
if not os.path.exists(p("site/css/jamesos.css")) or read("site/css/jamesos.css") != css:
    write("site/css/jamesos.css", css); changed.append("site/css/jamesos.css")

print("JamesOS Setup: " + (", ".join(changed) + " updated" if changed else "already up to date"))
