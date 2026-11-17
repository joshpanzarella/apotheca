#!/usr/bin/env python3
"""power-test.py [APOTHECA-POWER]

The desk test of apotheca-power (os/package/apotheca-tools/src/apotheca-power.c),
the system's power key and battery watcher: built with the desk's own C
compiler (or the program given), run against a made-up /sys (power supplies,
a backlight, input devices) and made-up input devices (FIFOs fed with the
kernel's input_event records), with its "off" a command that writes a file.

  devices      it opens the power key and the joypad, not the volume keys,
               and finds a device that appears later
  short press  the backlight off (bl_power 4), and on again with the next
  long press   held 2 s: off, with the key still down; the backlight off
               first; letting go then does nothing more
  battery      at 5% on the battery: off after two reads, not one; on a
               charger (a supply online, or Charging / Not charging): never;
               a low read that recovers: never; under 3.25 V at 30%: off; a
               voltage in millivolts (a test supply's): not read; the config's
               low-battery 0: never, 10: off at 8%
  sleep        power-key sleep: a short press writes "mem" to power/state
  no folder    with no /dev/input at the start, it and its first device are
               found when they appear
  stop         TERM: it says so and ends
  screen       a device tree not yet seen working: off after screen-check
               seconds with no button; a button keeps it as seen and makes
               the green light steady; another tree is checked again; no
               backlight or no display in /sys: off; none of it where the
               tree does not name itself (qemu)

What it cannot show: EVIOCGRAB (a FIFO cannot be held; the qemu test holds a
real uinput joypad), and the real AXP717 (only the handheld).
Exit code 0 when every check passes.
"""
import os, shutil, signal, struct, subprocess, sys, tempfile, time

fails = []
KEY_POWER, KEY_VOLUMEUP, KEY_VOLUMEDOWN, BTN_SOUTH, BTN_DPAD_UP = 116, 115, 114, 0x130, 0x220
EV_KEY, EV_SYN = 1, 0


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what, flush=True)
    if not ok:
        fails.append(what)


def bitmap(bits):
    """A sysfs capabilities/key line: hex words (64-bit), most significant first."""
    words = [0] * (max(bits) // 64 + 1)
    for b in bits:
        words[b // 64] |= 1 << (b % 64)
    return " ".join("%x" % w for w in reversed(words))


class Rig:
    def __init__(self, program, root):
        self.program = program
        self.root = root
        self.sys = os.path.join(root, "sys")
        self.dev = os.path.join(root, "dev")
        self.log = os.path.join(root, "power.log")
        self.offs = os.path.join(root, "offs")
        self.config = os.path.join(root, "power.txt")
        self.tree = os.path.join(root, "rocknix-dt-id")   # absent: not the handheld, no screen check
        self.seen = os.path.join(root, "screen-seen")
        self.find = 60
        os.makedirs(self.dev)
        os.makedirs(os.path.join(self.sys, "power"))
        open(os.path.join(self.sys, "power", "state"), "w").write("freeze mem\n")
        bl = os.path.join(self.sys, "class", "backlight", "backlight")
        os.makedirs(bl)
        self.bl = os.path.join(bl, "bl_power")
        open(self.bl, "w").write("4\n")  # as if an earlier run left it off
        self.fifos = {}
        self.p = None

    def device(self, n, name, keys):
        d = os.path.join(self.sys, "class", "input", "event%d" % n, "device")
        os.makedirs(os.path.join(d, "capabilities"))
        open(os.path.join(d, "name"), "w").write(name + "\n")
        open(os.path.join(d, "capabilities", "key"), "w").write(bitmap(keys) + "\n")
        os.makedirs(self.dev, exist_ok=True)
        path = os.path.join(self.dev, "event%d" % n)
        os.mkfifo(path)
        # Read and write, so it never blocks and never reads as closed.
        self.fifos[n] = os.open(path, os.O_RDWR | os.O_NONBLOCK)

    def supply(self, name, **files):
        d = os.path.join(self.sys, "class", "power_supply", name)
        os.makedirs(d, exist_ok=True)
        for k, v in files.items():
            open(os.path.join(d, k), "w").write("%s\n" % v)

    def start(self, poll=1):
        open(self.log, "w").close()
        if os.path.exists(self.offs):
            os.remove(self.offs)
        self.p = subprocess.Popen(
            [self.program, "--sys", self.sys, "--dev", self.dev, "--config", self.config, "--log", self.log,
             "--off", "echo off >> %s" % self.offs, "--poll", str(poll), "--console", os.devnull,
             "--tree", self.tree, "--seen", self.seen, "--find-screen-s", str(self.find)],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)

    def stop(self):
        self.p.send_signal(signal.SIGTERM)
        try:
            code = self.p.wait(5)
        except subprocess.TimeoutExpired:
            self.p.kill()
            code = None
        return code

    def key(self, n, code, value):
        os.write(self.fifos[n], struct.pack("llHHi", 0, 0, EV_KEY, code, value) + struct.pack("llHHi", 0, 0, EV_SYN, 0, 0))

    def press(self, seconds):
        self.key(0, KEY_POWER, 1)
        time.sleep(seconds)
        self.key(0, KEY_POWER, 0)
        time.sleep(0.3)

    def logged(self):
        return open(self.log).read()

    def offCount(self):
        return len(open(self.offs).read().split()) if os.path.exists(self.offs) else 0

    def backlight(self):
        return open(self.bl).read().strip()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    work = tempfile.mkdtemp(prefix="apotheca-power-", dir=os.environ.get("TMPDIR", "/tmp"))
    r = None
    try:
        if args:
            program = os.path.abspath(args[0])
        else:
            program = os.path.join(work, "apotheca-power")
            src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "package", "apotheca-tools", "src",
                               "apotheca-power.c")
            subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", "-o", program, src], check=True)

        r = Rig(program, os.path.join(work, "a"))
        r.device(0, "axp20x-pek", [KEY_POWER])
        r.device(1, "H700 Gamepad", list(range(BTN_SOUTH, BTN_SOUTH + 15)) + [BTN_DPAD_UP, BTN_DPAD_UP + 1])
        r.device(2, "gpio-keys-volume", [KEY_VOLUMEUP, KEY_VOLUMEDOWN])
        r.supply("battery", type="Battery", capacity=50, status="Discharging", voltage_now=3800000)
        r.supply("axp20x-usb", type="USB", online=0)
        r.start()
        log = r.logged()
        check("event0 (axp20x-pek): the power key" in log, "it found the power key")
        check("event1 (H700 Gamepad): a joypad" in log, "it found the joypad")
        check("event2" not in log, "it left the volume keys alone (they are Apotheca's)")
        check("battery 50%, 3.80 V, Discharging, on the battery" in log, "it read the battery")
        check(r.backlight() == "0", "the backlight on at the start, whatever was left (%s)" % r.backlight())

        r.press(0.3)
        check(r.backlight() == "4", "a short press: the backlight off")
        check("a short press" in r.logged() and "the screen off" in r.logged(), "said so")
        r.press(0.3)
        check(r.backlight() == "0", "another short press: the backlight on again")
        check(r.offCount() == 0, "no short press turned it off")

        # A device that appears later
        r.device(3, "a second gamepad", [BTN_SOUTH])
        time.sleep(0.8)
        check("event3 (a second gamepad): a joypad" in r.logged(), "a joypad plugged in later is found")

        # Long press: off while the key is still down
        r.key(0, KEY_POWER, 1)
        time.sleep(1.5)
        check(r.offCount() == 0, "held 1.5 s: not yet")
        time.sleep(0.8)
        check(r.offCount() == 1, "held 2.3 s: off, the key still down")
        check(r.backlight() == "4", "the backlight off as the sign")
        r.key(0, KEY_POWER, 0)
        time.sleep(0.3)
        check(r.offCount() == 1 and r.backlight() == "4", "let go after: nothing more (no short press)")
        check("power key held: turning off" in r.logged(), "said why")
        check(r.stop() == 0 and "stopped" in r.logged(), "TERM: it stops")

        # The battery
        r.supply("battery", capacity=5, status="Discharging", voltage_now=3600000)
        r.supply("axp20x-usb", online=1)
        r.start(poll=1)
        time.sleep(3.5)
        check(r.offCount() == 0, "5% with a charger plugged in (USB online): not off")
        r.supply("axp20x-usb", online=0)
        r.supply("battery", status="Charging")
        time.sleep(2.5)
        check(r.offCount() == 0, "5%, status Charging: not off")
        r.supply("battery", status="Not charging")
        time.sleep(2.5)
        check(r.offCount() == 0, "5%, status Not charging (on the charger, full): not off")
        r.supply("battery", status="Discharging")
        end = time.time() + 3
        while "read 1 of 2" not in r.logged() and time.time() < end:
            time.sleep(0.02)
        r.supply("battery", capacity=6)  # one low read, then not, before the next read (1 s)
        n = r.offCount()
        log = r.logged()
        time.sleep(2.5)
        check(n == 0 and "read 1 of 2" in log, "5%% on the battery: one read is not enough (%d)" % n)
        check(r.offCount() == 0 and "battery no longer low" in r.logged(), "a low read that recovers: not off")
        r.supply("battery", capacity=5)
        time.sleep(3)
        check(r.offCount() == 1 and "battery low (5%): turning off" in r.logged(), "5% twice: off")
        r.stop()

        r.supply("battery", capacity=30, voltage_now=3200000)
        r.start(poll=1)
        time.sleep(3)
        check(r.offCount() == 1, "30% but 3.20 V: off (the voltage backstop under the gauge)")
        r.stop()
        r.supply("battery", capacity=30, voltage_now=3300)  # a test supply's millivolts
        r.start(poll=1)
        time.sleep(3)
        check(r.offCount() == 0, "a voltage that is not a cell's (3300): not read")
        r.stop()

        open(r.config, "w").write("# made up\nlow-battery 0\n")
        r.supply("battery", capacity=2, voltage_now=3700000)
        r.start(poll=1)
        time.sleep(3)
        check(r.offCount() == 0 and "(never)" in r.logged(), "low-battery 0: never off for the battery")
        r.stop()
        open(r.config, "w").write("low-battery 10\n")
        r.supply("battery", capacity=8)
        r.start(poll=1)
        time.sleep(3)
        check(r.offCount() == 1, "low-battery 10: off at 8%")
        r.stop()
        open(r.config, "w").write("low-battery 99\n")  # out of range: the default
        r.start(poll=1)
        time.sleep(3)
        check(r.offCount() == 0 and "off at 5%" in r.logged(), "low-battery 99: not taken, 5% kept")
        r.stop()

        # Sleep, when asked for: a short press writes "mem" to power/state
        open(r.config, "w").write("power-key sleep\n")
        r.supply("battery", capacity=60)
        r.start()
        r.press(0.3)
        state = open(os.path.join(r.sys, "power", "state")).read().strip()
        check(state == "mem" and "awake after" in r.logged(), "power-key sleep: a short press suspends (%r)" % state)
        check(r.backlight() == "0", "and the backlight is on again after")
        check(r.stop() == 0, "stops")

        # No /dev/input at all when it starts (qemu has no input device until a
        # stand-in is made): it is found when it appears, with its first device.
        r = Rig(program, os.path.join(work, "b"))
        os.rmdir(r.dev)
        r.supply("battery", type="Battery", capacity=60, status="Discharging")
        r.start(poll=60)
        r.device(0, "axp20x-pek", [KEY_POWER])
        time.sleep(1)
        check("event0 (axp20x-pek): the power key" in r.logged(),
              "with no input folder at the start: the folder and its first device are found at once")
        r.press(0.3)
        check(r.backlight() == "4", "and its key works")
        check(r.stop() == 0, "stops")

        # The screen: never running behind a dark one (firmware plan, section 15)
        r = Rig(program, os.path.join(work, "c"))
        r.device(0, "axp20x-pek", [KEY_POWER])
        r.device(1, "H700 Gamepad", list(range(BTN_SOUTH, BTN_SOUTH + 15)) + [BTN_DPAD_UP, BTN_DPAD_UP + 1])
        r.supply("battery", type="Battery", capacity=60, status="Discharging")
        os.makedirs(os.path.join(r.sys, "class", "drm", "card0-DSI-1"))
        os.makedirs(os.path.join(r.sys, "class", "drm", "card0"))
        led = os.path.join(r.sys, "class", "leds", "green:power")
        os.makedirs(led)
        open(os.path.join(led, "trigger"), "w").write("heartbeat\n")
        open(os.path.join(led, "brightness"), "w").write("0\n")
        ledNow = lambda: (open(os.path.join(led, "trigger")).read().strip(), open(os.path.join(led, "brightness")).read().strip())
        r.find = 1
        r.start(poll=60)                      # no tree name: qemu, the desk
        time.sleep(2)
        check(r.offCount() == 0 and "the screen:" not in r.logged() and ledNow() == ("heartbeat", "0"),
              "where the device tree does not name itself (qemu): no screen check, the light left alone")
        r.stop()
        open(r.tree, "wb").write(b"sun50i-h700-anbernic-rg40xx-v\0")
        open(r.config, "w").write("screen-check 3\n")
        r.start(poll=60)
        check("the screen: sun50i-h700-anbernic-rg40xx-v, not yet seen working" in r.logged() and ledNow()[0] == "heartbeat",
              "a tree not seen working before: said, the light still blinking")
        time.sleep(1.5)
        check(r.offCount() == 0, "1.5 s of the 3: not yet off")
        time.sleep(2.2)
        check(r.offCount() == 1 and "no button in 3 s with a screen not yet seen working" in r.logged() and r.backlight() == "4",
              "no button in 3 s: off")
        check(not os.path.exists(r.seen), "and nothing is kept as seen")
        r.stop()
        r.start(poll=60)
        # The stick's readings (EV_ABS) and a button let go are not a press
        os.write(r.fifos[1], struct.pack("llHHi", 0, 0, 3, 0, 1234) + struct.pack("llHHi", 0, 0, EV_SYN, 0, 0))
        r.key(1, BTN_SOUTH, 0)
        time.sleep(0.4)
        check(ledNow()[0] == "heartbeat" and not os.path.exists(r.seen), "the stick moving, or a button let go, is not a press")
        r.key(1, BTN_SOUTH, 1)
        time.sleep(0.4)
        check(os.path.exists(r.seen) and open(r.seen).read() == "sun50i-h700-anbernic-rg40xx-v\n" and ledNow() == ("none", "1"),
              "a button pressed: the tree is kept as seen working, the light steady")
        time.sleep(3.5)
        check(r.offCount() == 0, "and it is not turned off")
        r.stop()
        open(os.path.join(led, "trigger"), "w").write("heartbeat\n")
        r.start(poll=60)
        time.sleep(3.6)
        check(r.offCount() == 0 and "seen working before" in r.logged() and ledNow() == ("none", "1"),
              "the next start with the same tree: seen before, the light steady at once, never off")
        r.stop()
        open(r.tree, "wb").write(b"sun50i-h700-anbernic-rg40xx-v-v2-panel\0")
        r.start(poll=60)
        time.sleep(3.6)
        check(r.offCount() == 1 and "(before: sun50i-h700-anbernic-rg40xx-v)" in r.logged(),
              "another tree (the card moved to the other handheld, or a tree put there by hand): checked again, off")
        r.stop()
        # The power key's screen-off holds the buttons: a press then is not "seen"
        r.start(poll=60)
        r.press(0.2)
        r.key(1, BTN_SOUTH, 1)
        time.sleep(0.4)
        check(not open(r.seen).read().startswith("sun50i-h700-anbernic-rg40xx-v-v2"), "a button with the screen turned off by the power key does not count")
        r.stop()
        open(r.config, "w").write("screen-check 0\n")
        r.start(poll=60)
        time.sleep(3.6)
        check(r.offCount() == 0, "screen-check 0: never off for it")
        r.stop()
        # Nothing to show on at all: off after the look
        open(r.config, "w").write("screen-check 0\n")
        os.rmdir(os.path.join(r.sys, "class", "drm", "card0-DSI-1"))
        r.start(poll=60)
        time.sleep(0.5)
        check(r.offCount() == 0, "no display in /sys, before the look: on")
        time.sleep(1.2)
        check(r.offCount() == 1 and "no screen to show anything on (backlight: yes, display: none)" in r.logged(), "no display 1 s in: off")
        r.stop()
        os.makedirs(os.path.join(r.sys, "class", "drm", "card0-DSI-1"))
        os.rename(os.path.join(r.sys, "class", "backlight", "backlight"), os.path.join(r.root, "bl-away"))
        r.start(poll=60)
        time.sleep(1.7)
        check(r.offCount() == 1 and "(backlight: none, display: yes)" in r.logged(), "no backlight: off")
        r.stop()
        os.rename(os.path.join(r.root, "bl-away"), os.path.join(r.sys, "class", "backlight", "backlight"))
        r.start(poll=60)
        time.sleep(1.7)
        check(r.offCount() == 0, "both there: on")
        check(r.stop() == 0, "stops")
    finally:
        if r and r.p and r.p.poll() is None:
            r.p.kill()
        shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("%d check(s) failed" % len(fails))
        return 1
    print("every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
