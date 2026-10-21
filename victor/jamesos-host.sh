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
# The server IP is kept in jamesos/ota-host.txt (not in git).

cd "$(dirname "$0")" || exit 1
HOSTFILE=jamesos/ota-host.txt
WOS="${WIREOS_DIR:-$HOME/wire-os}"
mkdir -p jamesos

host() {
  local h; h=$(cat "$HOSTFILE" 2>/dev/null)
  [[ -n "$h" ]] || { echo "No server yet. Run first:  ./jamesos-host.sh setup YOUR_SERVER_IP"; exit 1; }
  echo "$h"
}
rs() { ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 "root@$(host)" "$@"; }

newest_ota() {
  local o; o=$(ls -t "$WOS"/_build/vicos-*.ota 2>/dev/null | head -1)
  [[ -f "$o" ]] || { echo "No OTA file yet. Build one first: ./jamesos.sh ota" >&2; exit 1; }
  echo "$o"
}

upload() {
  local ota name url
  ota=$(newest_ota) || exit 1
  name="jamesos-$(basename "$ota" .ota | sed 's/^vicos-//').ota"
  echo "== Uploading $(basename "$ota") as $name ($(du -h "$ota" | cut -f1)) =="
  rsync -h --progress --partial "$ota" "root@$(host):/var/www/jamesos/$name.part" \
    || { echo "Upload failed - check your internet and run it again (it continues where it stopped)."; exit 1; }
  rs "mv -f /var/www/jamesos/$name.part /var/www/jamesos/$name && chmod 644 /var/www/jamesos/$name" || exit 1
  url="http://$(host)/$name"
  echo "$url" > jamesos/ota-link.txt
  echo
  echo "Done. Your OTA link:  $url"
}

case "$1" in
  setup)
    IP="$2"
    [[ "$IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Usage: ./jamesos-host.sh setup YOUR_SERVER_IP   (the IPv4 from DigitalOcean)"; exit 1; }
    echo "$IP" > "$HOSTFILE"
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
  *)
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    ;;
esac
