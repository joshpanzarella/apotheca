# Said at every login shell on USB serial or UART. Not on the screen's own
# shell (tty1): during a start that shell shares the screen with the
# start-up picture, and no words go over it. (inittab starts no shell there
# since 2026-11-28; the test stays, should one come back.)
if [ -z "$APOTHECA_SAID" ] && [ "$(tty 2>/dev/null)" != "/dev/tty1" ]; then
    export APOTHECA_SAID=1
    echo
    echo "Apotheca system, batch 4: Apotheca runs on the screen; the power key and battery are watched."
    echo "  model:     $(tr -d '\0' < /proc/device-tree/model 2>/dev/null)"
    echo "  kernel:    $(uname -r)"
    echo "  boot log:  /data/apotheca/logs/boot-latest.txt"
    echo "  apotheca:  /data/apotheca (its log: apotheca.log); hold restarts: touch /run/apotheca/apotheca.hold"
    echo "  power:     /data/apotheca/logs/power.log; settings in /data/apotheca/power.txt"
    echo "  try:       /usr/libexec/apotheca/padprobe (buttons), speaker-test -t sine -l 1 (sound), poweroff"
    echo
fi
