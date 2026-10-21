#!/usr/bin/env python3
"""Make the online JamesOS Setup (a plain website, no laptop needed) from the patched Vector Web Setup.

    build_online.py VECTOR_WEB_SETUP_FOLDER OUT_FOLDER OTA_URL [OTA_URL ...]

The page gets the OTA links built in; the robot downloads JamesOS straight from them.
OUT_FOLDER gets index.html + static/ (works on GitHub Pages, also in a sub-folder like /setup/).
"""
import json, os, shutil, sys

src, out, urls = sys.argv[1], sys.argv[2], sys.argv[3:]
if not urls or not all(u.startswith(("http://", "https://")) and u.endswith(".ota") for u in urls):
    sys.exit("give the full OTA link(s), ending in .ota")
site = os.path.join(src, "site")
if not os.path.exists(os.path.join(site, "js", "rts.js")):
    sys.exit("the setup site isn't built yet - run ./jamesos.sh setup-site once first")

# fresh copy of the site files (no OTA files, no laptop-only data)
st = os.path.join(out, "static")
if os.path.exists(st):
    shutil.rmtree(st)
shutil.copytree(site, st, ignore=shutil.ignore_patterns("firmware", "inventory.json"))

# the page script: absolute /static/ paths -> relative, so it also works under /setup/
rts = os.path.join(st, "js", "rts.js")
t = open(rts, encoding="utf-8").read()
open(rts, "w", encoding="utf-8").write(t.replace('"/static/', '"static/').replace("'/static/", "'static/"))

page = open(os.path.join(src, "templates", "main.ejs"), encoding="utf-8").read()
for tag in ("<%= serverIp %>", "<%= networkIp %>", "<%= port %>"):
    page = page.replace(tag, "")
if "<%" in page:
    sys.exit("the page still has template tags - send Claude a screenshot")
page = page.replace('"/static/', '"static/').replace("'/static/", "'static/")
otas = "<script>window.JAMESOS_OTAS = " + json.dumps(urls) + ";</script>\n    "
page = page.replace('<script src="static/js/rts.js"></script>', otas + '<script src="static/js/rts.js"></script>', 1)
if "window.JAMESOS_OTAS" not in page:
    sys.exit("couldn't add the OTA link to the page - send Claude a screenshot")
open(os.path.join(out, "index.html"), "w", encoding="utf-8").write(page)
open(os.path.join(out, ".nojekyll"), "w").close()
lic = os.path.join(src, "LICENSE")
if os.path.exists(lic):
    shutil.copyfile(lic, os.path.join(out, "LICENSE-vector-web-setup.txt"))
readme = os.path.join(out, "README.md")
if not os.path.exists(readme) or os.path.getsize(readme) < 200:   # new or GitHub's one-line README
    open(readme, "w", encoding="utf-8").write("""# JamesOS Setup (beta)

Install JamesOS, custom firmware for Anki Vector built on WireOS, from Chrome.

**Open the site:** the GitHub Pages link of this repo.

## Before you start

- Your Vector must be **unlocked** (it has run WireOS or another custom firmware before).
  A never-modded Vector: install WireOS first from https://v.pvic.xyz
- Use **Google Chrome** on a computer with Bluetooth.
- Vector on his charger, on Wi-Fi.

## Steps

1. Open the site and click **Start**.
2. Hold Vector's button for 15 seconds on the charger (recovery mode).
3. Click **PAIR WITH VECTOR**, type the code from his screen, pick Wi-Fi.
4. Wait while JamesOS downloads and installs. He restarts by himself.
5. Finish setup at https://v.pvic.xyz

Cloud with ! on his screen: click PAIR WITH VECTOR again.

## Credits and licenses

- Based on Vector Web Setup, (c) 2019-2020 Digital Dream Labs, MIT License (see LICENSE-vector-web-setup.txt).
- JamesOS is built on WireOS by Kerigan Creighton (kercre123) and the os-vector contributors, GPL-3.0.
  The source code of the GPL-3.0 parts of JamesOS is available on request.
- Icons: Font Awesome Free, CC BY 4.0.
- Vector, Anki and Digital Dream Labs are trademarks of their owners.
""")
print("Online JamesOS Setup made in %s (installs: %s)" % (out, ", ".join(u.rsplit("/", 1)[1] for u in urls)))
