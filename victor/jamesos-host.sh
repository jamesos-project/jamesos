#!/usr/bin/env bash
# JamesOS OTA host: puts your JamesOS OTA files on your own server (DigitalOcean) with a plain
# http:// link, so Vectors in recovery mode (old v0.9.3) can download them. GitHub's https
# links don't work there (error 203: could not open URL).
#
#   ./jamesos-host.sh setup IP     once: set up the server (web server for the OTA files + firewall)
#   ./jamesos-host.sh upload       put your newest OTA on it and print the link
#   ./jamesos-host.sh site         upload + make the online JamesOS Setup use that link
#   ./jamesos-host.sh list         show which OTA files are on the server
#   ./jamesos-host.sh test         check that the link works the way Vector needs it
#
# Auto-update (robots on JamesOS 108d or newer ask your server every hour):
#   ./jamesos-host.sh release      give your newest OTA to every older JamesOS robot automatically
#                                  (test it on your own robots FIRST with ./jamesos.sh ota-install)
#   ./jamesos-host.sh pause        stop handing out updates right away (robots keep what they have)
#   ./jamesos-host.sh resume       hand out the last released version again
#   ./jamesos-host.sh status       which version is released + which robots asked recently
#
# The server IP is kept in jamesos/ota-host.txt (not in git).

cd "$(dirname "$0")" || exit 1
HOSTFILE=jamesos/ota-host.txt
WOS="${WIREOS_DIR:-$HOME/wire-os}"
mkdir -p jamesos

H=""
need_host() {
  H=$(cat "$HOSTFILE" 2>/dev/null)
  [[ "$H" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "No server yet. Run first:  ./jamesos-host.sh setup 128.199.x.x   (your droplet's real IPv4 from DigitalOcean)"; exit 1; }
}
rs() { ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 "root@$H" "$@"; }

# Runs on the server: every older JamesOS version gets VER. "Older" = every version released
# before (released.txt) + every lower build number with the same start (3.0.2.100d..113d for
# 3.0.2.114d), so nobody has to add versions by hand. Newer test builds are never touched.
LINK_OLDER=$(cat <<'EOF2'
set -e
VER="$1"; W=/var/www/jamesos; V=$W/vic
[ -f "$W/jamesos-$VER.ota" ] || { echo "jamesos-$VER.ota is not on the server"; exit 1; }
mkdir -p "$V/diff"; touch "$V/released.txt"
grep -qx "$VER" "$V/released.txt" || echo "$VER" >> "$V/released.txt"
echo "$VER" > "$V/latest.txt"
rm -f "$V"/diff/*.ota
pre=$(echo "$VER" | sed -n 's/^\(.*\.\)\([0-9][0-9]*\)\([a-z]*\)$/\1/p')
num=$(echo "$VER" | sed -n 's/^\(.*\.\)\([0-9][0-9]*\)\([a-z]*\)$/\2/p')
suf=$(echo "$VER" | sed -n 's/^\(.*\.\)\([0-9][0-9]*\)\([a-z]*\)$/\3/p')
{ cat "$V/released.txt"
  if [ -n "$num" ]; then i=1; while [ "$i" -lt "$num" ]; do echo "$pre$i$suf"; i=$((i+1)); done; fi
} | sort -u | while read -r old; do
  [ -z "$old" ] && continue; [ "$old" = "$VER" ] && continue
  ln -sfn "../../jamesos-$VER.ota" "$V/diff/$old.ota"
done
rm -f "$V/paused"
n=$(ls "$V/diff" | wc -l)
echo "   every older JamesOS version ($n of them) now gets $VER - no need to add versions by hand"
EOF2
)

newest_ota() {
  local o; o=$(ls -t "$WOS"/_build/vicos-*.ota 2>/dev/null | head -1)
  [[ -f "$o" ]] || { echo "No OTA file yet. Build one first: ./jamesos.sh ota" >&2; exit 1; }
  echo "$o"
}

upload() {
  local ota name url
  need_host
  ota=$(newest_ota) || exit 1
  name="jamesos-$(basename "$ota" .ota | sed 's/^vicos-//').ota"
  echo "== Uploading $(basename "$ota") as $name ($(du -h "$ota" | cut -f1)) =="
  rsync -h --progress --partial "$ota" "root@$H:/var/www/jamesos/$name.part" \
    || { echo "Upload failed - check your internet and run it again (it continues where it stopped)."; exit 1; }
  rs "mv -f /var/www/jamesos/$name.part /var/www/jamesos/$name && chmod 644 /var/www/jamesos/$name" || exit 1
  url="http://$H/$name"
  echo "$url" > jamesos/ota-link.txt
  echo
  echo "Done. Your OTA link:  $url"
}

case "$1" in
  setup)
    IP="$2"
    [[ "$IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Put your droplet's real IPv4 address, like:  ./jamesos-host.sh setup 128.199.12.34"; exit 1; }
    echo "$IP" > "$HOSTFILE"; H="$IP"
    echo "== Connecting to $IP (type yes if it asks) =="
    rs true || { echo "Can't log in to root@$IP. Is the droplet 'Running'? Did you add your SSH key to it?"; exit 1; }
    echo "== Setting up the server (about 1-2 minutes) =="
    rs 'bash -s' <<'EOF' || { echo "Setup failed - send Claude a screenshot"; exit 1; }
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq nginx ufw rsync >/dev/null
mkdir -p /var/www/jamesos
cat > /var/www/jamesos/index.html <<'HTML'
<!doctype html><meta charset="utf-8"><title>JamesOS</title><p>JamesOS update files.</p>
HTML
cat > /etc/nginx/sites-available/jamesos <<'CONF'
# JamesOS OTA files: plain http, no redirects (old Vector recovery can't follow https or redirects)
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    server_name _;
    root /var/www/jamesos;
    autoindex off;
    index index.html;
    location / { try_files $uri $uri/ =404; }
    location ~ \.part$ { return 404; }
    types { application/octet-stream ota; text/html html; }
    sendfile on;
    tcp_nopush on;
}
CONF
rm -f /etc/nginx/sites-enabled/default
ln -sf /etc/nginx/sites-available/jamesos /etc/nginx/sites-enabled/jamesos
nginx -t -q
systemctl enable -q nginx
systemctl restart nginx
ufw allow OpenSSH >/dev/null
ufw allow 80/tcp >/dev/null
ufw --force enable >/dev/null
echo "server ready"
EOF
    echo
    echo "Server is ready at http://$IP/"
    echo "Next: ./jamesos-host.sh upload   (or: ./jamesos-host.sh site)"
    ;;
  upload)
    upload
    ;;
  site)
    upload
    echo
    echo "== Making the online JamesOS Setup use this link =="
    ./jamesos.sh setup-site online "$(cat jamesos/ota-link.txt)"
    ;;
  list)
    need_host
    rs 'ls -lh /var/www/jamesos/*.ota 2>/dev/null || echo "(no OTA files yet)"'
    ;;
  test)
    url=$(cat jamesos/ota-link.txt 2>/dev/null)
    [[ -n "$url" ]] || { echo "Nothing uploaded yet. Run: ./jamesos-host.sh upload"; exit 1; }
    echo "== Checking $url =="
    code=$(curl -s -o /dev/null -w '%{http_code} %{size_download}' -r 0-1023 "$url")
    case "$code" in
      206*|200*) echo "OK - the link works with plain http and no redirect (what Vector needs)." ;;
      *) echo "Problem: the server answered '$code'. Send Claude a screenshot." ;;
    esac
    ;;
  release)
    upload
    VER=$(basename "$(newest_ota)" .ota); VER=${VER#vicos-}
    echo
    echo "== Releasing $VER to every older JamesOS robot =="
    rs "bash -s '$VER'" <<<"$LINK_OLDER" || { echo "Release failed - send Claude a screenshot"; exit 1; }
    echo
    echo "Done. Robots on the charger get $VER within about 1-2 hours, then restart at night (1-5 AM)."
    echo "Something wrong? ./jamesos-host.sh pause   stops it right away."
    ;;
  pause)
    need_host
    rs 'rm -f /var/www/jamesos/vic/diff/*.ota; mkdir -p /var/www/jamesos/vic; touch /var/www/jamesos/vic/paused' \
      && echo "Paused: no robot gets an update now. Robots that already downloaded one may still restart into it." \
      && echo "Start again with: ./jamesos-host.sh resume   (or release a fixed version)"
    ;;
  resume)
    need_host
    VER=$(rs 'cat /var/www/jamesos/vic/latest.txt 2>/dev/null')
    [[ -n "$VER" ]] || { echo "Nothing was released yet. Use: ./jamesos-host.sh release"; exit 1; }
    rs "bash -s '$VER'" <<<"$LINK_OLDER" && echo "Robots get $VER again."
    ;;
  status)
    need_host
    rs 'V=/var/www/jamesos/vic
echo "Released now: $(cat $V/latest.txt 2>/dev/null || echo none)$([ -f $V/paused ] && echo "  (PAUSED)")"
echo "Older versions that get it: $(ls $V/diff 2>/dev/null | wc -l) (every JamesOS build before it)"
echo
echo "Robots that asked for updates (last 2 days, by version):"
find /var/log/nginx -name "access.log*" -mtime -2 2>/dev/null | xargs -r zgrep -h "GET /vic/" 2>/dev/null \
  | sed -n "s#.*GET /vic/[a-z]*/\([^ ?]*\)\.ota?[^ ]*emresn=\([0-9a-fA-F]*\)[^ ]* HTTP[^ ]* \([0-9][0-9][0-9]\) .*#\1 \2 \3#p" \
  | sort | uniq -c | awk "{print \"  \" \$2 \"  robot \" substr(\$3,1,4) \"...  answer \" \$4 \"  (\" \$1 \" times)\"}" | tail -30
echo "(answer 200 = got the update, 404 = already up to date)"'
    ;;
  *)
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    ;;
esac
