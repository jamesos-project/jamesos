#!/bin/bash
# Bring back James's own startup picture + boot animation after the 119d update.
# They were on his OLD system slot (the update wrote a fresh slot). This reads them from there.
#
#   bash james-restore-boot.sh check    look only, changes nothing
#   bash james-restore-boot.sh fix      copy his old ones onto the new system (only if check is OK)
#
set -e
IP=192.168.0.110
KEY="$HOME/.ssh/vector_key"
SSH="ssh -i $KEY -o LogLevel=ERROR -o ConnectTimeout=8 root@$IP"
D="$HOME/james-backup/oldslot"
mkdir -p "$D"; cd "$D"
MODE="${1:-check}"

ROOT=$($SSH 'mount | awk "\$3==\"/\" {print \$1}" | tail -1')
if [[ "$ROOT" != *mmcblk0p2[25] ]]; then
  # /dev/root is a nickname: ask which slot he booted from (_a = p22, _b = p25)
  SLOT=$($SSH 'grep -o "slot_suffix=_[ab]" /proc/cmdline | tail -1')
  case "$SLOT" in *_a) ROOT=/dev/mmcblk0p22 ;; *_b) ROOT=/dev/mmcblk0p25 ;; esac
fi
case "$ROOT" in
  *p22) OLD=/dev/mmcblk0p25 ;;
  *p25) OLD=/dev/mmcblk0p22 ;;
  *) echo "Running system is '$ROOT' - not what I expected. Nothing was done."; exit 1 ;;
esac
echo "Running system: $ROOT    old system: $OLD"

# open the old slot read-only, and the new system drive (read-only bind) to see the hidden boot animation
$SSH "mkdir -p /tmp/oldb /tmp/jr
grep -q ' /tmp/oldb ' /proc/mounts || mount -o ro $OLD /tmp/oldb
grep -q ' /tmp/jr ' /proc/mounts || mount --bind / /tmp/jr
echo -n 'Old system version: '; cat /tmp/oldb/anki/etc/version 2>/dev/null || echo '?'"

echo "== Copying both versions to $D =="
scp -q -i "$KEY" root@$IP:/tmp/oldb/usr/bin/rampost old_rampost
scp -q -i "$KEY" root@$IP:/usr/bin/rampost new_rampost
scp -q -i "$KEY" root@$IP:/tmp/oldb/persist/boot_anim.raw old_hidden_anim.raw 2>/dev/null || rm -f old_hidden_anim.raw
scp -q -i "$KEY" root@$IP:/tmp/jr/persist/boot_anim.raw new_hidden_anim.raw 2>/dev/null || rm -f new_hidden_anim.raw
scp -q -i "$KEY" root@$IP:/persist/boot_anim.raw real_anim.raw 2>/dev/null || rm -f real_anim.raw

python3 - <<'PY' > verdict.txt
a = open("old_rampost", "rb").read(); b = open("new_rampost", "rb").read()
if len(a) != len(b):
    print("RAMPOST DIFFERENT_PROGRAM sizes %d vs %d" % (len(a), len(b)))
else:
    diff = [i for i in range(len(a)) if a[i] != b[i]]
    if not diff:
        print("RAMPOST SAME")
    elif diff[-1] - diff[0] < 35328:
        print("RAMPOST PICTURE_ONLY (%d bytes differ, all inside the picture)" % len(diff))
    else:
        print("RAMPOST DIFFERENT_PROGRAM (%d bytes differ outside the picture)" % len(diff))
import hashlib, os
def h(p): return hashlib.md5(open(p, "rb").read()).hexdigest()[:10] if os.path.exists(p) else "missing"
o, n, r = h("old_hidden_anim.raw"), h("new_hidden_anim.raw"), h("real_anim.raw")
print("ANIM old=%s new=%s real=%s" % (o, n, r))
print("ANIM " + ("SAME" if o == n else ("NO_OLD" if o == "missing" else "DIFFERENT")))
PY
cat verdict.txt

if [[ "$MODE" != fix ]]; then
  $SSH "umount /tmp/jr 2>/dev/null; umount /tmp/oldb 2>/dev/null; true"
  echo "Looked only - nothing changed. Send this output to Claude."
  exit 0
fi

DO_PIC=0; DO_ANIM=0
grep -q "RAMPOST PICTURE_ONLY" verdict.txt && DO_PIC=1
grep -q "ANIM DIFFERENT" verdict.txt && DO_ANIM=1
if grep -q "RAMPOST DIFFERENT_PROGRAM" verdict.txt; then
  echo "The startup program changed between versions - NOT copying it (would be unsafe). Tell Claude."
fi
if [[ $DO_PIC == 0 && $DO_ANIM == 0 ]]; then
  $SSH "umount /tmp/jr 2>/dev/null; umount /tmp/oldb 2>/dev/null; true"
  echo "Nothing to fix."; exit 0
fi
echo "== Putting his old ones back (startup picture: $DO_PIC, boot animation: $DO_ANIM) =="
$SSH "set -e
umount /tmp/jr 2>/dev/null || true
mount -o remount,rw /
if [ $DO_PIC = 1 ]; then
  [ -f /usr/bin/rampost.ota-backup ] || cp -p /usr/bin/rampost /usr/bin/rampost.ota-backup
  cp /tmp/oldb/usr/bin/rampost /usr/bin/rampost.tmp && chmod 755 /usr/bin/rampost.tmp && mv /usr/bin/rampost.tmp /usr/bin/rampost
  [ -f /tmp/oldb/usr/bin/rampost.wireos-backup ] && cp -p /tmp/oldb/usr/bin/rampost.wireos-backup /usr/bin/rampost.wireos-backup
fi
if [ $DO_ANIM = 1 ]; then
  mount --bind / /tmp/jr
  mkdir -p /tmp/jr/persist
  cp /tmp/oldb/persist/boot_anim.raw /tmp/jr/persist/boot_anim.raw
  sync; umount /tmp/jr
fi
sync
mount -o remount,ro /
umount /tmp/oldb
md5sum /usr/bin/rampost"
echo "Done. Reboot him to see it:  ssh -i ~/.ssh/vector_key root@$IP reboot"
