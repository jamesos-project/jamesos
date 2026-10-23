#!/bin/bash
# Put James's OWN startup picture back, inside the NEW startup program (the 119d one).
# The program itself changed between versions, so we can't copy the old one. Instead we take only
# the 184x96 picture out of the old program and swap it into the new one (nothing else moves).
#
#   bash james-fix-splash.sh check    find his old picture, change nothing
#   bash james-fix-splash.sh fix      install it on James
#
set -e
cd "$(dirname "$0")"
IP=192.168.0.110
KEY="$HOME/.ssh/vector_key"
SSH="ssh -i $KEY -o LogLevel=ERROR -o ConnectTimeout=8 root@$IP"
D="$HOME/james-backup/oldslot"
mkdir -p "$D"
MODE="${1:-check}"
SPL="$PWD/jamesos-features/splash"

SLOT=$($SSH 'grep -o "slot_suffix=_[ab]" /proc/cmdline | tail -1')
case "$SLOT" in *_a) OLD=/dev/mmcblk0p25 ;; *_b) OLD=/dev/mmcblk0p22 ;; *) echo "Can't tell his slot. Nothing was done."; exit 1 ;; esac
$SSH "mkdir -p /tmp/oldb; grep -q ' /tmp/oldb ' /proc/mounts || mount -o ro $OLD /tmp/oldb"
scp -q -i "$KEY" root@$IP:/tmp/oldb/usr/bin/rampost "$D/old_rampost"
scp -q -i "$KEY" root@$IP:/tmp/oldb/usr/bin/rampost.wireos-backup "$D/old_rampost_wireos" 2>/dev/null || rm -f "$D/old_rampost_wireos"
scp -q -i "$KEY" root@$IP:/usr/bin/rampost "$D/new_rampost"
$SSH "umount /tmp/oldb 2>/dev/null; true"

python3 - "$D" "$SPL" <<'PY'
import sys, os, glob
D, SPL = sys.argv[1], sys.argv[2]
N = 35328
old = open(os.path.join(D, "old_rampost"), "rb").read()
new = open(os.path.join(D, "new_rampost"), "rb").read()
pics = {}
for f in sorted(glob.glob(os.path.join(SPL, "*.raw"))):
    b = open(f, "rb").read()
    if len(b) == N:
        pics[os.path.basename(f)[:-4]] = b

def find_known(data):
    hits = [(name, data.find(b)) for name, b in pics.items() if data.find(b) >= 0]
    return hits

# 1) where is the picture in the NEW program?
nh = find_known(new)
if len(nh) != 1:
    sys.exit("New program: found %s known pictures (%s) - not sure where the picture is. Nothing changed."
             % (len(nh), ", ".join(n for n, _ in nh) or "none"))
new_name, new_pos = nh[0]
print("New program shows: %s (at byte %d)" % (new_name, new_pos))

# 2) his OLD picture
oh = find_known(old)
if len(oh) == 1:
    old_name, old_pos = oh[0]
    pic = old[old_pos:old_pos + N]
    print("His old picture: %s" % old_name)
else:
    wb = os.path.join(D, "old_rampost_wireos")
    if not os.path.exists(wb) or "wireos_splash" not in pics:
        sys.exit("Couldn't find his old picture (no match and no old WireOS copy). Nothing changed.")
    w = open(wb, "rb").read()
    p = w.find(pics["wireos_splash"])
    if p < 0 or len(w) != len(old):
        sys.exit("Couldn't line up the old program with its WireOS copy. Nothing changed.")
    pic = old[p:p + N]
    old_name = "custom picture (not one of the files on this PC)"
    print("His old picture: " + old_name)
    open(os.path.join(D, "james_old_splash.raw"), "wb").write(pic)

if new[new_pos:new_pos + N] == pic:
    print("SAME - the new program already shows his picture.")
    open(os.path.join(D, "verdict"), "w").write("same")
    sys.exit(0)
out = new[:new_pos] + pic + new[new_pos + N:]
assert len(out) == len(new)
open(os.path.join(D, "rampost.james"), "wb").write(out)
open(os.path.join(D, "verdict"), "w").write("fix")
print("READY - made a new startup program with his picture (only the picture changed).")
PY

[[ "$(cat "$D/verdict" 2>/dev/null)" == fix ]] || exit 0
if [[ "$MODE" != fix ]]; then echo "Looked only - nothing changed on James. Run:  bash james-fix-splash.sh fix"; exit 0; fi

SUM=$(md5sum < "$D/rampost.james" | cut -d' ' -f1)
$SSH "cat > /data/rampost.new" < "$D/rampost.james"
$SSH "set -e
[ \"\$(md5sum < /data/rampost.new | cut -d' ' -f1)\" = $SUM ] || { echo 'Upload was broken, nothing changed'; rm -f /data/rampost.new; exit 1; }
mount -o remount,rw /
[ -f /usr/bin/rampost.ota-backup ] || cp -p /usr/bin/rampost /usr/bin/rampost.ota-backup
cp /data/rampost.new /usr/bin/rampost.tmp && chmod 755 /usr/bin/rampost.tmp && mv /usr/bin/rampost.tmp /usr/bin/rampost
rm -f /data/rampost.new
sync
mount -o remount,ro /
echo 'Installed his startup picture.'"
echo "Reboot him to see it:  ssh -i ~/.ssh/vector_key root@$IP reboot"
echo "(Undo: the 119d one is saved on him as /usr/bin/rampost.ota-backup)"
