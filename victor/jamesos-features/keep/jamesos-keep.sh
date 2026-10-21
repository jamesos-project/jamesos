#!/bin/sh
# JamesOS keep (runs ON the robot, as root, from the JamesOS build itself).
#
#   jamesos-keep.sh boot   at every start, before Vector's programs:
#                          - after CLEAR OUT SOUL (empty /data): put JamesOS back -
#                            backpack lights, sparkle settings, galaxy eyes, the updater,
#                            and his badges / pet stats from the saved copy
#                          - then save a fresh copy of the badges
#   jamesos-keep.sh save   at every shutdown: save badges / pet stats to /persist,
#                          which CLEAR OUT SOUL does not erase
#   jamesos-keep.sh restore put the saved badges / pet stats back now and add the JamesOS defaults
#                          that are missing (for a robot that was reset before this worked)
#   jamesos-keep.sh forget delete the saved badges (a real fresh start, e.g. before giving him away)
#   jamesos-keep.sh unmark remove the saved prototype mark
#
# CLEAR OUT SOUL erases the whole /data drive. This file lives in /anki (the JamesOS
# programs), and the saved badges live in /persist - neither is erased.

J=/data/data/jamesos
DEF=$(cd "$(dirname "$0")" && pwd)   # /anki/data/assets/cozmo_resources/config/jamesos
P=/persist/jamesos
KEEP="badges.txt stats.txt pets.txt prototype"
U=/data/jamesos-update

# /persist may be read-only (or a folder on the read-only system drive): unlock it for a moment
persist_rw() {
  MP=$(df /persist 2>/dev/null | tail -1 | awk '{print $NF}')
  [ -z "$MP" ] && MP=/
  WASRO=0
  if awk -v m="$MP" '$2==m && $4 ~ /^ro/ {f=1} END {exit !f}' /proc/mounts; then
    WASRO=1
    mount -o remount,rw "$MP" || return 1
  fi
  return 0
}
persist_ro() { sync; [ "$WASRO" = 1 ] && mount -o remount,ro "$MP"; true; }

# put the saved badges back without losing new ones: badges are merged (one line per badge),
# pet stats come back if the saved ones have more pets
bring_back() {
  if [ -s "$P/badges.txt" ]; then
    cat "$P/badges.txt" "$J/badges.txt" 2>/dev/null | awk 'NF && !seen[$1]++' > "$J/badges.txt.new" \
      && mv "$J/badges.txt.new" "$J/badges.txt"
    chmod 666 "$J/badges.txt"
  fi
  [ -s "$P/prototype" ] && cp "$P/prototype" "$J/prototype" && chmod 666 "$J/prototype"
  saved=$(sed -n 's/^total=//p' "$P/stats.txt" 2>/dev/null); now=$(sed -n 's/^total=//p' "$J/stats.txt" 2>/dev/null)
  if [ -s "$P/stats.txt" ] && [ "${saved:-0}" -ge "${now:-0}" ] 2>/dev/null; then
    cp "$P/stats.txt" "$J/stats.txt"; chmod 666 "$J/stats.txt"
    [ -s "$P/pets.txt" ] && cp "$P/pets.txt" "$J/pets.txt" && chmod 666 "$J/pets.txt"
  fi
}

save() {
  awk '$2=="/data" {f=1} END {exit !f}' /proc/mounts || return 0
  have=0
  for f in $KEEP; do [ -s "$J/$f" ] && have=1; done
  [ "$have" = 1 ] || return 0          # nothing to save (never overwrite the copy with nothing)
  # nothing changed since the last save: don't touch /persist
  same=1
  for f in $KEEP; do
    if [ -s "$J/$f" ] && ! cmp -s "$J/$f" "$P/$f"; then same=0; fi
  done
  [ "$same" = 1 ] && return 0
  persist_rw || return 0
  mkdir -p "$P"
  for f in $KEEP; do
    [ -s "$J/$f" ] && cp "$J/$f" "$P/$f.tmp" && mv "$P/$f.tmp" "$P/$f"
  done
  persist_ro
}

boot() {
  # /data must really be there (WireOS mounts it in mount-data.service)
  if ! awk '$2=="/data" {f=1} END {exit !f}' /proc/mounts; then
    echo "JamesOS keep: /data is not mounted - doing nothing"
    return 0
  fi
  # brand-new /data: WireOS leaves /tmp/data_cleared after CLEAR OUT SOUL; or the folder is empty
  fresh=0
  if [ -e /tmp/data_cleared ] && [ ! -e "$J/.jamesos" ]; then
    fresh=1
  elif [ ! -e "$J/.jamesos" ] && [ -z "$(ls -A "$J" 2>/dev/null)" ] && [ ! -e /data/data/customBackpackLights ]; then
    fresh=1
  fi
  mkdir -p "$J"
  chmod 777 "$J"

  if [ "$fresh" = 1 ]; then
    echo "JamesOS: fresh start - putting JamesOS settings back"
    # badges and pet stats from before
    bring_back
    # default lights + sparkles + galaxy eyes (built into this JamesOS version)
    if [ -f "$DEF/defaults.tar" ]; then
      tar -C /data/data -xf "$DEF/defaults.tar"
      chmod 777 "$J"
      chmod 666 "$J"/*.conf "$J"/hearts.on "$J"/petfx 2>/dev/null
    fi
  fi
  touch "$J/.jamesos"
  chmod 666 "$J/.jamesos"

  # the updater lives in /data too: put it back if it's gone
  if [ ! -x "$U/bin/jamesos-update" ] && [ -f "$DEF/jamesos-update" ]; then
    mkdir -p "$U/bin"
    cp "$DEF/jamesos-update" "$U/bin/jamesos-update.tmp" && chmod 755 "$U/bin/jamesos-update.tmp" \
      && mv "$U/bin/jamesos-update.tmp" "$U/bin/jamesos-update"
    [ -f "$DEF/jamesos-update.service" ] && cp "$DEF/jamesos-update.service" "$U/bin/jamesos-update.service"
  fi

  save
  sync
}

restore() {
  mkdir -p "$J"; chmod 777 "$J"
  bring_back
  if [ -f "$DEF/defaults.tar" ]; then
    # only what is missing (keeps your own settings)
    T=/data/data/.jamesos-defaults.$$
    rm -rf "$T"; mkdir -p "$T"
    tar -C "$T" -xf "$DEF/defaults.tar"
    (cd "$T" && find . -type d) | while read -r d; do mkdir -p "/data/data/$d"; done
    (cd "$T" && find . -type f) | while read -r f; do
      [ -e "/data/data/$f" ] || cp -p "$T/$f" "/data/data/$f"
    done
    rm -rf "$T"
    chmod 777 "$J"
  fi
  touch "$J/.jamesos"
  sync
  echo "Badges, pet stats and missing JamesOS settings are back."
}

forget() {
  persist_rw && rm -rf "$P" && persist_ro
  rm -f "$J/badges.txt" "$J/stats.txt" "$J/pets.txt"
  sync
  echo "Badges and pet stats deleted (also the saved copy)."
}

case "$1" in
  boot)   boot ;;
  save)   save ;;
  restore) restore ;;
  forget) forget ;;
  unmark) rm -f "$J/prototype"; persist_rw && rm -f "$P/prototype" && persist_ro; sync ;;
  *)      sed -n '2,14p' "$0" ;;
esac
exit 0
