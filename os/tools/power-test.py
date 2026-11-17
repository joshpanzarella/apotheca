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
  stop         TERM: it says so and ends

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
             "--off", "echo off >> %s" % self.offs, "--poll", str(poll), "--console", os.devnull],
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
