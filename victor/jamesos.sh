#!/usr/bin/env bash
# JamesOS helper - one command for building and updating your Vector.
#
#   ./jamesos.sh find          find all your Vectors on the Wi-Fi (by serial number) and fix their IPs
#   ./jamesos.sh use 1.0       switch which Vector you work on, by name (or: use 192.168.0.116)
#   ./jamesos.sh which         show which Vector you're working on
#   ./jamesos.sh install-all   put ALL of JamesOS on this Vector (deploy, lights, boot animation,
#                              welcome message, starlight + galaxy settings), then reboot. No build.
#   ./jamesos.sh go            build (only changed parts) + deploy + done
#   ./jamesos.sh deploy        deploy only (no build)
#   ./jamesos.sh push FILE     copy ONE config/asset file to Vector, no build at all
#                              FILE must be inside resources/config/...
#                              e.g. ./jamesos.sh push resources/config/engine/jamesos_eye_colors.json
#   ./jamesos.sh restart       restart Vector's programs (needed after pushing most files)
#   ./jamesos.sh poweroff [IP] turn Vector off, like holding his back button (needs JamesOS v16 on him)
#   ./jamesos.sh voice-poweroff-setup   add "Hey Vector, power off" to wire-pod (then restart wire-pod)
#   ./jamesos.sh voice-poweroff-remove  take that voice command out of wire-pod again
#   ./jamesos.sh ssh           open a shell on Vector
#   ./jamesos.sh lights        send the JamesOS backpack light theme (jamesos/backpackLights) + restart
#   ./jamesos.sh lights off    go back to normal WireOS backpack lights + restart
#   ./jamesos.sh starlight on  send jamesos/starlight.conf (sparkles on, or new settings) - no restart
#   ./jamesos.sh starlight off turn the sparkles off - no restart
#   ./jamesos.sh style white|rainbow|shooting   sparkle style - no restart
#   ./jamesos.sh petfx galaxy|off   galaxy eyes while you pet him - no restart
#   ./jamesos.sh mood on|off    mood colors - no restart
#   (all of these can also be switched on Vector: info screen -> EXTRAS)
#   ./jamesos.sh bootanim      send the JamesOS "Wake Up" boot animation (picks 1.0 or 2.0 size by itself)
#   ./jamesos.sh bootanim galaxy send the "Galaxy Ignite" boot animation instead
#   ./jamesos.sh bootanim warp send the older "Warp" boot animation instead
#   ./jamesos.sh bootanim wakeup|galaxy|warp 1.0|2.0   same, but you say which model it is
#   ./jamesos.sh bootanim off  go back to the normal boot animation
#   ./jamesos.sh splash sparkle|sleeping|badge   the first picture at startup (instead of "WireOS")
#   ./jamesos.sh splash off    put the WireOS startup picture back
#   ./jamesos.sh splash seasonal   startup picture changes with the seasons and holidays
#   ./jamesos.sh theme auto|normal|halloween|christmas|lunar|newyear|valentine|stpatrick|aprilfools|easter|mothersday|fireworks|thanksgiving
#                                             eye theme (auto = AUTO HOLIDAYS, picks by date; lunar = Chinese New Year)
#   ./jamesos.sh holidays all|western|off   which holidays auto + the countdown banner use (all = with Chinese New Year)
#   ./jamesos.sh sleepscreen stars|clock|both|off   what he shows while sleeping on the charger
#   ./jamesos.sh pets          how many times he was petted today
#   ./jamesos.sh sleepy on|off sleepy eyes when his battery is low - no restart
#   ./jamesos.sh funeyes auto|on|off holiday eye shapes (pumpkin, hearts, flag, year): auto = on the day itself,
#                              on = the whole holiday - no restart
#   ./jamesos.sh shiny test|off   make him shiny for today to see it (test: no badge)
#   ./jamesos.sh bluemoon test|off pretend it's a blue moon night (test: no badge)
#   ./jamesos.sh private on|off   James's own extras in the build (Vector family, prototype)
#   ./jamesos.sh prototype on|off mark this robot as a prototype (private extra)
#   ./jamesos.sh family [name ROBOT NAME him|her|them | miss ROBOT on|off]   family names (private)
#   ./jamesos.sh badges        which badges (achievements) he has unlocked
#   ./jamesos.sh badges reset  delete all badges + pet stats (they survive CLEAR OUT SOUL otherwise)
#   ./jamesos.sh badges restore  put saved badges + missing JamesOS settings back (after an old-style reset)
#   ./jamesos.sh web           JamesOS web control page: open http://localhost:8765 (also starts with autoupdate)
#   ./jamesos.sh web stop      stop the web control page
#
# OTA (a full system file a Vector installs by itself - for new owners, no PC needed):
#   ./jamesos.sh ota           build a JamesOS OTA with the WireOS builder in ~/wire-os (about 20-60 min)
#                              uses the JamesOS code saved in git (commit first). Private extras are always OFF.
#   ./jamesos.sh ota-install 2.0   install the newest OTA on a robot (by name or IP) - never on the PVT
#   ./jamesos.sh setup-site    JamesOS Setup website on this laptop (like websetup.skittle.dev, but it
#                              installs your newest JamesOS OTA). Open http://localhost:8000 in Chrome.
#   ./jamesos.sh setup-site online LINK   put JamesOS Setup online (GitHub Pages, repo in ~/jamesos-site),
#                              installing the OTA at LINK (a GitHub release file). No laptop needed to use it.
#
# Auto-update (your PC sends new JamesOS versions to your robots by itself):
#   ./jamesos.sh publish       make the build you last sent with "go" the new JamesOS version
#                              (test it on one robot first!) - every robot gets it automatically
#   ./jamesos.sh autoupdate    keep checking your robots every minute and update them (leave the window open)
#   ./jamesos.sh autoupdate startup      start the auto-updater by itself every time Windows starts
#   ./jamesos.sh autoupdate startup off  stop doing that
#   ./jamesos.sh robots        list your robots and which JamesOS version each one has
#   ./jamesos.sh robots add IP [name]   /   robots remove IP
#   ./jamesos.sh update-now [IP]   update a robot right away (even off the charger, even if it failed before)
#   ./jamesos.sh rollback [IP]     put back the version from before the last update
#   ./jamesos.sh updater       (re)install the small updater program on the current robot
#
# Put this file in the root of wire-os-victor (next to robot_ip.txt).

set -e
cd "$(dirname "$0")"

# the JamesOS version name (shown on Vector's info screen as "OS: JamesOS v21")
JAMESOS_RELEASE="v24"

if [[ "$1" == "use" ]]; then
  # ./jamesos.sh use 1.0   (a name from ./jamesos.sh find)   or   ./jamesos.sh use 192.168.0.116
  if [[ -n "$2" && ! "$2" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    found=$(awk -v n="$2" '$2==n {print $1; exit}' jamesos/robots.txt 2>/dev/null)
    [[ -n "$found" ]] || { echo "No robot called '$2' in jamesos/robots.txt. Run: ./jamesos.sh find"; exit 1; }
    echo "$found" > robot_ip.txt
    esn=$(awk -v n="$2" '$2==n {print $1; exit}' jamesos/robot-names.txt 2>/dev/null)
    [[ -n "$esn" ]] && echo "$esn" > jamesos/current-esn
    echo "Now working on $2 at $found"
    exit 0
  fi
  [[ "$2" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Usage: ./jamesos.sh use 1.0   or   ./jamesos.sh use 192.168.0.116"; exit 1; }
  echo "$2" > robot_ip.txt
  mkdir -p jamesos
  touch jamesos/robots.txt
  grep -q "^$2\( \|$\)" jamesos/robots.txt || echo "$2" >> jamesos/robots.txt
  echo "Now working on the Vector at $2"
  exit 0
fi

IP="$(tr -d ' \r\n' < robot_ip.txt)"
KEY="$HOME/.ssh/vector_key"
if [[ ! -f "$KEY" ]]; then
  mkdir -p "$HOME/.ssh"
  cp robot_sshkey "$KEY"
fi
chmod 600 "$KEY"
SSH="ssh -i $KEY -o StrictHostKeyChecking=no -o LogLevel=ERROR root@$IP"

start=$(date +%s)
done_msg() { echo; echo "== Done in $(( $(date +%s) - start ))s =="; }

# ---------------- auto-update helpers ----------------
UPD=jamesos/updates                      # published version lives here (on your PC)
ROBOTS=jamesos/robots.txt                # one robot per line: IP [name]
RU=/data/jamesos-update                  # updater folder on the robot
RB=$RU/bin/jamesos-update                # updater program on the robot
USRC=jamesos-features/updater
KSRC=jamesos-features/keep                # JamesOS keep: survives CLEAR OUT SOUL (v21)

rssh() { local ip=$1; shift; ssh -i "$KEY" -o StrictHostKeyChecking=no -o LogLevel=ERROR -o ConnectTimeout=6 -o BatchMode=yes -o ServerAliveInterval=10 -o ServerAliveCountMax=6 "root@$ip" "$@"; }

robot_list() {
  mkdir -p jamesos
  [[ -s $ROBOTS ]] || echo "$IP" > "$ROBOTS"
  grep -v '^[[:space:]]*\(#\|$\)' "$ROBOTS" | tr -d '\r'
}

# put (or refresh) the small updater program + its startup safety check on a robot
setup_updater() {
  local ip=$1
  [[ -f $USRC/jamesos-update && -f $USRC/jamesos-update.service ]] || { echo "$USRC is missing (extract jamesos-features-v10 first)"; return 1; }
  local want have
  want=$( (md5sum < "$USRC/jamesos-update"; md5sum < "$USRC/jamesos-update.service"; md5sum < "$KSRC/jamesos-keep.service") | cut -d' ' -f1 | tr '\n' ' ')
  have=$(rssh "$ip" "(md5sum < $RB; md5sum < /etc/systemd/system/jamesos-update.service; md5sum < /etc/systemd/system/jamesos-keep.service) 2>/dev/null | cut -d' ' -f1 | tr '\n' ' '") || return 1
  [[ "$want" == "$have" ]] && return 0
  rssh "$ip" "mkdir -p $RU/bin && cat > $RB.tmp && chmod 755 $RB.tmp && mv $RB.tmp $RB" < "$USRC/jamesos-update" || return 1
  rssh "$ip" "cat > $RU/bin/jamesos-update.service" < "$USRC/jamesos-update.service" || return 1
  rssh "$ip" "cat > $RU/bin/jamesos-keep.service" < "$KSRC/jamesos-keep.service" || return 1
  rssh "$ip" "sh -s" <<'UNIT' || return 1
W=0
awk '$2=="/" && $4 ~ /^ro/ {f=1} END {exit !f}' /proc/mounts && W=1 && mount -o remount,rw /
cp /data/jamesos-update/bin/jamesos-update.service /etc/systemd/system/jamesos-update.service
mkdir -p /etc/systemd/system/multi-user.target.wants
ln -sf ../jamesos-update.service /etc/systemd/system/multi-user.target.wants/jamesos-update.service
# JamesOS keep (v21): lives on the system drive, so CLEAR OUT SOUL can't remove it
cp /data/jamesos-update/bin/jamesos-keep.service /etc/systemd/system/jamesos-keep.service
ln -sf ../jamesos-keep.service /etc/systemd/system/multi-user.target.wants/jamesos-keep.service
sync
[ "$W" = 1 ] && mount -o remount,ro /
systemctl daemon-reload
# start it now too (does nothing on a robot that is already set up, except save a copy of the badges)
systemctl start jamesos-keep.service 2>/dev/null
true
UNIT
}

# write the version into the JamesOS programs on the robot: /anki/etc/jamesos-version
# (line 1 = version id, line 2 = name). /anki survives CLEAR OUT SOUL, so the version can't get lost.
stamp_version() {
  rssh "$1" sh -s -- "$2" "$JAMESOS_RELEASE" <<'STAMP'
W=0
awk '$2=="/" && $4 ~ /^ro/ {f=1} END {exit !f}' /proc/mounts && W=1 && mount -o remount,rw /
printf '%s\n%s\n' "$1" "$2" > /anki/etc/jamesos-version
sync
[ "$W" = 1 ] && mount -o remount,ro /
true
STAMP
}

# v24: the SSH welcome message says "JamesOS <version> (based on WireOS ...)". It reads the version
# from /anki/etc/jamesos-version each time, so it is always right (it said "v1.0" before).
motd_line() {
  printf '%s\n' 'BASE="$NAME"; NAME="JamesOS"; JV=$(sed -n 2p /anki/etc/jamesos-version 2>/dev/null); VERSION="${JV:-'"$JAMESOS_RELEASE"'} (based on $BASE $VERSION)"  # JamesOS'
}
motd_update() {
  motd_line | rssh "$1" "cat > /tmp/jamesos-motd" || return 1
  rssh "$1" sh -s <<'MOTD'
M=/usr/sbin/vector-motd
[ -f $M ] || exit 0
if grep -qF -f /tmp/jamesos-motd $M; then rm -f /tmp/jamesos-motd; exit 0; fi
W=0
awk '$2=="/" && $4 ~ /^ro/ {f=1} END {exit !f}' /proc/mounts && W=1 && mount -o remount,rw /
[ -f $M.wireos-backup ] || cp $M $M.wireos-backup
sed -i '/# JamesOS$/d' $M
sed -i '/^VERSION=/r /tmp/jamesos-motd' $M
rm -f /tmp/jamesos-motd
sync
[ "$W" = 1 ] && mount -o remount,ro /
true
MOTD
}

# before go/deploy: stop Vector's programs, so a running program file (like vic-anim) can be replaced
# (otherwise: "rsync: open /bin/vic-anim failed: Text file busy"). deploy starts them again.
pre_deploy() {
  echo "== Stopping Vector's programs for the copy (his face goes dark for a moment) =="
  $SSH "systemctl stop anki-robot.target" || true
}

# after go/deploy: mark the robot as running a TEST build (auto-update leaves it alone until you publish)
after_deploy() {
  local T="test-$(date +%Y%m%d-%H%M%S)"
  if stamp_version "$IP" "$T"; then echo "== Marked as JamesOS $JAMESOS_RELEASE TEST ($T) =="; else echo "(couldn't write the version file - not a problem for testing)"; fi
  setup_updater "$IP" >/dev/null 2>&1 || true
  motd_update "$IP" >/dev/null 2>&1 || true
  $SSH "systemctl is-active -q anki-robot.target || systemctl start anki-robot.target" || true
}

# Only one window at a time may update or roll back a robot (publish, update-now, rollback and the
# auto-update window can all run at once otherwise - they used to trip over each other).
# robot_lock IP: takes the lock for this robot, or returns 1 if another window has it.
robot_lock() {
  mkdir -p jamesos
  exec {ROBOT_LOCK_FD}>"jamesos/.lock-$1"
  flock -n "$ROBOT_LOCK_FD" && return 0
  exec {ROBOT_LOCK_FD}>&-
  return 1
}
robot_unlock() { [[ -n "$ROBOT_LOCK_FD" ]] && exec {ROBOT_LOCK_FD}>&-; ROBOT_LOCK_FD=""; }

# check one robot and update it if needed. Sets STATUS. $2 = "force" to skip the charger/failed checks
update_robot() {
  if ! robot_lock "$1"; then
    STATUS="busy: another JamesOS window is updating it right now - will check again"; return
  fi
  update_robot_locked "$@"
  robot_unlock
}

update_robot_locked() {
  local ip=$1 force=$2 V SHA info have failed why rc out shown=0 n i
  STATUS=""
  [[ -f $UPD/version.txt && -f $UPD/jamesos-update.tar.gz ]] || { STATUS="nothing published yet (run: ./jamesos.sh publish)"; return; }
  V=$(cat "$UPD/version.txt"); SHA=$(cat "$UPD/jamesos-update.sha256")
  rssh "$ip" true 2>/dev/null || { STATUS="offline"; return; }
  setup_updater "$ip" >/dev/null 2>&1 || { STATUS="couldn't set up the updater on it (try: ./jamesos.sh use $ip; ./jamesos.sh updater)"; return; }
  info=$(rssh "$ip" "$RB version; cat $RU/failed_version 2>/dev/null; echo") || { STATUS="offline"; return; }
  have=$(sed -n 1p <<<"$info"); failed=$(sed -n 2p <<<"$info")
  if [[ "$have" == "$V" && "$force" != force ]]; then STATUS="up to date (JamesOS $V)"; return; fi
  # a TEST build you sent with go/deploy: only replace it when you publish something newer
  if [[ "$have" == test-* && "$force" != force && ! "$V" > "${have#test-}" ]]; then
    STATUS="testing a build (${have#test-}) that is newer than the published $V - leaving it alone"; return
  fi
  if [[ "$failed" == "$V" && "$force" != force ]]; then
    STATUS="skipped: $V did not start on this robot before, so it went back to $have (publish a fixed build, or: ./jamesos.sh update-now $ip)"; return
  fi
  if [[ "$force" != force ]]; then
    rc=0; why=$(rssh "$ip" "$RB ready") || rc=$?
    if [[ $rc -ne 0 ]]; then STATUS="new version $V is waiting: ${why:-could not check}"; return; fi
  fi

  if rssh "$ip" "$RB busy" >/dev/null 2>&1; then
    STATUS="busy: an update or rollback is already running on it - will check again"; return
  fi
  echo "[$(date +%H:%M)] $(robot_name "$ip"): updating $have -> $V  (sending $(du -h "$UPD/jamesos-update.tar.gz" | cut -f1))..."
  # each update gets its own file name, so two updates can never overwrite each other's file
  local pkg="$RU/incoming-$$-$RANDOM.tar.gz"
  if ! rssh "$ip" "rm -f $RU/incoming.tar.gz; cat > $pkg.tmp && mv $pkg.tmp $pkg" < "$UPD/jamesos-update.tar.gz"; then
    rssh "$ip" "rm -f $pkg.tmp" 2>/dev/null
    STATUS="sending failed (Wi-Fi?) - will try again"; return
  fi
  # run the install ON the robot, detached, so a Wi-Fi hiccup can't stop it halfway
  rssh "$ip" "rm -f $RU/last.rc $RU/last.out; nohup sh -c '$RB install $pkg $V $SHA > $RU/last.out 2>&1; echo \$? > $RU/last.rc; rm -f $pkg' > /dev/null 2>&1 < /dev/null &" \
    || { STATUS="couldn't start the install - will try again"; return; }
  rc=""
  for i in $(seq 1 200); do
    sleep 5
    out=$(rssh "$ip" "cat $RU/last.out 2>/dev/null; echo; echo @@RC=\$(cat $RU/last.rc 2>/dev/null)" 2>/dev/null) || continue
    n=$(grep -v '^@@RC=' <<<"$out" | grep -c . || true)
    if (( n > shown )); then grep -v '^@@RC=' <<<"$out" | grep . | tail -n $(( n - shown )) | sed 's/^/      /'; shown=$n; fi
    rc=$(grep '^@@RC=' <<<"$out" | cut -d= -f2)
    [[ -n "$rc" ]] && break
  done
  case "$rc" in
    0) STATUS="up to date (JamesOS $V) - just updated!" ;;
    4) STATUS="update $V did not start properly, so it went back to the old version" ;;
    7) rssh "$ip" "echo $V > $RU/failed_version" 2>/dev/null; STATUS="not installed: this robot's WireOS base doesn't match the build (see above)" ;;
    8) STATUS="busy: another update or rollback was already running on it - will check again" ;;
    "") STATUS="lost contact during the update - check it with: ./jamesos.sh robots" ;;
    *) STATUS="update not installed (see the lines above) - will try again" ;;
  esac
}

robot_name() { local n; n=$(awk -v ip="$1" '$1==ip {print $2}' "$ROBOTS" 2>/dev/null); echo "${n:-$1}"; }
# v23i private: family names - jamesos/family.txt, one line per robot: ROBOT NAME PRONOUN MISS(yes|no)
FAMILY=jamesos/family.txt
fam_line() {   # -> "NAME PRONOUN MISS(1|0)" for a robot name from robots.txt
  local r="$1" n p m
  read -r _ n p m < <(awk -v r="$r" '$1==r' "$FAMILY" 2>/dev/null | head -1)
  [[ -n "$n" ]] || n=$(echo "$r" | tr 'a-z' 'A-Z')
  echo "$n ${p:-them} $([[ "$m" == no ]] && echo 0 || echo 1)"
}

case "$1" in
  go)
    echo "== Building (only changed files get recompiled) =="
    ./build/build-v.sh
    echo "== Deploying to $IP (only changed files get copied) =="
    pre_deploy
    ./build/deploy-v.sh
    after_deploy
    done_msg
    ;;

  deploy)
    pre_deploy
    ./build/deploy-v.sh
    after_deploy
    done_msg
    ;;

  push)
    FILE="$2"
    if [[ -z "$FILE" || ! -f "$FILE" ]]; then
      echo "Usage: ./jamesos.sh push resources/config/<path to file>"; exit 1
    fi
    case "$FILE" in
      resources/config/*) REL="${FILE#resources/config/}" ;;
      *) echo "The file must be inside resources/config/ (so I know where it goes on Vector)."; exit 1 ;;
    esac
    DEST="/anki/data/assets/cozmo_resources/config/$REL"
    echo "== Copying $FILE -> Vector:$DEST =="
    $SSH "mount -o rw,remount / && mkdir -p '$(dirname "$DEST")' && cat > '$DEST' && sync && mount -o ro,remount / || true" < "$FILE"
    echo "Copied. Eye colors work right away; most other files need: ./jamesos.sh restart"
    done_msg
    ;;

  restart)
    echo "== Restarting Vector's programs (takes about 30 seconds) =="
    $SSH "systemctl restart anki-robot.target"
    done_msg
    ;;

  ssh)
    exec $SSH
    ;;

  which)
    echo "Working on the Vector at $IP"
    $SSH "hostname; cat /anki/etc/version 2>/dev/null" || echo "(can't reach it right now)"
    ;;

  lights)
    if [[ "$2" == "off" ]]; then
      echo "== Removing JamesOS lights (back to WireOS lights) =="
      $SSH "rm -rf /data/data/customBackpackLights && systemctl restart anki-robot.target"
    else
      [[ -f jamesos/backpackLights/off.json ]] || { echo "jamesos/backpackLights/off.json is missing. Run the feature installer first."; exit 1; }
      echo "== Sending JamesOS backpack lights to Vector =="
      tar -C jamesos/backpackLights -cf - . | $SSH "rm -rf /data/data/customBackpackLights && mkdir -p /data/data/customBackpackLights && tar -C /data/data/customBackpackLights -xf - && mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && rm -f /data/data/jamesos/lights.off && sync"
      if [[ -f jamesos/backpackLights-halloween/off.json ]]; then
        echo "== Sending Halloween lights too =="
        tar -C jamesos/backpackLights-halloween -cf - . | $SSH "rm -rf /data/data/jamesos/lights-halloween && mkdir -p /data/data/jamesos/lights-halloween && tar -C /data/data/jamesos/lights-halloween -xf - && sync"
      fi
      echo "== Restarting Vector's programs so he loads the new lights (about 30 seconds) =="
      $SSH "systemctl restart anki-robot.target"
    fi
    done_msg
    ;;

  starlight)
    if [[ "$2" == "off" ]]; then
      $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && touch /data/data/jamesos/starlight.off && sync"
      echo "Starlight eyes OFF (takes about 2 seconds)"
    else
      [[ -f jamesos/starlight.conf ]] || { echo "jamesos/starlight.conf is missing. Run the feature installer first."; exit 1; }
      $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && cat > /data/data/jamesos/starlight.conf && chmod 666 /data/data/jamesos/starlight.conf && rm -f /data/data/jamesos/starlight.off && sync" < jamesos/starlight.conf
      echo "Starlight eyes ON with your settings (takes about 2 seconds)"
    fi
    done_msg
    ;;

  style)
    case "$2" in white|rainbow|shooting|gold) ;; *) echo "Usage: ./jamesos.sh style white|rainbow|shooting"; exit 1 ;; esac
    $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && echo $2 > /data/data/jamesos/starlight.style && chmod 666 /data/data/jamesos/starlight.style && sync"
    echo "Sparkle style: $2 (takes about 2 seconds; sparkles must be on)"
    done_msg
    ;;

  petfx)
    case "$2" in
      galaxy) $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && touch /data/data/jamesos/hearts.on && echo galaxy > /data/data/jamesos/petfx && chmod 666 /data/data/jamesos/petfx /data/data/jamesos/hearts.on && sync" ;;
      off) $SSH "rm -f /data/data/jamesos/hearts.on && sync" ;;
      *) echo "Usage: ./jamesos.sh petfx galaxy|off"; exit 1 ;;
    esac
    echo "Petting effect: $2 (takes about 2 seconds)"
    done_msg
    ;;

  hearts|mood)
    FLAG="/data/data/jamesos/$1.on"
    if [[ "$2" == "on" ]]; then
      $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && touch $FLAG && chmod 666 $FLAG && sync"
    elif [[ "$2" == "off" ]]; then
      $SSH "rm -f $FLAG && sync"
    else
      echo "Usage: ./jamesos.sh $1 on|off"; exit 1
    fi
    echo "$1 $2 (takes about 2 seconds)"
    done_msg
    ;;

  install-all)
    echo "== Checking the Vector at $IP =="
    VER=$($SSH "cat /anki/etc/version 2>/dev/null; hostname" | tr '\n' ' ') || { echo "Can't reach the Vector at $IP. Is it on and on Wi-Fi?"; exit 1; }
    echo "Found: $VER"
    case "$VER" in
      3.0.2.*) ;;
      *) echo "This Vector is not on WireOS 3.0.2. JamesOS needs that base."
         read -p "Continue anyway? (y/n): " yn; [[ "$yn" == [Yy]* ]] || exit 1 ;;
    esac
    [[ -d _build/vicos/Release ]] || { echo "No JamesOS build yet. Run ./jamesos.sh go once first (with any robot)."; exit 1; }

    echo; echo "== 1/6 Copying JamesOS programs =="
    pre_deploy
    ./build/deploy-v.sh
    after_deploy

    echo; echo "== 2/6 Backpack lights =="
    "$0" lights

    echo; echo "== 3/6 Boot animation + startup picture =="
    "$0" bootanim
    "$0" splash "${JAMESOS_SPLASH:-vchevron}" || echo "(skipped the startup picture)"

    echo; echo "== 4/6 Welcome message =="
    motd_update "$IP" && echo "Welcome message: JamesOS" || echo "(skipped the welcome message)"

    echo; echo "== 5/6 Settings: starlight sparkles + galaxy eyes when petted =="
    if [[ -f jamesos/starlight.conf ]]; then
      $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && cat > /data/data/jamesos/starlight.conf && chmod 666 /data/data/jamesos/starlight.conf && rm -f /data/data/jamesos/starlight.off" < jamesos/starlight.conf
    fi
    $SSH "touch /data/data/jamesos/hearts.on && echo galaxy > /data/data/jamesos/petfx && chmod 666 /data/data/jamesos/hearts.on /data/data/jamesos/petfx && sync"

    echo; echo "== 6/6 Auto-updater =="
    if setup_updater "$IP"; then echo "Auto-updater ready (it gets new versions when you: ./jamesos.sh publish)"; else echo "(skipped the auto-updater - you can add it later with: ./jamesos.sh updater)"; fi
    mkdir -p jamesos; touch "$ROBOTS"; grep -q "^$IP\( \|$\)" "$ROBOTS" || echo "$IP" >> "$ROBOTS"

    echo; echo "== Rebooting so everything starts fresh (about 1 minute) =="
    $SSH "reboot" || true
    echo "JamesOS is installed on the Vector at $IP. Watch for the warp boot animation!"
    done_msg
    ;;

  theme)
    case "$2" in normal|halloween|christmas|lunar|auto|glitch|retro|galaxymax|newyear|valentine|stpatrick|aprilfools|easter|mothersday|fireworks|thanksgiving) ;;
      *) echo "Usage: ./jamesos.sh theme auto|normal|halloween|christmas|lunar|newyear|valentine|stpatrick|aprilfools|easter|mothersday|fireworks|thanksgiving"; exit 1 ;; esac
    $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && echo $2 > /data/data/jamesos/theme && chmod 666 /data/data/jamesos/theme && sync"
    echo "Theme: $2 (eyes and back lights change in a few seconds, no restart)"
    [[ "$2" == auto ]] && echo "Auto holidays: he picks the holiday theme by date (which holidays: ./jamesos.sh holidays all|western|off)"
    done_msg
    ;;

  funeyes)
    case "$2" in
      on)   $SSH "rm -f /data/data/jamesos/funeyes.off /data/data/jamesos/funeyes.auto && sync" ;;
      auto) $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && rm -f /data/data/jamesos/funeyes.off && touch /data/data/jamesos/funeyes.auto && chmod 666 /data/data/jamesos/funeyes.auto && sync" ;;
      off)  $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && rm -f /data/data/jamesos/funeyes.auto && touch /data/data/jamesos/funeyes.off && chmod 666 /data/data/jamesos/funeyes.off && sync" ;;
      *) echo "Usage: ./jamesos.sh funeyes auto|on|off"; exit 1 ;;
    esac
    echo "Fun eyes: $2 (changes in a few seconds; hats and other extras stay)"
    done_msg
    ;;

  shiny)
    case "$2" in
      test) $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && echo \$(date +%Y%m%d) test > /data/data/jamesos/shiny && chmod 666 /data/data/jamesos/shiny && sync"
            echo "He's shiny for the rest of today (a test, so no badge). Undo: ./jamesos.sh shiny off" ;;
      off)  $SSH "echo \$(date +%Y%m%d) 0 > /data/data/jamesos/shiny && sync"
            echo "Shiny off - today is a normal day." ;;
      *)    echo "Today's roll on this robot (1 = shiny, 0 = normal, test = test):"
            $SSH "cat /data/data/jamesos/shiny 2>/dev/null || echo '  not rolled yet'"
            echo "Usage: ./jamesos.sh shiny test|off" ;;
    esac
    ;;

  bluemoon)
    case "$2" in
      test) $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && touch /data/data/jamesos/bluemoon.test && chmod 666 /data/data/jamesos/bluemoon.test && sync"
            echo "Blue moon test ON (no badge). Undo: ./jamesos.sh bluemoon off" ;;
      off)  $SSH "rm -f /data/data/jamesos/bluemoon.test && sync"; echo "Blue moon test off." ;;
      *)    echo "Next blue moons: Dec 31 2028, Mar 30 2029, Oct 30 2031, Jul 31 2034, Jan 31 2037, Mar 31 2037"
            echo "(6 PM that night until 6 AM. Pet him then for the BLUE MOON badge.)"
            echo "Usage: ./jamesos.sh bluemoon test|off" ;;
    esac
    ;;

  private)
    case "$2" in
      on)  mkdir -p jamesos && touch jamesos/private.on
           echo "Private extras ON (Vector family, prototype). jamesos/ never goes to GitHub."
           echo "Next: python3 jamesos-features/apply_jamesos_features.py   then   ./jamesos.sh go" ;;
      off) rm -f jamesos/private.on
           echo "Private extras OFF. Run the installer + go (or publish) to build without them." ;;
      *)   [[ -f jamesos/private.on ]] && echo "Private extras: ON" || echo "Private extras: OFF"
           echo "Usage: ./jamesos.sh private on|off" ;;
    esac
    ;;

  family)
    [[ -f jamesos/private.on ]] || { echo "This is a private extra. Turn it on first: ./jamesos.sh private on"; exit 1; }
    touch "$FAMILY"
    case "$2" in
      name)
        R="$3"; N=$(echo "$4" | tr 'a-z' 'A-Z' | tr -cd 'A-Z0-9.-' | cut -c1-10); P=$(echo "${5:-them}" | tr 'A-Z' 'a-z')
        [[ -n "$R" && -n "$N" ]] || { echo "Usage: ./jamesos.sh family name ROBOT NAME him|her|them   (example: family name 2.0 Bella her)"; exit 1; }
        case "$P" in him|her|them) ;; *) echo "The last word must be him, her or them"; exit 1 ;; esac
        robot_list | awk -v r="$R" '$2==r {f=1} END {exit !f}' || echo "(note: no robot called '$R' in your robot list yet - names are: $(robot_list | awk '{print $2}' | tr '\n' ' '))"
        M=$(awk -v r="$R" '$1==r {print $4}' "$FAMILY"); M=${M:-yes}
        { awk -v r="$R" '$1!=r' "$FAMILY"; echo "$R $N $P $M"; } > "$FAMILY.tmp" && mv "$FAMILY.tmp" "$FAMILY"
        echo "$R is now called $N ($P). His brothers and sisters learn it within a minute (auto-update window must be running)." ;;
      miss)
        R="$3"
        case "$4" in on) M=yes ;; off) M=no ;; *) echo "Usage: ./jamesos.sh family miss ROBOT on|off   (off = the others don't get sad when he's away, e.g. the PVT)"; exit 1 ;; esac
        read -r N P _ <<< "$(fam_line "$R")"
        { awk -v r="$R" '$1!=r' "$FAMILY"; echo "$R $N $P $M"; } > "$FAMILY.tmp" && mv "$FAMILY.tmp" "$FAMILY"
        echo "Others miss $R: $4" ;;
      *)
        echo "Your Vector family (robot / name / him-her-them / missed when away):"
        for r in $(robot_list | awk '$2 != "" {print $2}'); do read -r N P M <<< "$(fam_line "$r")"; echo "  $r  $N  $P  $([[ $M == 1 ]] && echo yes || echo no)"; done
        echo "Change:  ./jamesos.sh family name 2.0 Bella her      ./jamesos.sh family miss PVT off" ;;
    esac
    exit 0
    ;;

  prototype)
    [[ -f jamesos/private.on ]] || { echo "This is a private extra. Turn it on first: ./jamesos.sh private on"; exit 1; }
    KS=/anki/data/assets/cozmo_resources/config/jamesos/jamesos-keep.sh
    case "$2" in
      on)  $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && echo 1 > /data/data/jamesos/prototype && chmod 666 /data/data/jamesos/prototype && sh $KS save; sync"
           echo "$(robot_name "$IP") is marked as a PROTOTYPE (kept after CLEAR OUT SOUL). Look in ABOUT JAMESOS." ;;
      off) $SSH "sh $KS unmark"; echo "Prototype mark removed from $(robot_name "$IP")." ;;
      *)   echo "Usage: ./jamesos.sh prototype on|off   (works on the robot picked with: ./jamesos.sh use NAME)" ;;
    esac
    ;;

  holidays)
    case "$2" in all|western|off) ;; *) echo "Usage: ./jamesos.sh holidays all|western|off   (all = with Chinese New Year)"; exit 1 ;; esac
    $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && echo $2 > /data/data/jamesos/holidays && chmod 666 /data/data/jamesos/holidays && sync"
    echo "Holidays: $2 (auto theme + countdown banners; changes in about 2 seconds)"
    done_msg
    ;;

  sleepscreen)
    case "$2" in stars|clock|both) $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && echo $2 > /data/data/jamesos/sleepscreen && chmod 666 /data/data/jamesos/sleepscreen && sync" ;;
      off) $SSH "rm -f /data/data/jamesos/sleepscreen && sync" ;;
      *) echo "Usage: ./jamesos.sh sleepscreen stars|clock|both|off"; exit 1 ;; esac
    echo "Sleep screen: $2 (shows when he sleeps on the charger)"
    done_msg
    ;;


  sleepy)
    case "$2" in
      on)  $SSH "rm -f /data/data/jamesos/sleepy.off && sync" ;;
      off) $SSH "mkdir -p /data/data/jamesos && chmod 777 /data/data/jamesos && touch /data/data/jamesos/sleepy.off && chmod 666 /data/data/jamesos/sleepy.off && sync" ;;
      *) echo "Usage: ./jamesos.sh sleepy on|off"; exit 1 ;;
    esac
    echo "Sleepy eyes on low battery: $2"
    done_msg
    ;;

  badges)
    if [[ "$2" == "restore" ]]; then
      echo "== Saved copy on Vector (/persist/jamesos) =="
      $SSH "cat /persist/jamesos/badges.txt 2>/dev/null || echo '  (no saved copy)'"
      $SSH "sh /anki/data/assets/cozmo_resources/config/jamesos/jamesos-keep.sh restore && systemctl restart anki-robot.target"
      exit 0
    fi
    if [[ "$2" == "reset" ]]; then
      read -p "Delete ALL badges and pet stats on $IP (also the saved copy)? Type yes: " yn
      [[ "$yn" == yes ]] || { echo "Nothing deleted."; exit 0; }
      $SSH "sh /anki/data/assets/cozmo_resources/config/jamesos/jamesos-keep.sh forget && systemctl restart anki-robot.target"
      exit 0
    fi
    $SSH "cat /data/data/jamesos/stats.txt 2>/dev/null; echo; echo 'Unlocked:'; cat /data/data/jamesos/badges.txt 2>/dev/null || echo '  none yet - pet him!'"
    ;;

  web)
    if [[ "$2" == "stop" ]]; then
      pkill -f "jamesos-features/web/jamesos_web.py" && echo "Web control page stopped." || echo "It wasn't running."
      exit 0
    fi
    [[ -f jamesos-features/web/jamesos_web.py ]] || { echo "jamesos-features/web is missing (extract jamesos-features-v13 first)"; exit 1; }
    exec python3 jamesos-features/web/jamesos_web.py
    ;;

  pets)
    $SSH "cat /data/data/jamesos/pets.txt 2>/dev/null | awk '{print \"Pets on \" \$1 \": \" \$2}' || true"
    ;;

  bootanim)
    CUSTOM=/persist/boot_anim.raw
    # make /persist writable for a moment if it is read-only, then put it back
    # /persist may be its own drive or just a folder on the (read-only) system drive:
    # find which drive it is on, unlock it for a moment, then lock it again
    RW='MP=$(df /persist 2>/dev/null | tail -1 | awk "{print \$NF}"); [ -z "$MP" ] && MP=/; WASRO=0; awk -v m="$MP" "\$2==m && \$4 ~ /^ro/ {f=1} END {exit !f}" /proc/mounts && WASRO=1 && mount -o remount,rw "$MP";'
    RO='[ "$WASRO" = 1 ] && mount -o remount,ro "$MP"; true'
    # some robots (the PVT) mount /persist only AFTER the boot animation starts, so it plays the copy
    # hidden underneath on the system drive: keep that copy the same as the real one
    HIDDEN='if [ "$(df /persist 2>/dev/null | tail -1 | awk "{print \$NF}")" = /persist ]; then RW0=0; awk "\$2==\"/\" && \$4 ~ /^ro/ {f=1} END {exit !f}" /proc/mounts && RW0=1 && mount -o remount,rw /; mkdir -p /tmp/jr && mount --bind / /tmp/jr && { mkdir -p /tmp/jr/persist; if [ -f /persist/boot_anim.raw ]; then cp /persist/boot_anim.raw /tmp/jr/persist/boot_anim.raw; else rm -f /tmp/jr/persist/boot_anim.raw; fi; sync; umount /tmp/jr; }; [ "$RW0" = 1 ] && mount -o remount,ro /; fi; true'
    if [[ "$2" == "off" ]]; then
      $SSH "$RW rm -f $CUSTOM; sync; $RO"
      $SSH "$HIDDEN"
      echo "Normal boot animation is back (you'll see it on the next reboot)"
      done_msg
      exit 0
    fi
    STYLE=wakeup
    MODEL=""
    for a in "$2" "$3"; do
      case "$a" in
        wakeup|galaxy|warp|prototype) STYLE="$a" ;;
        1.0|1|2.0|2) MODEL="$a" ;;
        "") ;;
        *) echo "Usage: ./jamesos.sh bootanim [wakeup|galaxy|warp|prototype] [1.0|2.0]   or   ./jamesos.sh bootanim off"; exit 1 ;;
      esac
    done
    if [[ -z "$MODEL" ]]; then
      HW=$($SSH "emr-cat v 2>/dev/null" | tr -d ' \r\n' || true)
      if [[ "$HW" =~ ^[0-9a-fA-F]+$ ]]; then
        if (( 16#$HW >= 16#20 )); then MODEL=2.0; else MODEL=1.0; fi
        echo "This Vector is a $MODEL (hardware $HW)"
      else
        echo "Couldn't tell if this is a 1.0 or 2.0. Run: ./jamesos.sh bootanim 2.0   (or 1.0)"
        exit 1
      fi
    fi
    case "$MODEL" in
      2.0|2) SIZE=160x80 ;;
      1.0|1) SIZE=184x96 ;;
    esac
    if [[ "$STYLE" == prototype && ! -f jamesos/private.on ]]; then
      echo "The prototype boot animation is a private extra. Turn it on first: ./jamesos.sh private on"; exit 1
    fi
    if [[ "$STYLE" == wakeup || "$STYLE" == galaxy || "$STYLE" == prototype ]]; then
      FILE=jamesos-features/bootanim/$STYLE/boot_$SIZE.raw
    else
      FILE=jamesos/bootanim/boot_$SIZE.raw
    fi
    echo "Boot animation: $STYLE"
    [[ -f "$FILE" ]] || { echo "$FILE is missing"; exit 1; }
    echo "== Sending $FILE to Vector =="
    $SSH "$RW cat > $CUSTOM.tmp && mv $CUSTOM.tmp $CUSTOM && sync; R=\$?; $RO; exit \$R" < "$FILE" \
      || { echo "Couldn't write to /persist on this Vector. Send Claude a screenshot."; exit 1; }
    $SSH "$HIDDEN" || echo "(note: couldn't update the early-boot copy - send Claude a screenshot)"
    echo "Done! Reboot Vector to see it:  ./jamesos.sh ssh   then type: reboot"
    done_msg
    ;;

  publish)
    S=_build/staging/Release
    [[ -d $S/anki/bin ]] || { echo "No deployed build found. Run ./jamesos.sh go first (and check it works on that robot)."; exit 1; }
    mkdir -p "$UPD"
    V=$(date +%Y%m%d-%H%M%S)
    echo "== Packing the build you last sent with go/deploy as JamesOS $V =="
    echo "   (built $(date -r "$S/anki/bin" '+%Y-%m-%d %H:%M'))"
    # the version goes INSIDE the package (anki/etc/jamesos-version), so the robot always knows it
    VT=$(mktemp -d)
    mkdir -p "$VT/anki/etc"
    printf '%s\n%s\n' "$V" "$JAMESOS_RELEASE" > "$VT/anki/etc/jamesos-version"
    if ! tar --owner=0 --group=0 --numeric-owner -czf "$UPD/jamesos-update.tmp" -C "$S" anki -C "$VT" anki/etc/jamesos-version 2>/dev/null; then
      echo "Some build files need admin rights to read. Your WSL password, please:"
      sudo tar --owner=0 --group=0 --numeric-owner -czf "$UPD/jamesos-update.tmp" -C "$S" anki -C "$VT" anki/etc/jamesos-version
      sudo chown "$(id -u):$(id -g)" "$UPD/jamesos-update.tmp"
    fi
    rm -rf "$VT"
    mv "$UPD/jamesos-update.tmp" "$UPD/jamesos-update.tar.gz"
    sha256sum "$UPD/jamesos-update.tar.gz" | cut -d' ' -f1 > "$UPD/jamesos-update.sha256"
    echo "$V" > "$UPD/version.txt"
    echo "Published JamesOS $JAMESOS_RELEASE ($V, $(du -h "$UPD/jamesos-update.tar.gz" | cut -f1))."
    echo
    echo "== Updating robots that are online now (others get it when they come online and autoupdate is running) =="
    exec "$0" autoupdate once
    ;;

  autoupdate)
    set +e
    if [[ "$2" == "startup" ]]; then
      WINAPPDATA=$(cd /mnt/c && cmd.exe /c "echo %APPDATA%" 2>/dev/null | tr -d '\r')
      [[ -n "$WINAPPDATA" ]] || { echo "Couldn't find your Windows Startup folder."; exit 1; }
      SU="$(wslpath "$WINAPPDATA")/Microsoft/Windows/Start Menu/Programs/Startup"
      BAT="$SU/JamesOS-AutoUpdate.bat"
      if [[ "$3" == "off" ]]; then
        rm -f "$BAT"; echo "The auto-updater won't start with Windows anymore."; exit 0
      fi
      printf '@echo off\r\ntitle JamesOS auto-update\r\nwsl.exe -d %s --cd "%s" -- ./jamesos.sh autoupdate\r\npause\r\n' "${WSL_DISTRO_NAME:-Ubuntu}" "$PWD" > "$BAT"
      echo "Done! The JamesOS auto-update window will open by itself every time Windows starts."
      echo "Starting it now too..."
      (cd /mnt/c && cmd.exe /c start "" "$(wslpath -w "$BAT")" >/dev/null 2>&1)
      exit 0
    fi
    declare -A LAST
    if [[ "$2" != "once" && -f jamesos-features/web/jamesos_web.py ]] && ! (exec 3<>/dev/tcp/127.0.0.1/8765) 2>/dev/null; then
      mkdir -p jamesos
      nohup python3 jamesos-features/web/jamesos_web.py > jamesos/web.log 2>&1 < /dev/null &
      echo "== JamesOS web control page: http://localhost:8765 =="
    fi
    [[ "$2" == "once" ]] || { echo "== JamesOS auto-update: checking your robots every minute. Leave this window open (you can minimize it). Ctrl+C stops it. =="; echo "   Robots: $(robot_list | awk '{print ($2?$2:$1)}' | tr '\n' ' ')"; }
    LAST_FIND=0
    while true; do
      ANY_OFF=0
      for ip in $(robot_list | awk '{print $1}'); do
        update_robot "$ip"
        [[ "$STATUS" == offline ]] && ANY_OFF=1
        if [[ "${LAST[$ip]}" != "$STATUS" ]]; then
          echo "[$(date +%H:%M)] $(robot_name "$ip"): $STATUS"
          LAST[$ip]="$STATUS"
        fi
      done
      [[ "$2" == "once" ]] && break
      # v23 private: tell each robot about its family (tiny faces, hello / missing / back, pet scoreboard)
      # line 1: "<time> <siblings online> <family size>", then per sibling: "S NAME PRONOUN ONLINE MISS PETS"
      if [[ -f jamesos/private.on ]]; then
        FAM_IPS=(); FAM_ON=(); unset FAM_PETS; declare -A FAM_PETS=()
        TODAY=$(date +%Y%m%d)
        for ip in $(robot_list | awk '{print $1}'); do
          FAM_IPS+=("$ip")
          if [[ "${LAST[$ip]}" != offline ]]; then
            FAM_ON+=("$ip")
            FAM_PETS[$ip]=$(rssh "$ip" "cat /data/data/jamesos/pets.txt 2>/dev/null" 2>/dev/null | awk -v d="$TODAY" '$1==d {print $2}')
          fi
        done
        for ip in "${FAM_ON[@]}"; do
          {
            for other in "${FAM_IPS[@]}"; do
              [[ "$other" == "$ip" ]] && continue
              on=0; [[ "${LAST[$other]}" != offline ]] && on=1
              read -r fn fp fm <<< "$(fam_line "$(robot_name "$other")")"
              echo "S $fn $fp $on $fm ${FAM_PETS[$other]:-0}"
            done
          } | rssh "$ip" "{ echo \$(date +%s) $(( ${#FAM_ON[@]} - 1 )) ${#FAM_IPS[@]}; cat; } > /dev/shm/jamesos_family.tmp && mv /dev/shm/jamesos_family.tmp /dev/shm/jamesos_family" 2>/dev/null
        done
      fi
      # a robot is "offline"? maybe it just got a new IP: look for it (at most every 10 minutes)
      if [[ $ANY_OFF == 1 && -s jamesos/robot-names.txt && $(( $(date +%s) - LAST_FIND )) -gt 600 ]]; then
        LAST_FIND=$(date +%s)
        "$0" find quiet < /dev/null
      fi
      sleep 60
    done
    ;;

  update-now)
    set +e
    T=${2:-$IP}
    echo "== Updating $T right now =="
    update_robot "$T" force
    echo "$(robot_name "$T"): $STATUS"
    done_msg
    ;;

  poweroff)
    T=${2:-$IP}
    echo "== Turning off the Vector at $T (same as holding his back button) =="
    if rssh "$T" "touch /dev/shm/jamesos_poweroff"; then
      echo "Sent. He saves, then powers off in about 5 seconds. Press his back button to turn him on again."
    else
      echo "Can't reach $T - is he on?"
    fi
    ;;

  voice-poweroff)
    # Called by wire-pod when someone says "power off". $2 = the serial number (ESN) of the robot that heard it.
    set +e
    ESN=$(echo "$2" | tr 'A-Z' 'a-z' | tr -cd '0-9a-f')
    [[ -n "$ESN" ]] || { echo "no robot serial"; exit 1; }
    CACHE=jamesos/esn.txt
    mkdir -p jamesos; touch "$CACHE"
    T=$(awk -v e="$ESN" '$1==e {print $2}' "$CACHE" | head -n 1)
    if [[ -n "$T" ]] && [[ "$(rssh "$T" "emr-cat e" 2>/dev/null | tr -d ' \r\n' | tr 'A-Z' 'a-z')" != "$ESN" ]]; then T=""; fi
    if [[ -z "$T" ]]; then
      for ip in $(robot_list | awk '{print $1}'); do
        e=$(rssh "$ip" "emr-cat e" 2>/dev/null | tr -d ' \r\n' | tr 'A-Z' 'a-z')
        [[ -n "$e" ]] || continue
        grep -v " $ip\$" "$CACHE" | grep -v "^$e " > "$CACHE.tmp"; echo "$e $ip" >> "$CACHE.tmp"; mv "$CACHE.tmp" "$CACHE"
        [[ "$e" == "$ESN" ]] && T=$ip
      done
    fi
    [[ -n "$T" ]] || { echo "robot $ESN is not in jamesos/robots.txt"; exit 1; }
    # wait 4 seconds on the robot so he can say goodbye first, then power off
    rssh "$T" "nohup sh -c 'sleep 4; touch /dev/shm/jamesos_poweroff' > /dev/null 2>&1 < /dev/null &"
    echo "powering off $T"
    ;;

  voice-poweroff-setup|voice-poweroff-remove)
    set +e
    WINAPPDATA=$(cd /mnt/c && cmd.exe /c "echo %APPDATA%" 2>/dev/null | tr -d '\r')
    F="$(wslpath "$WINAPPDATA")/wire-pod/customIntents.json"
    [[ -n "$WINAPPDATA" && -d "$(dirname "$F")" ]] || { echo "Couldn't find wire-pod's settings folder (AppData\\Roaming\\wire-pod)."; exit 1; }
    MODE=add; [[ "$1" == "voice-poweroff-remove" ]] && MODE=remove
    python3 - "$F" "$MODE" "${WSL_DISTRO_NAME:-Ubuntu}" "$PWD" <<'PY'
import json, os, shutil, sys, time
path, mode, distro, repo = sys.argv[1:5]
intents = []
if os.path.exists(path):
    txt = open(path, encoding="utf-8").read().strip()
    intents = json.loads(txt) if txt else []
    shutil.copy(path, path + ".backup-" + time.strftime("%Y%m%d-%H%M%S"))
intents = [i for i in intents if i.get("name") != "JamesOS power off"]
if mode == "add":
    intents.append({
        "name": "JamesOS power off",
        "description": "Vector says goodbye, then powers off (JamesOS)",
        "utterances": ["power off", "shut down now", "turn yourself off"],
        "intent": "intent_greeting_goodbye",
        "params": {"paramname": "", "paramvalue": ""},
        "exec": "C:\\Windows\\System32\\wsl.exe",
        "execargs": ["-d", distro, "--cd", repo, "--", "./jamesos.sh", "voice-poweroff", "!botSerial"],
        "issystem": False,
        "luascript": "",
    })
json.dump(intents, open(path, "w", encoding="utf-8"), indent=2)
print("Voice power off " + ("added to" if mode == "add" else "removed from") + " wire-pod (" + path + ")")
PY
    echo "Now close wire-pod and open it again so it loads the change."
    [[ "$MODE" == add ]] && echo 'Then say: "Hey Vector" ... "power off"'
    ;;

  rollback)
    T=${2:-$IP}
    robot_lock "$T" || { echo "Another JamesOS window is updating $T right now. Wait for it to finish, then try again."; exit 1; }
    echo "== Putting back the version from before the last update on $T (about 1 minute) =="
    setup_updater "$T" >/dev/null 2>&1
    rssh "$T" "$RB rollback"
    done_msg
    ;;

  updater)
    echo "== Installing the JamesOS updater on $IP =="
    setup_updater "$IP" && echo "Updater ready. Current JamesOS version on it: $(rssh "$IP" "$RB version")"
    mkdir -p jamesos; touch "$ROBOTS"; grep -q "^$IP\( \|$\)" "$ROBOTS" || echo "$IP" >> "$ROBOTS"
    ;;

  find)
    # Find every Vector on the Wi-Fi and fix the IPs in jamesos/robots.txt.
    # Robots are recognised by their ESN (serial number), which never changes - IPs and even
    # the hostname can. Names are kept in jamesos/robot-names.txt ("ESN name").
    set +e
    QUIET=0; [[ "$2" == quiet ]] && { QUIET=1; shift; }
    say() { [[ $QUIET == 1 ]] || echo "$@"; }
    NAMES=jamesos/robot-names.txt
    mkdir -p jamesos; touch "$NAMES" "$ROBOTS"
    NET=${2:-$(cut -d. -f1-3 robot_ip.txt 2>/dev/null)}
    [[ "$NET" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Usage: ./jamesos.sh find   (or: ./jamesos.sh find 192.168.0)"; exit 1; }
    say "== Looking for Vectors on $NET.1 - $NET.254 (about 10 seconds) =="
    CUR_ESN=$(rssh "$IP" "emr-cat e 2>/dev/null" 2>/dev/null | tr -d ' \r\n')
    TMPD=$(mktemp -d)
    for i in $(seq 1 254); do
      ( timeout 1 bash -c "exec 3<>/dev/tcp/$NET.$i/22" 2>/dev/null && touch "$TMPD/$NET.$i" ) &
    done
    wait
    : > "$TMPD/found"
    for f in "$TMPD"/$NET.*; do
      [[ -e "$f" ]] || continue
      ip=${f##*/}
      info=$(rssh "$ip" 'E=$(emr-cat e 2>/dev/null | tr -d " \r\n"); [ -n "$E" ] || E=$(tr " " "\n" < /proc/cmdline | sed -n "s/^androidboot.serialno=//p"); echo "$E $(hostname)"' 2>/dev/null) || continue
      esn=$(awk '{print $1}' <<<"$info"); host=$(awk '{print $2}' <<<"$info")
      [[ "$esn" =~ ^[0-9a-fA-F]{8}$ ]] || continue
      name=$(awk -v e="$esn" '$1==e {print $2; exit}' "$NAMES")
      if [[ -z "$name" ]]; then
        if [[ -t 0 && $QUIET == 0 ]]; then
          read -p "New Vector: ESN $esn ($host) at $ip. What do you call it (like 1.0, 2.0, PVT - no spaces)? " name
        fi
        name=${name// /_}; name=${name:-$host}
        echo "$esn $name" >> "$NAMES"
      fi
      echo "$ip $name $esn" >> "$TMPD/found"
    done
    # new robots.txt: everyone we found + robots we know that are off right now (old IP kept)
    { echo "# made by ./jamesos.sh find - IP name"; awk '{print $1, $2}' "$TMPD/found"; } > "$ROBOTS.new"
    while read -r ip name; do
      [[ -z "$ip" || "$ip" == \#* ]] && continue
      if ! awk -v n="$name" -v i="$ip" '$2==n || $1==i {f=1} END {exit !f}' "$TMPD/found"; then
        echo "$ip $name" >> "$ROBOTS.new"
        echo "$ip $name" >> "$TMPD/missing"
      fi
    done < "$ROBOTS"
    mv "$ROBOTS.new" "$ROBOTS"
    # the robot you are working on moved? follow it
    if [[ -n "$CUR_ESN" ]]; then echo "$CUR_ESN" > jamesos/current-esn; fi
    CUR_ESN=$(cat jamesos/current-esn 2>/dev/null)
    newip=$(awk -v e="$CUR_ESN" '$3==e {print $1; exit}' "$TMPD/found")
    if [[ -n "$newip" && "$newip" != "$IP" ]]; then
      echo "$newip" > robot_ip.txt
      say "The robot you work on moved: $IP -> $newip (robot_ip.txt updated)"
    fi
    n=$(wc -l < "$TMPD/found")
    if [[ $QUIET == 0 ]]; then
      echo "Found $n Vector(s):"
      while read -r ip name esn; do printf '  %-8s %-16s ESN %s\n' "$name" "$ip" "$esn"; done < "$TMPD/found"
      echo "Work on one with:  ./jamesos.sh use NAME   (like: ./jamesos.sh use 1.0)"
      if [[ -s "$TMPD/missing" ]]; then
        echo "Not found right now (off or asleep?) - kept in the list:"
        sed 's/^/  /' "$TMPD/missing"
        echo "  Old entry you don't need? ./jamesos.sh robots remove IP"
      fi
    fi
    rm -rf "$TMPD"
    exit 0
    ;;

  robots)
    set +e
    mkdir -p jamesos; touch "$ROBOTS"
    case "$2" in
      add)
        [[ "$3" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Usage: ./jamesos.sh robots add 192.168.0.111 [name]"; exit 1; }
        grep -v "^$3\( \|$\)" "$ROBOTS" > "$ROBOTS.tmp"; mv "$ROBOTS.tmp" "$ROBOTS"
        echo "$3${4:+ $4}" >> "$ROBOTS"; echo "Added $3${4:+ ($4)}" ;;
      remove)
        grep -v "^$3\( \|$\)" "$ROBOTS" > "$ROBOTS.tmp"; mv "$ROBOTS.tmp" "$ROBOTS"; echo "Removed $3" ;;
      *)
        echo "Published version: $(cat "$UPD/version.txt" 2>/dev/null || echo 'none yet')"
        for ip in $(robot_list | awk '{print $1}'); do
          v=$(rssh "$ip" "$RB version 2>/dev/null || echo 'no updater yet'" 2>/dev/null) || v="offline"
          printf '  %-16s %-14s %s\n' "$ip" "$(robot_name "$ip")" "$v"
        done ;;
    esac
    ;;

  ota)
    # Build a JamesOS OTA: the WireOS builder (~/wire-os) + your saved JamesOS code, private extras OFF,
    # with the JamesOS startup picture, boot animation, welcome message and version built in.
    WOS="${WIREOS_DIR:-$HOME/wire-os}"
    VB="$WOS/anki/victor"
    [[ -e "$VB/.git" && -x "$WOS/build/build.sh" ]] || { echo "No WireOS builder in $WOS. Get it first: git clone https://github.com/os-vector/wire-os ~/wire-os --recurse-submodules"; exit 1; }
    if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
      echo "You have JamesOS changes that aren't saved to git yet (the OTA only uses saved code):"
      git status --short --untracked-files=no | head -10
      echo "Save them first:  git add -A && git commit -m \"JamesOS $JAMESOS_RELEASE\""
      exit 1
    fi
    docker info >/dev/null 2>&1 || { echo "Docker isn't running. Start it:  sudo service docker start   then run this again."; exit 1; }
    NUMF=jamesos/ota-number
    N="${2:-$(( $(cat "$NUMF" 2>/dev/null || echo 100) + 1 ))}"
    [[ "$N" =~ ^[0-9]+$ ]] || { echo "Usage: ./jamesos.sh ota   (or: ./jamesos.sh ota NUMBER)"; exit 1; }
    PIC="jamesos-features/splash/splash_${JAMESOS_SPLASH:-vchevron}.raw"
    [[ "${JAMESOS_SPLASH:-vchevron}" != pvt_* && -f "$PIC" ]] || { echo "Startup picture $PIC can't go in an OTA"; exit 1; }
    for f in jamesos-features/bootanim/wakeup/boot_184x96.raw jamesos-features/bootanim/wakeup/boot_160x80.raw; do
      [[ -f "$f" ]] || { echo "$f is missing"; exit 1; }
    done

    BASE=$(git -C "$WOS" ls-tree HEAD anki/victor | awk '{print $3}')
    [[ -n "$BASE" ]] || { echo "Couldn't tell which WireOS version the builder uses."; exit 1; }
    echo "== 1/5 Mixing JamesOS ($(git log -1 --format=%h)) into WireOS (${BASE:0:8}) =="
    git -C "$VB" cat-file -e "$BASE^{commit}" 2>/dev/null || git -C "$VB" fetch -q origin
    git -C "$VB" checkout -q -f -B jamesos-ota "$BASE"
    git -C "$VB" fetch -q "$PWD" HEAD
    if ! git -C "$VB" merge -q --no-edit -m "JamesOS $JAMESOS_RELEASE for OTA" FETCH_HEAD; then
      BOTH=$(git -C "$VB" diff --name-only --diff-filter=U)
      echo "   JamesOS and WireOS both changed: $(echo $BOTH)"
      echo "   -> using the whole JamesOS file for those (the same file that works on your robots)"
      for f in $BOTH; do
        git -C "$VB" checkout -q --theirs -- "$f" && git -C "$VB" add -- "$f" || { git -C "$VB" merge --abort; echo "Couldn't use the JamesOS version of $f. Nothing was built. Send Claude a screenshot."; exit 1; }
      done
      git -C "$VB" -c core.editor=true commit -q --no-edit || { git -C "$VB" merge --abort; echo "Couldn't finish mixing JamesOS into WireOS. Nothing was built. Send Claude a screenshot."; exit 1; }
    fi

    echo "== 2/5 Private extras OFF =="
    DR="$VB/cannedAnimLib/proceduralFace/proceduralFaceDrawer.cpp"
    sed -i 's/^#define JAMESOS_PRIVATE 1/#define JAMESOS_PRIVATE 0/' "$DR"
    if ! grep -q '^#define JAMESOS_PRIVATE 0' "$DR" || grep -q '^#define JAMESOS_PRIVATE 1' "$DR"; then
      echo "Couldn't turn the private extras off - stopping, nothing was built."; exit 1
    fi

    # JamesOS updates come from your server (jamesos/ota-host.txt), never from WireOS's server,
    # and a robot never installs an older version. No server set up yet = auto-update off.
    UCF="$VB/platform/config/CMakeLists.txt"
    UENV="$VB/platform/config/development/etc-template/update-engine.env.in"
    OTAHOST=$(cat jamesos/ota-host.txt 2>/dev/null)
    grep -q 'https://ota.pvic.xyz/vic/' "$UCF" || { echo "Couldn't find WireOS's update server setting - stopping, nothing was built. Send Claude a screenshot."; exit 1; }
    if [[ "$OTAHOST" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
      sed -i "s#https://ota.pvic.xyz/vic/#http://$OTAHOST/vic/#g" "$UCF"
      echo "   Updates from: http://$OTAHOST/vic/ (your server)"
    else
      sed -i 's/^UPDATE_ENGINE_ENABLED=True/UPDATE_ENGINE_ENABLED=False/' "$UENV"
      echo "   No JamesOS server set up - auto-update OFF in this OTA"
    fi
    sed -i 's/^UPDATE_ENGINE_ALLOW_DOWNGRADE=True/UPDATE_ENGINE_ALLOW_DOWNGRADE=False/' "$UENV"
    if grep -q 'ota.pvic.xyz' "$UCF"; then echo "WireOS's update server is still set - stopping, nothing was built."; exit 1; fi

    echo "== 3/5 Version $JAMESOS_RELEASE, boot animation, startup picture, welcome message, JamesOS keep =="
    INF="$VB/animProcess/src/cozmoAnim/faceDisplay/faceInfoScreenManager.cpp"
    sed -i "s/return \"JamesOS v1\.0\";/return \"JamesOS $JAMESOS_RELEASE\";/" "$INF"
    sed -i "s/std::string(\"V1\.0\")/std::string(\"${JAMESOS_RELEASE^^}\")/" "$INF"   # CONF -> ABOUT JAMESOS
    grep -q "return \"JamesOS $JAMESOS_RELEASE\";" "$INF" || echo "   (couldn't set the version name on the info screen - it may say v1.0)"
    grep -q "std::string(\"${JAMESOS_RELEASE^^}\")" "$INF" || echo "   (couldn't set the version on the ABOUT JAMESOS page - it may say V1.0)"
    AN="$VB/resources/config/engine/animations"
    cp jamesos-features/bootanim/wakeup/boot_184x96.raw "$AN/boot_anim.raw"
    cp jamesos-features/bootanim/wakeup/boot_160x80.raw "$AN/boot_anim_20.raw"
    python3 - "$PIC" "$WOS/anki/rampost/anki_dev_unit.h" <<'HDR' || { echo "Couldn't make the startup picture - stopping"; exit 1; }
import sys
data = open(sys.argv[1], "rb").read()
assert len(data) == 35328, "picture must be 184x96"
rows = ",\n".join("  " + ", ".join("0x%02x" % b for b in data[i:i + 12]) for i in range(0, len(data), 12))
open(sys.argv[2], "w").write("#ifndef __ANKI_PROPRIETARY_IMAGE_H_\n#define __ANKI_PROPRIETARY_IMAGE_H_\n\n"
    "// JamesOS startup picture\nunsigned char anki_dev_unit[] = {\n" + rows + "\n};\n"
    "unsigned int anki_dev_unit_len = 35328;\n\n#endif\n")
HDR
    MO="$WOS/poky/victor/meta-anki/recipes-helpful/vector-motd/vector-motd/vector-motd"
    if [[ -f "$MO" ]] && grep -q '^VERSION=' "$MO"; then
      motd_line > /tmp/jamesos-motd.$$
      sed -i '/# JamesOS$/d' "$MO"
      sed -i "/^VERSION=/r /tmp/jamesos-motd.$$" "$MO"
      rm -f /tmp/jamesos-motd.$$
    else
      echo "   (couldn't find the welcome message file - it will say WireOS)"
    fi
    # JamesOS keep inside the OTA: the same startup service your PC installs (puts the JamesOS lights,
    # sparkles and galaxy eyes in place on a fresh robot / after CLEAR OUT SOUL, and keeps the badges)
    KR="$WOS/poky/victor/meta-anki/recipes/jamesos-keep"
    IMG="$WOS/poky/victor/meta-anki/recipes-products/images/apq8009/apq8009-anki-robot-image.inc"
    if [[ -f "$IMG" ]]; then
      mkdir -p "$KR/files"
      cp "$KSRC/jamesos-keep.service" "$KR/files/jamesos-keep.service"
      cat > "$KR/jamesos-keep.bb" <<'RECIPE'
SUMMARY = "JamesOS keep: puts JamesOS settings back after Clear Out Soul and keeps the badges"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://jamesos-keep.service"
S = "${UNPACKDIR}"
do_install() {
    install -d ${D}${sysconfdir}/systemd/system/multi-user.target.wants
    install -m 0644 ${UNPACKDIR}/jamesos-keep.service ${D}${sysconfdir}/systemd/system/jamesos-keep.service
    ln -sf ../jamesos-keep.service ${D}${sysconfdir}/systemd/system/multi-user.target.wants/jamesos-keep.service
}
FILES:${PN} = "${sysconfdir}/systemd/system"
RECIPE
      # (BitBake doesn't allow a comment at the end of a line, so the comment gets its own line)
      sed -i '/jamesos-keep/d; /^# JamesOS keep$/d' "$IMG"
      printf '# JamesOS keep\nIMAGE_INSTALL += "jamesos-keep"\n' >> "$IMG"
    else
      echo "   (couldn't find the OTA image list - the keep service won't be inside)"
    fi
    # WireOS puts /etc/do-not-auto-update in the image, which blocks ALL auto-updates.
    # JamesOS takes it out (robots only ask YOUR server - see "updates come from your server" above).
    if [[ -f "$IMG" ]]; then
      sed -i '/^# JamesOS auto-update$/,/^}$/d' "$IMG"
      printf '# JamesOS auto-update\nROOTFS_POSTPROCESS_COMMAND:append = " jamesos_allow_updates; "\njamesos_allow_updates() {\n    rm -f ${IMAGE_ROOTFS}/etc/do-not-auto-update\n}\n' >> "$IMG"
      echo "   Auto-update allowed (from your server only)"
    else
      echo "   (couldn't find the OTA image list - auto-update stays blocked)"
    fi
    git -C "$VB" commit -qa -m "OTA: private off, JamesOS $JAMESOS_RELEASE boot animation + version" || true
    # the builder doesn't notice a changed picture inside rampost by itself: ask it to rebuild these
    echo "rampost vector-motd jamesos-keep" > "$WOS/wire-cleaning"

    echo "== 4/5 Building the OTA (vicos-*.${N}d) - about 20-60 minutes. Keep the charger in! =="
    ( cd "$WOS" && ./build/build.sh -bt dev -v "$N" ) || { echo "The build stopped with an error. Send Claude a screenshot of the red lines."; exit 1; }
    OTA=$(ls -t "$WOS"/_build/vicos-*."$N"d.ota 2>/dev/null | head -1)
    [[ -f "$OTA" ]] || { echo "The build finished but no .ota file was made - send Claude a screenshot."; exit 1; }
    mkdir -p jamesos; echo "$N" > "$NUMF"
    RPB=$(find "$WOS/poky/build/tmp-glibc/work" -path '*rampost*/image/usr/bin/rampost' 2>/dev/null | head -1)
    if [[ -n "$RPB" ]] && python3 -c "import sys; sys.exit(0 if open(sys.argv[2],'rb').read() in open(sys.argv[1],'rb').read() else 1)" "$RPB" "$PIC"; then
      echo "Startup picture inside the OTA: ${JAMESOS_SPLASH:-vchevron} (checked)"
    else
      echo "(couldn't confirm the startup picture inside the OTA - check it on the robot)"
    fi

    echo "== 5/5 Done =="
    echo "OTA: $OTA ($(du -h "$OTA" | cut -f1))"
    echo "Test it on ONE robot (never the PVT):  ./jamesos.sh ota-install 2.0"
    done_msg
    ;;

  ota-install)
    # Install an OTA on a robot over SSH: your PC shares the file through the SSH connection,
    # so no firewall or extra window is needed. Never on the PVT.
    WOS="${WIREOS_DIR:-$HOME/wire-os}"
    T="${2:-$IP}"
    if [[ ! "$T" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
      NAME="$T"; T=$(awk -v n="$NAME" '$2==n {print $1; exit}' "$ROBOTS" 2>/dev/null)
      [[ -n "$T" ]] || { echo "No robot called '$NAME'. Run: ./jamesos.sh find"; exit 1; }
    else
      NAME=$(awk -v i="$T" '$1==i {print $2; exit}' "$ROBOTS" 2>/dev/null)
    fi
    OTA="${3:-$(ls -t "$WOS"/_build/vicos-*.ota 2>/dev/null | head -1)}"
    [[ -f "$OTA" ]] || { echo "No OTA file found. Build one first: ./jamesos.sh ota"; exit 1; }
    case "$NAME" in [Pp][Vv][Tt]*) echo "$NAME is the PVT - no OTAs on the PVT. Nothing was done."; exit 1 ;; esac
    INFO=$(rssh "$T" '[ -f /data/data/jamesos/prototype ] && echo PROTO; getprop ro.anki.version 2>/dev/null || cat /anki/etc/version 2>/dev/null; hostname') \
      || { echo "Can't reach the robot at $T. Is it on? (IPs change: ./jamesos.sh find)"; exit 1; }
    if grep -q PROTO <<< "$INFO"; then echo "This robot is marked as a prototype (the PVT) - no OTAs on it. Nothing was done."; exit 1; fi
    WANT=$(basename "$OTA" .ota); WANT=${WANT#vicos-}
    echo "Robot: ${NAME:-?} at $T ($(echo $INFO | tr '\n' ' '))"
    echo "OTA:   $(basename "$OTA") -> version $WANT"
    echo "He must be ON THE CHARGER. Don't touch him until he has restarted (5-10 minutes)."
    read -p "Install it now? (type yes): " yn
    [[ "$yn" == yes ]] || { echo "Nothing was done."; exit 0; }
    PORT=8771
    python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$(dirname "$OTA")" >/dev/null 2>&1 &
    HTTPPID=$!
    trap 'kill $HTTPPID 2>/dev/null' EXIT
    sleep 1
    echo "== Sending the OTA (his eyes go dark, then he restarts by himself) =="
    ssh -i "$KEY" -o StrictHostKeyChecking=no -o LogLevel=ERROR -o ConnectTimeout=6 -o ExitOnForwardFailure=yes \
        -R "$PORT:127.0.0.1:$PORT" "root@$T" "update-os http://localhost:$PORT/$(basename "$OTA")" || true
    kill $HTTPPID 2>/dev/null; trap - EXIT
    echo "== Waiting for him to come back (up to 10 minutes) =="
    sleep 30
    HAVE=""
    for i in $(seq 57); do
      HAVE=$(rssh "$T" "getprop ro.anki.version 2>/dev/null || cat /anki/etc/version" 2>/dev/null | tr -d ' \r\n') || HAVE=""
      if [[ -n "$HAVE" && "$HAVE" == *"$WANT"* ]]; then echo "He's back on $HAVE - the OTA is installed!"; done_msg; exit 0; fi
      sleep 10
    done
    if [[ -n "$HAVE" ]]; then
      echo "He's on, but still on $HAVE (not $WANT) - the OTA didn't install. Send Claude a screenshot."
    else
      echo "He hasn't come back on $T yet. Look at his screen; if he's on, his IP may have changed: ./jamesos.sh find"
    fi
    exit 1
    ;;

  setup-site)
    # JamesOS Setup: Digital Dream Labs' Vector Web Setup (MIT) with JamesOS text and colors, serving
    # your newest JamesOS OTA. Vector (in recovery mode) downloads it from this laptop over Wi-Fi.
    WOS="${WIREOS_DIR:-$HOME/wire-os}"
    SD="${JAMESOS_SETUP_DIR:-$HOME/jamesos-setup}"
    command -v node >/dev/null && command -v npm >/dev/null || { echo "JamesOS Setup needs Node.js once. Install it:  sudo apt install -y nodejs npm   then run this again."; exit 1; }
    if [[ "$2" == online ]]; then
      # ./jamesos.sh setup-site online <OTA link>: the online setup site (GitHub Pages), no laptop needed
      URL="$3"; OUT="${JAMESOS_SITE_DIR:-$HOME/jamesos-site}"
      [[ "$URL" =~ ^https?://.+\.ota$ ]] || { echo "Usage: ./jamesos.sh setup-site online https://github.com/ORG/setup/releases/download/TAG/FILE.ota"; exit 1; }
      [[ -e "$OUT/.git" ]] || { echo "First get your public site repo once:  git clone https://github.com/ORG/setup ~/jamesos-site"; exit 1; }
      [[ -f "$SD/site/js/rts.js" ]] || { echo "Run ./jamesos.sh setup-site once first (it builds the site), then Ctrl+C and run this again."; exit 1; }
      echo "== Making the online JamesOS Setup =="
      python3 jamesos-features/setup-site/patch_vws.py "$SD" || exit 1
      ( cd "$SD" && node vector-web-setup.js configure --reset-setting >/dev/null ) || { echo "configure failed - send Claude a screenshot"; exit 1; }
      python3 jamesos-features/setup-site/build_online.py "$SD" "$OUT" "$URL" || exit 1
      echo "== Putting it online =="
      git -C "$OUT" add -A
      git -C "$OUT" commit -qm "JamesOS Setup: $(basename "$URL")" || echo "(nothing changed)"
      git -C "$OUT" push -q || { echo "Push failed - send Claude a screenshot"; exit 1; }
      ORG=$(git -C "$OUT" remote get-url origin | sed -E 's#.*github.com[:/]([^/]+)/([^/.]+).*#\1#')
      REPO=$(git -C "$OUT" remote get-url origin | sed -E 's#.*github.com[:/]([^/]+)/([^/.]+).*#\2#')
      echo "Done. The site updates in about a minute at:  https://$ORG.github.io/$REPO/"
      echo "(First time only: on GitHub open the repo -> Settings -> Pages -> Deploy from a branch -> main, / (root) -> Save)"
      exit 0
    fi
    OTA=$(ls -t "$WOS"/_build/vicos-*.ota 2>/dev/null | head -1)
    [[ -f "$OTA" ]] || { echo "No OTA file yet. Build one first: ./jamesos.sh ota"; exit 1; }
    NET=$(cut -d. -f1-3 robot_ip.txt 2>/dev/null)
    LAN=$(ip -4 -o addr show 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | grep "^$NET\." | head -1)
    if [[ -z "$LAN" ]]; then
      echo "Your robots can't reach this laptop yet: Ubuntu isn't on your home network ($NET.x)."
      echo "Do this once (then run ./jamesos.sh setup-site again):"
      echo "  1. In Ubuntu:  grep -q networkingMode /mnt/c/Users/*/.wslconfig || printf '\\nnetworkingMode=mirrored\\n' >> /mnt/c/Users/ADMIN/.wslconfig"
      echo "  2. In PowerShell (Run as administrator):"
      echo "     Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow"
      echo "  3. In Windows cmd:  wsl --shutdown   then open Ubuntu again"
      exit 1
    fi
    if [[ ! -d "$SD/.git" ]]; then
      echo "== Getting Vector Web Setup (first time only) =="
      git clone -q https://github.com/digital-dream-labs/vector-web-setup "$SD" || exit 1
    fi
    if [[ ! -d "$SD/node_modules" ]]; then
      echo "== Installing its parts (first time only, about a minute) =="
      ( cd "$SD" && npm install --no-audit --no-fund ) || { echo "npm install failed - send Claude a screenshot"; exit 1; }
    fi
    echo "== Making it JamesOS Setup =="
    python3 jamesos-features/setup-site/patch_vws.py "$SD" || exit 1
    ( cd "$SD" && node vector-web-setup.js configure --reset-setting >/dev/null ) || { echo "configure failed - send Claude a screenshot"; exit 1; }
    FW="$SD/site/firmware/jamesos"
    mkdir -p "$FW"; rm -f "$FW"/*.ota
    NAME="jamesos-$(basename "$OTA" .ota | sed 's/^vicos-//').ota"
    ln -s "$OTA" "$FW/$NAME"
    PORT=8000
    ( cd "$SD" && JAMESOS_LAN_IP="$LAN" exec node vector-web-setup.js serve -p $PORT ) >/tmp/jamesos-setup.log 2>&1 &
    SPID=$!
    trap 'kill $SPID 2>/dev/null' EXIT INT TERM
    sleep 2
    kill -0 $SPID 2>/dev/null || { cat /tmp/jamesos-setup.log; echo "The setup site didn't start - send Claude a screenshot"; exit 1; }
    echo
    echo "JamesOS Setup is running. It installs: $NAME"
    echo "  Open Chrome on Windows:  http://localhost:$PORT"
    echo "  Vector downloads JamesOS from: http://$LAN:$PORT"
    # can a robot reach the laptop? (a robot, if it is on and on Wi-Fi)
    if R=$(rssh "$IP" "curl -s -o /dev/null -w '%{http_code}' --max-time 5 http://$LAN:$PORT/static/data/settings.json" 2>/dev/null); then
      if [[ "$R" == 200 ]]; then echo "  Checked: the robot at $IP can reach this laptop. Good."
      else
        echo "  WARNING: the robot at $IP can't reach this laptop (got '$R'). Windows Firewall is probably blocking it."
        echo "  Fix it once in PowerShell (Run as administrator), then try again:"
        echo "    Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow"
      fi
    fi
    echo
    echo "Leave this window open while you use it. Press Ctrl+C to stop it."
    wait $SPID
    ;;

  splash)
    # The first picture at startup is drawn by /usr/bin/rampost (a WireOS picture built into it).
    # We swap only that picture inside the program; the WireOS original is kept as rampost.wireos-backup.
    RP=/usr/bin/rampost
    BK=/usr/bin/rampost.wireos-backup
    ROOTRW='W=0; awk '"'"'$2=="/" && $4 ~ /^ro/ {f=1} END {exit !f}'"'"' /proc/mounts && W=1 && mount -o remount,rw /;'
    ROOTRO='sync; [ "$W" = 1 ] && mount -o remount,ro /; true'
    if [[ "$2" == "off" ]]; then
      $SSH "$ROOTRW rm -f /etc/jamesos/splash_offset /etc/jamesos/splash_current; if [ -f $BK ]; then cp $BK $RP.tmp && chmod 755 $RP.tmp && mv $RP.tmp $RP && echo 'WireOS startup picture is back'; else echo 'Nothing to undo'; fi; $ROOTRO"
      done_msg
      exit 0
    fi
    if [[ "$2" == "seasonal" ]]; then
      # JamesOS seasonal startup picture: today's picture now, then the robot swaps it by itself
      # when the season or holiday changes (jamesos-features/splash/seasonal_splash.sh).
      SP=resources/config/engine/animations/jamesos_seasonal/splash
      [[ -d "$SP" ]] || { echo "Seasonal pictures not found - use the seasonal-boots branch: git checkout seasonal-boots"; exit 1; }
      echo "== Asking $IP which picture fits today =="
      TODAY=$($SSH "/anki/bin/vic-bootAnim --splash" 2>/dev/null | tr -d '\r' | tail -n 1)
      [[ "$TODAY" == */splash/*.raw ]] || { echo "Vector didn't answer. Deploy the seasonal build first (./jamesos.sh go), then try again."; exit 1; }
      PIC="$SP/$(basename "$TODAY")"
      [[ -f "$PIC" ]] || { echo "Missing $PIC"; exit 1; }
      TMP=$(mktemp -d)
      echo "== Reading the startup program from $IP =="
      $SSH "if [ -f $BK ]; then cat $BK; else cat $RP; fi" > "$TMP/rampost.orig" || { echo "Couldn't read it."; rm -rf "$TMP"; exit 1; }
      OFFSET=""
      for OLD in jamesos-features/splash/wireos_splash.raw jamesos-features/splash/splash_*.raw; do
        OUT=$(python3 jamesos-features/splash/patch_rampost.py "$TMP/rampost.orig" "$OLD" "$PIC" "$TMP/rampost.new" 2>/dev/null) && { OFFSET=$(echo "$OUT" | grep -o '[0-9]\+' | head -n 1); break; }
      done
      [[ -n "$OFFSET" ]] || { echo "Couldn't find the old startup picture inside rampost - send Claude a screenshot"; rm -rf "$TMP"; exit 1; }
      echo "$OFFSET" > "$TMP/splash_offset"; echo "$TODAY" > "$TMP/splash_current"
      SUM=$(md5sum < "$TMP/rampost.new" | cut -d' ' -f1)
      $SSH "cat > /data/rampost.new" < "$TMP/rampost.new"
      $SSH "cat > /data/seasonal_splash.sh" < jamesos-features/splash/seasonal_splash.sh
      $SSH "cat > /data/splash_offset" < "$TMP/splash_offset"
      $SSH "cat > /data/splash_current" < "$TMP/splash_current"
      rm -rf "$TMP"
      $SSH "[ \"\$(md5sum < /data/rampost.new | cut -d' ' -f1)\" = $SUM ]" || { echo "Upload was broken, nothing changed"; $SSH "rm -f /data/rampost.new /data/seasonal_splash.sh /data/splash_offset /data/splash_current"; exit 1; }
      echo "== Installing the seasonal startup picture =="
      RES=$($SSH "sh -s" < jamesos-features/splash/install_seasonal_on_robot.sh | tail -n 1)
      if [[ "$RES" == OK ]]; then
        echo "Startup picture: seasonal (today: $(basename "$TODAY" .raw)). It changes by itself with the seasons."
        echo "You'll see it on the next reboot (./jamesos.sh ssh, then: reboot)."
      else
        echo "Something went wrong: $RES - send Claude a screenshot"
      fi
      done_msg
      exit 0
    fi
    PIC="jamesos-features/splash/splash_${2:-sparkle}.raw"
    if [[ "$2" == pvt_* && ! -f jamesos/private.on ]]; then
      echo "The PVT startup pictures are a private extra. Turn it on first: ./jamesos.sh private on"; exit 1
    fi
    [[ -f "$PIC" ]] || { echo "Usage: ./jamesos.sh splash sparkle|sleeping|badge|seasonal|off   (PVT: pvt_goldsparkle|pvt_goldname|pvt_angkor|pvt_starsky|pvt_anvil|pvt_gear|pvt_crest)"; exit 1; }
    TMP=$(mktemp -d)
    echo "== Reading the startup program from $IP =="
    $SSH "if [ -f $BK ]; then cat $BK; else cat $RP; fi" > "$TMP/rampost.orig" || { echo "Couldn't read it."; exit 1; }
    # the picture inside rampost now: WireOS's, or JamesOS sparkle (robots that got a JamesOS OTA)
    OK=0
    for OLD in jamesos-features/splash/wireos_splash.raw jamesos-features/splash/splash_sparkle.raw; do
      python3 jamesos-features/splash/patch_rampost.py "$TMP/rampost.orig" "$OLD" "$PIC" "$TMP/rampost.new" 2>/dev/null && { OK=1; break; }
    done
    [[ $OK = 1 ]] || { echo "Couldn't find the old startup picture inside rampost - send Claude a screenshot"; rm -rf "$TMP"; exit 1; }
    SUM=$(md5sum < "$TMP/rampost.new" | cut -d' ' -f1)
    $SSH "cat > /data/rampost.new" < "$TMP/rampost.new"
    rm -rf "$TMP"
    echo "== Installing the new startup picture =="
    $SSH "[ \"\$(md5sum < /data/rampost.new | cut -d' ' -f1)\" = $SUM ] || { echo 'Upload was broken, nothing changed'; rm -f /data/rampost.new; exit 1; }
          $ROOTRW rm -f /etc/jamesos/splash_offset /etc/jamesos/splash_current; [ -f $BK ] || cp -p $RP $BK; cp /data/rampost.new $RP.tmp && chmod 755 $RP.tmp && mv $RP.tmp $RP; rm -f /data/rampost.new; $ROOTRO" \
      && echo "Startup picture: ${2:-sparkle}. You'll see it on the next reboot (./jamesos.sh ssh, then: reboot)."
    done_msg
    ;;

  *)
    sed -n '2,/^# Put this file/p' "$0"
    ;;
esac
