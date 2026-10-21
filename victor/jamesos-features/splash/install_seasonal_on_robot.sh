# Runs ON the robot (sent by "./jamesos.sh splash seasonal"). Needs /data/rampost.new,
# /data/seasonal_splash.sh and /data/splash_offset already uploaded.
RP=/usr/bin/rampost; BK=/usr/bin/rampost.wireos-backup; INIT=/usr/bin/rampost-init.sh
HOOK='[ -f /etc/jamesos/seasonal_splash.sh ] && . /etc/jamesos/seasonal_splash.sh'
W=0; awk '$2=="/" && $4 ~ /^ro/ {f=1} END {exit !f}' /proc/mounts && W=1 && mount -o remount,rw /
[ -f $BK ] || cp -p $RP $BK
mkdir -p /etc/jamesos
cp /data/seasonal_splash.sh /etc/jamesos/seasonal_splash.sh
cp /data/splash_offset /etc/jamesos/splash_offset
cat /data/splash_current > /etc/jamesos/splash_current
cp /data/rampost.new $RP.tmp && chmod 755 $RP.tmp && mv $RP.tmp $RP
if ! grep -q "seasonal_splash.sh" $INIT; then
  cp -p $INIT /etc/jamesos/rampost-init.sh.backup
  awk -v hook="$HOOK" '/^rampost -d/ && !done {print "# JamesOS: seasonal startup picture"; print hook; done=1} {print}' $INIT > $INIT.tmp \
    && grep -q "seasonal_splash.sh" $INIT.tmp && chmod 755 $INIT.tmp && mv $INIT.tmp $INIT
fi
rm -f /data/rampost.new /data/seasonal_splash.sh /data/splash_offset /data/splash_current
sync; [ "$W" = 1 ] && mount -o remount,ro /
grep -q "seasonal_splash.sh" $INIT && echo "OK" || echo "rampost-init.sh was not changed"
