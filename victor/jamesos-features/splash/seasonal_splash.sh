# JamesOS seasonal startup picture. Runs from /usr/bin/rampost-init.sh just before rampost.
# Asks vic-bootAnim which picture fits today. If it's not the one inside rampost yet,
# swaps it the same way "./jamesos.sh splash" does (copy, then replace). Only writes when
# the season or holiday changes. Any problem: leaves rampost exactly as it was.
# Installed by:  ./jamesos.sh splash seasonal      Removed by: ./jamesos.sh splash <other>|off
JS_DIR=/etc/jamesos
JS_RP=/usr/bin/rampost
JS_OFF=$(cat "$JS_DIR/splash_offset" 2>/dev/null)
JS_PIC=$(/anki/bin/vic-bootAnim --splash 2>/dev/null)
JS_CUR=$(cat "$JS_DIR/splash_current" 2>/dev/null)
if [ -n "$JS_OFF" ] && [ -n "$JS_PIC" ] && [ -f "$JS_PIC" ] && [ "$JS_PIC" != "$JS_CUR" ] && \
   [ "$(wc -c < "$JS_PIC")" -eq 35328 ]; then
  JS_W=0
  if awk '$2=="/" && $4 ~ /^ro/ {f=1} END {exit !f}' /proc/mounts; then
    mount -o remount,rw / && JS_W=1
  fi
  if cp -p "$JS_RP" "$JS_RP.tmp" && \
     dd if="$JS_PIC" of="$JS_RP.tmp" bs=1 seek="$JS_OFF" conv=notrunc 2>/dev/null && \
     [ "$(wc -c < "$JS_RP.tmp")" -eq "$(wc -c < "$JS_RP")" ]; then
    chmod 755 "$JS_RP.tmp" && mv "$JS_RP.tmp" "$JS_RP" && echo "$JS_PIC" > "$JS_DIR/splash_current"
    echo "JamesOS: startup picture is now $JS_PIC"
  else
    rm -f "$JS_RP.tmp"
    echo "JamesOS: couldn't change the startup picture, kept the old one"
  fi
  sync
  [ "$JS_W" = 1 ] && mount -o remount,ro /
fi
true
