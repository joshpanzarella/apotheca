# Said at every login shell on the screen, USB serial or UART.
if [ -z "$APOTHECA_SAID" ]; then
    export APOTHECA_SAID=1
    echo
    echo "Apotheca system, batch 1: a console only."
    echo "  model:     $(tr -d '\0' < /proc/device-tree/model 2>/dev/null)"
    echo "  kernel:    $(uname -r)"
    echo "  boot log:  /data/apotheca/logs/boot-latest.txt"
    echo "  try:       evtest (buttons), speaker-test -t sine -l 1 (sound), poweroff"
    echo
fi
