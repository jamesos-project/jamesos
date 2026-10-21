#!/usr/bin/env python3
"""JamesOS web control page.

Run from the root of wire-os-victor:   ./jamesos.sh web
Then open http://localhost:8765 in a browser on this PC.

It talks to every robot in jamesos/robots.txt over SSH (same key as jamesos.sh),
shows battery, pets, badges and version, and switches features by writing the same
small settings files the COOL FEATURES menu uses (/data/data/jamesos/...).
Only Python's standard library is used.
"""
import json
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("JAMESOS_WEB_PORT", "8765"))
# v21: only this PC can open the page (not everyone on the same Wi-Fi).
# To use it from your phone on purpose: JAMESOS_WEB_HOST=0.0.0.0 ./jamesos.sh web
HOST = os.environ.get("JAMESOS_WEB_HOST", "127.0.0.1")
OK_HOSTS = {"localhost:%d" % PORT, "127.0.0.1:%d" % PORT}
OK_ORIGINS = {"http://" + h for h in OK_HOSTS}
HERE = os.path.dirname(os.path.abspath(__file__))
ROBOTS_FILE = "jamesos/robots.txt"
PUBLISHED_FILE = "jamesos/updates/version.txt"
KEY = os.path.expanduser("~/.ssh/vector_key")
D = "/data/data/jamesos"

# same list (and order) as the badges in the robot code
BADGES = [
    ("FIRST_PET", "FIRST PET", "Pet him once"),
    ("DAY_10", "10 IN A DAY", "10 pets in one day"),
    ("DAY_50", "50 IN A DAY", "50 pets in one day"),
    ("TOTAL_100", "100 PETS", "100 pets in total"),
    ("TOTAL_500", "500 PETS", "500 pets in total"),
    ("TOTAL_1000", "1000 PETS", "1000 pets in total"),
    ("STREAK_3", "3 DAYS IN A ROW", "Pet him 3 days in a row"),
    ("STREAK_7", "7 DAYS IN A ROW", "Pet him 7 days in a row"),
    ("STREAK_30", "30 DAYS IN A ROW", "Pet him 30 days in a row"),
    ("STREAK_100", "LOYAL FRIEND", "Pet him 100 days in a row (unlocks gold sparkles)"),
    ("NIGHT_OWL", "NIGHT OWL", "Pet him between 12 and 5 AM"),
    ("EARLY_BIRD", "EARLY BIRD", "Pet him between 5 and 7 AM"),
    ("SPOOKY", "SPOOKY FRIEND", "Pet him in the Halloween theme"),
    ("HALLOWEEN", "HALLOWEEN NIGHT", "Pet him on Oct 31 after 6 PM"),
    ("KHMER_NY", "KHMER NEW YEAR", "Pet him April 14 to 16"),
    ("CHRISTMAS", "CHRISTMAS", "Pet him on Dec 25"),
    ("NEW_YEAR", "HAPPY NEW YEAR", "Pet him on Jan 1"),
    ("NY_MIDNIGHT", "NEW YEAR MIDNIGHT", "Pet him on Jan 1 between 12 and 1 AM"),
    ("CNY", "CHINESE NEW YEAR", "Pet him on Chinese New Year"),
    ("VALENTINE", "VALENTINE'S DAY", "Pet him on Feb 14"),
    # easter eggs: shown as ??? until found
    ("TICKLISH", "TICKLISH", "A secret. Keep looking"),
    ("SHY_GUY", "SHY GUY", "A secret. Keep looking"),
    ("SECRET_ROOM", "SECRET ROOM", "A secret. Keep looking"),
    ("SHINY", "SHINY VECTOR", "A secret. Keep looking"),
    ("BLUE_MOON", "BLUE MOON", "Pet him under a blue moon (the 2nd full moon in a month)"),
]
SECRET_BADGES = {"TICKLISH", "SHY_GUY", "SECRET_ROOM", "SHINY"}
# v23: James's private badges (only shown when his private extras are on)
if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "jamesos", "private.on")):
    BADGES += [("FAMILY", "FAMILY REUNION", "A secret. Keep looking"), ("PROTOTYPE", "PROTOTYPE", "A secret. Keep looking")]
    SECRET_BADGES |= {"FAMILY", "PROTOTYPE"}

STATE_CMD = r"""
D=/data/data/jamesos
echo "ver=$(head -n 1 /anki/etc/jamesos-version 2>/dev/null || cat /data/jamesos-update/version 2>/dev/null)"
if [ -f /dev/shm/jamesos_status ]; then
  echo "status_age=$(( $(date +%s) - $(stat -c %Y /dev/shm/jamesos_status 2>/dev/null || echo 0) ))"
  cat /dev/shm/jamesos_status
fi
for f in starlight.conf starlight.off hearts.on mood.on lights.off sleepy.off funeyes.off funeyes.auto; do [ -e $D/$f ] && echo "has_$f=1"; done
[ -e /data/data/customBackpackLights/off.json ] && echo "lights_sent=1"
[ -e $D/lights-halloween/off.json ] && echo "halloween_lights=1"
echo "style=$(head -n 1 $D/starlight.style 2>/dev/null)"
echo "petfx=$(head -n 1 $D/petfx 2>/dev/null)"
echo "theme=$(head -n 1 $D/theme 2>/dev/null)"
echo "holidays=$(head -n 1 $D/holidays 2>/dev/null)"
echo "sleep=$(head -n 1 $D/sleepscreen 2>/dev/null)"
echo "pets=$(cat $D/pets.txt 2>/dev/null)"
sed 's/^/stat_/' $D/stats.txt 2>/dev/null
sed 's/^/badge=/' $D/badges.txt 2>/dev/null
echo "host=$(hostname)"
echo "today=$(date +%Y%m%d)"
"""

PREP = "mkdir -p {D} && chmod 777 {D}".format(D=D)
STAR_CONF_DEFAULT = r"enabled=1\nstars=3\nchance=0.04\nlife=24\nsize=2\ncolor=255,255,255\n"
RESTART_IF_LIGHTS = ("if [ -e /data/data/customBackpackLights/off.json ] && [ ! -e {D}/lights.off ] "
                     "&& [ -e {D}/lights-halloween/off.json ]; then systemctl restart anki-robot.target; fi").format(D=D)

THEMES = ("auto", "normal", "halloween", "christmas", "lunar", "newyear", "valentine", "stpatrick", "aprilfools",
          "easter", "mothersday", "fireworks", "thanksgiving", "glitch", "retro", "galaxymax")

# every setting the page can change: value -> shell command on the robot
SETTINGS = {
    "stars": {
        "off": PREP + " && touch {D}/starlight.off && chmod 666 {D}/starlight.off",
        **{style: (PREP + " && echo {s} > {D}/starlight.style && chmod 666 {D}/starlight.style"
                   " && rm -f {D}/starlight.off"
                   " && ( [ -e {D}/starlight.conf ] || printf '" + STAR_CONF_DEFAULT + "' > {D}/starlight.conf )"
                   " && chmod 666 {D}/starlight.conf").replace("{s}", style)
           for style in ("rainbow", "white", "shooting", "gold")},
    },
    "petfx": {
        "off": "rm -f {D}/hearts.on",
        "galaxy": PREP + " && touch {D}/hearts.on && echo galaxy > {D}/petfx && chmod 666 {D}/hearts.on {D}/petfx",
    },
    "mood": {
        "on": PREP + " && touch {D}/mood.on && chmod 666 {D}/mood.on",
        "off": "rm -f {D}/mood.on",
    },
    "funeyes": {
        "on": "rm -f {D}/funeyes.off {D}/funeyes.auto",
        "auto": PREP + " && rm -f {D}/funeyes.off && touch {D}/funeyes.auto && chmod 666 {D}/funeyes.auto",
        "off": PREP + " && rm -f {D}/funeyes.auto && touch {D}/funeyes.off && chmod 666 {D}/funeyes.off",
    },
    "sleepy": {
        "on": "rm -f {D}/sleepy.off",
        "off": PREP + " && touch {D}/sleepy.off && chmod 666 {D}/sleepy.off",
    },
    "theme": {
        # v23: the back lights follow the theme by themselves - no restart
        t: (PREP + " && echo {t} > {D}/theme && chmod 666 {D}/theme").replace("{t}", t)
        for t in THEMES
    },
    "holidays": {h: (PREP + " && echo {h} > {D}/holidays && chmod 666 {D}/holidays").replace("{h}", h)
                 for h in ("all", "western", "off")},
    "sleep": {
        "off": "rm -f {D}/sleepscreen",
        **{m: (PREP + " && echo {m} > {D}/sleepscreen && chmod 666 {D}/sleepscreen").replace("{m}", m)
           for m in ("stars", "clock", "both")},
    },
    "lights": {
        "custom": "[ -e /data/data/customBackpackLights/off.json ] || { echo 'NOT_SENT'; exit 3; }; "
                  "rm -f {D}/lights.off && systemctl restart anki-robot.target",
        "normal": "[ -e /data/data/customBackpackLights/off.json ] || { echo 'NOT_SENT'; exit 3; }; "
                  + PREP + " && touch {D}/lights.off && chmod 666 {D}/lights.off && systemctl restart anki-robot.target",
    },
}

ACTIONS = {"restart": "systemctl restart anki-robot.target",
           "poweroff": "touch /dev/shm/jamesos_poweroff"}  # vic-robot powers him off (JamesOS v16)

_cache = {}
_cache_lock = threading.Lock()
_pool = ThreadPoolExecutor(max_workers=8)


def robots():
    out = []
    try:
        with open(ROBOTS_FILE) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                out.append({"ip": parts[0], "name": parts[1] if len(parts) > 1 else parts[0]})
    except FileNotFoundError:
        pass
    return out


def ssh(ip, cmd, timeout=20):
    args = ["ssh", "-i", KEY, "-o", "StrictHostKeyChecking=no", "-o", "LogLevel=ERROR",
            "-o", "ConnectTimeout=4", "-o", "BatchMode=yes", "root@" + ip, cmd]
    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return 255, "", "timeout"


def parse_state(ip, name, text):
    kv, badge_dates = {}, {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        k, v = line.split("=", 1)
        if k == "badge":
            parts = v.split()
            if len(parts) >= 1:
                badge_dates[parts[0]] = parts[1] if len(parts) > 1 else "1"
        else:
            kv[k.strip()] = v.strip()

    def num(key, cast=int, default=None):
        try:
            return cast(kv[key])
        except (KeyError, ValueError):
            return default

    volts = num("volts", float)
    age = num("status_age", int)
    fresh = age is not None and age <= 60
    pct = None
    if fresh and kv.get("charger") == "1" and kv.get("charging") == "0":
        pct = 100   # charging finished: Vector rests the battery and the voltage reading isn't real (v21)
    elif volts and volts > 1 and fresh:
        pct = int(max(0, min(100, (volts - 3.6) / (4.1 - 3.6) * 100)))
    pets_today = 0
    pets = kv.get("pets", "").split()
    if len(pets) == 2 and pets[0] == kv.get("today", ""):
        pets_today = int(pets[1]) if pets[1].isdigit() else 0

    if "has_starlight.conf" not in kv or "has_starlight.off" in kv:
        stars = "off"
    else:
        stars = kv.get("style") if kv.get("style") in ("rainbow", "shooting", "gold") else "white"
    if "has_hearts.on" not in kv:
        petfx = "off"
    else:
        petfx = "galaxy"
    sleep = kv.get("sleep") if kv.get("sleep") in ("stars", "clock", "both") else "off"
    lights = ("custom" if "has_lights.off" not in kv else "normal") if "lights_sent" in kv else "not sent"

    def fmt_date(d):
        return "%s-%s-%s" % (d[:4], d[4:6], d[6:8]) if len(d) == 8 and d.isdigit() and d > "19000101" else ""

    return {
        "ip": ip, "name": name, "online": True, "host": kv.get("host", ""),
        "version": kv.get("ver") or "", "volts": volts if fresh else None, "pct": pct,
        "charger": (kv.get("charger") == "1") if fresh else None,
        "pets_today": pets_today, "total": num("stat_total", int, 0),
        "streak": num("stat_streak", int, 0), "best": num("stat_best", int, 0),
        # secret badges stay "???" until found (the name isn't even sent to the page)
        "badges": [{"key": k if (k in badge_dates or k not in SECRET_BADGES) else "",
                    "name": n if (k in badge_dates or k not in SECRET_BADGES) else "???",
                    "hint": h, "got": k in badge_dates, "secret": k in SECRET_BADGES,
                    "date": fmt_date(badge_dates.get(k, ""))} for k, n, h in BADGES],
        "settings": {"stars": stars, "petfx": petfx, "mood": "on" if "has_mood.on" in kv else "off",
                     "sleepy": "off" if "has_sleepy.off" in kv else "on",
                     "funeyes": "off" if "has_funeyes.off" in kv else ("auto" if "has_funeyes.auto" in kv else "on"),
                     "theme": kv.get("theme") if kv.get("theme") in THEMES else "auto",
                     "holidays": kv.get("holidays") if kv.get("holidays") in ("all", "western", "off") else "all",
                     "sleep": sleep, "lights": lights},
    }


def fetch(robot, max_age=5.0):
    ip = robot["ip"]
    with _cache_lock:
        hit = _cache.get(ip)
        if hit and time.time() - hit[0] < max_age:
            return hit[1]
    rc, out, err = ssh(ip, STATE_CMD, timeout=12)
    if rc == 0:
        state = parse_state(ip, robot["name"], out)
    else:
        state = {"ip": ip, "name": robot["name"], "online": False}
    with _cache_lock:
        _cache[ip] = (time.time(), state)
    return state


def forget(ip):
    with _cache_lock:
        _cache.pop(ip, None)


def published():
    try:
        with open(PUBLISHED_FILE) as f:
            return f.read().strip()
    except FileNotFoundError:
        return ""


def targets(target):
    known = robots()
    if target == "all":
        return known
    return [r for r in known if r["ip"] == target]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # keep the window quiet
        pass

    def send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def safe(self, post=False):
        """Block other websites and other computers from using the page.
        - Host must be localhost (stops "DNS rebinding" tricks)
        - changes need our own header, which other websites can't send without asking first
        - if the browser says where the request came from, it must be this page"""
        if HOST in ("127.0.0.1", "localhost") and self.headers.get("Host", "") not in OK_HOSTS:
            return False
        if post:
            if self.headers.get("X-JamesOS") != "1":
                return False
            origin = self.headers.get("Origin")
            if origin and HOST in ("127.0.0.1", "localhost") and origin not in OK_ORIGINS:
                return False
        return True

    def do_GET(self):
        if not self.safe():
            return self.send(403, {"error": "not allowed"})
        if self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html; charset=utf-8")
        if self.path.startswith("/api/state"):
            fresh = "fresh=1" in self.path
            rs = robots()
            states = list(_pool.map(lambda r: fetch(r, 0 if fresh else 5.0), rs))
            return self.send(200, {"published": published(), "robots": states, "time": time.strftime("%H:%M:%S")})
        self.send(404, {"error": "not found"})

    def do_POST(self):
        if not self.safe(post=True):
            return self.send(403, {"error": "not allowed"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            req = json.loads(self.rfile.read(length) or b"{}")
        except (ValueError, json.JSONDecodeError):
            return self.send(400, {"error": "bad request"})
        rs = targets(str(req.get("target", "")))
        if not rs:
            return self.send(400, {"error": "unknown robot"})

        if self.path == "/api/set":
            setting, value = str(req.get("setting", "")), str(req.get("value", ""))
            cmd = SETTINGS.get(setting, {}).get(value)
            if cmd is None:
                return self.send(400, {"error": "unknown setting"})
            cmd = cmd.replace("{D}", D) + " && sync"

            def run(r):
                rc, out, _ = ssh(r["ip"], cmd)
                forget(r["ip"])
                if "NOT_SENT" in out:
                    return {"ip": r["ip"], "ok": False, "msg": "lights not sent yet (./jamesos.sh lights)"}
                return {"ip": r["ip"], "ok": rc == 0, "msg": "" if rc == 0 else ("offline" if rc == 255 else "didn't work")}
            return self.send(200, {"results": list(_pool.map(run, rs))})

        if self.path == "/api/action":
            action = str(req.get("action", ""))
            if action == "update":
                results = []
                for r in rs:
                    os.makedirs("jamesos/updates", exist_ok=True)
                    log = open("jamesos/updates/web-update-%s.log" % r["ip"], "w")
                    subprocess.Popen(["./jamesos.sh", "update-now", r["ip"]], stdout=log, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL, start_new_session=True)
                    forget(r["ip"])
                    results.append({"ip": r["ip"], "ok": True, "msg": "update started (3-5 min)"})
                return self.send(200, {"results": results})
            cmd = ACTIONS.get(action)
            if cmd is None:
                return self.send(400, {"error": "unknown action"})

            def run(r):
                rc, _, _ = ssh(r["ip"], cmd)
                forget(r["ip"])
                return {"ip": r["ip"], "ok": rc == 0, "msg": "" if rc == 0 else "offline"}
            return self.send(200, {"results": list(_pool.map(run, rs))})
        self.send(404, {"error": "not found"})


def main():
    if not os.path.exists("jamesos.sh"):
        print("Run this from the wire-os-victor folder:  ./jamesos.sh web")
        sys.exit(1)
    try:
        server = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError:
        print("The JamesOS web page is already running: http://localhost:%d" % PORT)
        return
    print("JamesOS web control page: http://localhost:%d   (Ctrl+C stops it)" % PORT)
    if HOST not in ("127.0.0.1", "localhost"):
        print("  WARNING: anyone on this network can open it (JAMESOS_WEB_HOST=%s)" % HOST)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
