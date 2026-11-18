#!/usr/bin/env python3
"""qemu-test.py IMAGE.img.gz

Boots the SD card image's own kernel and root file system in qemu's generic
arm64 machine ("virt") and checks what can be checked without the handheld:

  first boot   the root is the read-only squashfs on partition 2; the DATA
               partition (zeros in the image) is grown to the end of the
               (enlarged) disk, made exFAT and labelled APOTHECA; a boot log
               is written to it; only our start-up scripts and no stray
               daemons run; ROCKNIX's joypad module loads into the kernel
  apotheca     started at boot by inittab under apotheca-keep, on SDL's
               KMSDRM (qemu's virtio-gpu, Mesa) with sound through ALSA (qemu's
               virtio-sound): it runs, says its screen and sound, reads the
               RG40XXV's controls.txt, and its screen is not blank; told
               gpu-off, it starts again drawing with the CPU; the program also
               draws a still with SDL's offscreen driver, compared with a desk
               build when APOTHECA_DESK is set
  joypad       a uinput stand-in for the RG40XXV's joypad presses every
               button; SDL's numbers are the ones controls-rg40xxv.txt holds
  consoles     a shell on the serial port; killed, init starts it again
  supervisor   apotheca-keep backs off a program that keeps failing
               (1, 2, 4 s), starts a long-running one again at once, waits
               while held, and stops it with itself when told to
  power key    (batch 4) apotheca-power runs and finds a stand-in power
               key and joypad (fakepad --serve) that appear later; a short
               press holds the joypad (EVIOCGRAB: evtest is told it is
               grabbed and sees no press), another lets it go
  quit is off  apotheca-keep runs its off command, and does not start the
               program again, when the program leaves the off mark; a stale
               mark counts for nothing. Apotheca, quit with SEL + START on
               the stand-in joypad, writes the mark after saving, and the
               machine goes down
  second boot  DATA is NOT made again: a file written on the first boot is
               still there, and the boot log count goes on; the power key
               held 2.6 s turns the machine off, Apotheca stopped first
               (S95apotheca: TERM, handed on, saved)
  third boot   a made-up battery (the kernel's test_power) at 4%: on a
               charger nothing happens; unplugged, apotheca-power turns the
               machine off after two reads, Apotheca stopped first
  fourth boot  idle-off 1 in power.txt: Apotheca, left alone a minute,
               saves, leaves the mark and the machine goes down; then a
               shell poweroff, which also stops Apotheca first

qemu cannot run the H700's U-Boot, screen, GPU, sound, buttons, PMIC, USB or
WiFi: this boots the kernel straight from the image (no U-Boot) on qemu's
own virtual hardware (virtio disk, PL011 serial).

Needs qemu-system-aarch64 and mtools (mcopy). Exit code 0 when every check
passes. --keep leaves the work folder (the enlarged image, the console logs).
"""
import gzip, os, re, select, shutil, struct, subprocess, sys, tempfile, time

MiB = 1024 * 1024
fails = []


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what, flush=True)
    if not ok:
        fails.append(what)


CMDLINE = "root=PARTUUID=41504f54-02 rootfstype=squashfs ro rootwait console=ttyAMA0"


def qemu_args(image, kernel, machine="virt", dtb=None, append=CMDLINE, resets=False):
    """qemu's command line for a boot of the card (the QMP socket aside)."""
    return (["qemu-system-aarch64", "-M", machine, "-cpu", "cortex-a53", "-smp", "4",
             "-m", "1024", "-nographic", "-monitor", "none", "-serial", "stdio",
             "-kernel", kernel,
             "-append", append,
             "-drive", "file=%s,format=raw,if=none,id=card" % image,
             "-device", "virtio-blk-device,drive=card"] + ([] if resets else ["-no-reboot"]) + [
             # A screen (KMS, no GPU acceleration) and a sound card for Apotheca;
             # both need the modern (1.x) virtio, which virt's MMIO is not by itself
             "-global", "virtio-mmio.force-legacy=false",
             "-device", "virtio-gpu-device,xres=640,yres=480",
             "-audiodev", "none,id=snd0", "-device", "virtio-sound-device,audiodev=snd0"]
            + (["-dtb", dtb] if dtb else []))


class Machine:
    def __init__(self, image, kernel, log, dtb=None, append=CMDLINE, resets=False):
        self.log = open(log, "wb")
        self.buf = b""
        self.p = subprocess.Popen(
            qemu_args(image, kernel, dtb=dtb, append=append, resets=resets) +
            # (relative, in the work folder: a socket's path must be under 108 bytes)
            ["-qmp", "unix:%s,server=on,wait=off" % self.qmp_name(log)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            cwd=os.path.dirname(log))
        self.began = time.time()
        self.qmp = self.qmp_name(log)
        self.where = os.path.dirname(log)

    @staticmethod
    def qmp_name(log):
        return "qmp-%s.sock" % os.path.basename(log).split(".")[0]

    def screendump(self, path):
        """The virtual screen as a PPM file, through qemu's QMP socket."""
        import json, socket
        s = socket.socket(socket.AF_UNIX)
        here = os.getcwd()
        os.chdir(self.where)
        try:
            s.connect(self.qmp)
        finally:
            os.chdir(here)
        f = s.makefile("rw")
        f.readline()
        for cmd in ({"execute": "qmp_capabilities"},
                    {"execute": "screendump", "arguments": {"filename": path}}):
            f.write(json.dumps(cmd) + "\n")
            f.flush()
            while True:
                reply = json.loads(f.readline())
                if "return" in reply or "error" in reply:
                    break
        s.close()
        return "return" in reply

    def read_until(self, pattern, timeout):
        """Reads the console until the regex matches; returns the text before."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        while True:
            m = rx.search(self.buf)
            if m:
                text = self.buf[:m.end()]
                self.buf = self.buf[m.end():]
                return text.decode("utf-8", "replace")
            left = end - time.time()
            if left <= 0 or self.p.poll() is not None:
                raise TimeoutError("waited for %r; last: %r" % (pattern, self.buf[-300:]))
            r, _, _ = select.select([self.p.stdout], [], [], min(left, 0.5))
            if r:
                chunk = os.read(self.p.stdout.fileno(), 65536)
                self.log.write(chunk)
                self.log.flush()
                self.buf += chunk

    def send(self, line):
        self.p.stdin.write(line.encode() + b"\n")
        self.p.stdin.flush()

    n = 0

    def run(self, command, timeout=30):
        """Runs a shell command on the console; returns (output, exit code)."""
        Machine.n += 1
        tag = "Q%d" % Machine.n
        self.send("echo %s-BEGIN; %s; echo %s-END:$?" % (tag, command, tag))
        self.read_until(rb"\n%s-BEGIN\r?\n" % tag.encode(), timeout)
        out = self.read_until(rb"%s-END:(\d+)" % tag.encode(), timeout)
        code = int(re.search(r"%s-END:(\d+)" % tag, out).group(1))
        return out[: out.rfind(tag + "-END")].replace("\r", "").strip(), code

    def wait_shell(self, timeout=120):
        self.read_until(rb"Apotheca system, batch 4", timeout)
        self.read_until(rb"# ", 30)
        took = time.time() - self.began
        # The kernel's own messages off this console (all but its emergencies,
        # which "reboot: Power down" is): one landing in a command's output
        # failed checks now and then ("deferred probe pending" at about 13 s).
        # The boot log still has every message (dmesg is not touched).
        self.run("dmesg -n 1")
        return took

    def end(self, timeout=60):
        try:
            # Keep the console's last words (the kernel's "Power down") in the log.
            rest = self.p.communicate(timeout=timeout)[0] or b""
            self.log.write(rest)
            self.log.flush()
            self.p.wait(timeout)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()
            return False
        return True


def start_stand_in(m):
    """fakepad --serve: a stand-in joypad and power key, driven through /tmp/pad.
    Returns the joypad's /dev/input node, once apotheca-power has found both."""
    m.run("rm -f /tmp/pad; mkfifo /tmp/pad; /usr/libexec/apotheca/fakepad --serve /tmp/pad > /tmp/fp.txt 2>&1 & "
          "echo $! > /tmp/fp.pid")
    m.run("sleep 3")  # apotheca-power finds them at once (inotify); a margin for qemu
    out, _ = m.run("for e in /sys/class/input/event*; do grep -q 'H700 Gamepad' $e/device/name && "
                   "echo /dev/input/${e##*/}; done | tail -n 1")
    return out


def swap_program(m, put):
    """Stops Apotheca with apotheca-keep held, runs PUT (a shell command that
    puts something at /data/apotheca/apotheca), then lets it start again."""
    m.run("touch /run/apotheca/apotheca.hold; kill $(pidof apotheca) 2>/dev/null; "
          "n=0; while pidof apotheca > /dev/null && [ $n -lt 60 ]; do sleep 1; n=$((n+1)); done", timeout=90)
    m.run(put)
    m.run("sync; rm /run/apotheca/apotheca.hold")


def wait_log(m, text, seconds=120, log="/data/apotheca/logs/apotheca-start.log"):
    """Waits until LOG holds TEXT; returns whether it did."""
    t0 = time.time()
    while time.time() - t0 < seconds:
        out, _ = m.run("grep -cF %s %s" % (shell_quote(text), log))
        if out not in ("", "0"):
            return True
        m.run("sleep 3", timeout=30)
    return False


def shell_quote(text):
    return "'" + text.replace("'", "'\\''") + "'"


def wait_bootlog(m, seconds=120):
    """The boot log is written in the background (nothing waits for it at the
    start): waits until it is on DATA and on the boot partition."""
    out, _ = m.run("i=0; while [ ! -e /run/apotheca/bootlog-copied ] && [ $i -lt %d ]; do sleep 1; i=$((i+1)); done; "
                   "ls /run/apotheca/bootlog-copied" % seconds, timeout=seconds + 20)
    return "bootlog-copied" in out


def wait_running(m, seconds=180):
    t0 = time.time()
    while time.time() - t0 < seconds:
        out, _ = m.run("pidof apotheca || echo none")
        if out != "none":
            return True
        time.sleep(5)
    return False


PANELS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "board", "rg40xxv", "firmware", "panels")
HOST_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build-image", "output", "host", "bin")

PANEL_NODES = """
	gpio-sim {
		compatible = "gpio-simulator";
		sim: bank0 {
			gpio-controller;
			#gpio-cells = <2>;
			ngpios = <8>;
		};
	};
	spi-a {
		compatible = "spi-gpio";
		#address-cells = <1>;
		#size-cells = <0>;
		sck-gpios = <&sim 0 0>;
		mosi-gpios = <&sim 1 0>;
		cs-gpios = <&sim 2 0>;
		num-chipselects = <1>;
		panel@0 {
			compatible = "anbernic,rg40xx-v2-panel", "panel-mipi-dpi-spi";
			reg = <0>;
			spi-max-frequency = <3125000>;
			spi-3wire;
		};
	};
	spi-b {
		compatible = "spi-gpio";
		#address-cells = <1>;
		#size-cells = <0>;
		sck-gpios = <&sim 4 0>;
		mosi-gpios = <&sim 5 0>;
		cs-gpios = <&sim 6 0>;
		num-chipselects = <1>;
		panel@0 {
			compatible = "anbernic,rg40xx-v2-panel", "panel-mipi-dpi-spi";
			reg = <0>;
			spi-max-frequency = <3125000>;
			spi-3wire;
		};
	};
"""


def panel_boot(work, image, kernel):
    import zlib
    dtc = os.path.join(HOST_BIN, "dtc")
    if not (os.access(dtc, os.X_OK) and os.access(os.path.join(HOST_BIN, "fdtput"), os.X_OK)):
        print("skip  the panel boot: no dtc and fdtput in build-image/output/host/bin (build the image first)")
        return
    virt = os.path.join(work, "virt.dtb")
    subprocess.run(qemu_args(image, kernel, machine="virt,dumpdtb=" + virt),
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dts = subprocess.run([dtc, "-q", "-I", "dtb", "-O", "dts", virt], check=True,
                         capture_output=True, text=True).stdout
    end = dts.rstrip().rfind("};")
    dts = dts[:end] + PANEL_NODES + dts[end:]
    with open(os.path.join(work, "panels.dts"), "w") as f:
        f.write(dts)
    plain = os.path.join(work, "panels.dtb")
    subprocess.run([dtc, "-q", "-I", "dts", "-O", "dtb", "-o", plain, os.path.join(work, "panels.dts")], check=True)
    stock = os.path.join(PANELS, "..", "panels-to-try", "v2-stock-dither.panel")
    v2 = os.path.join(PANELS, "anbernic,rg40xx-v2-panel.panel")
    both = os.path.join(work, "panels-b.dtb")
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "panel-dtb.sh")
    r = subprocess.run([tool, plain, stock, both, "/spi-b/panel@0"], capture_output=True, text=True)
    check(r.returncode == 0, "panel-dtb.sh put a panel file into a device tree (%s)"
          % (r.stdout.strip() or r.stderr.strip()))
    m = Machine(image, kernel, os.path.join(work, "boot6.log"), dtb=both)
    m.wait_shell()
    lines = ""
    t0 = time.time()
    while time.time() - t0 < 60:
        lines, _ = m.run("dmesg | grep 'panel-mipi spi'")
        if lines.count("Modeline") >= 2:
            break
        m.run("sleep 3", timeout=30)
    print("info  " + lines.replace("\n", "\n      "))
    crc = lambda path: "%08x" % (zlib.crc32(open(path, "rb").read()) & 0xffffffff)
    check("firmware: panels/anbernic,rg40xx-v2-panel.panel: loaded successfully (%d bytes, crc32 %s)"
          % (os.path.getsize(v2), crc(v2)) in lines,
          "a panel with nothing in its node takes the panel file built into the kernel")
    check("firmware: the device tree's apotheca,panel-firmware: loaded successfully (%d bytes, crc32 %s)"
          % (os.path.getsize(stock), crc(stock)) in lines,
          "a panel with apotheca,panel-firmware takes that instead (try 4's panel file, its CRC-32)")
    check(lines.count("bus format 0x1009") == 1,
          "that file's 18-bit pixel format makes its bus RGB666_1X18 (the TCON dithers), and only its")
    check('Modeline "640x480": 60 27000 640 704 708 750 480 580 584 600' in lines,
          "the first panel's mode is the built-in v2 file's (27 MHz, 750 x 600)")
    check('Modeline "640x480": 60 24000 640 722 742 768 480 506 510 521' in lines,
          "the second's is the file's from the device tree (24 MHz, 768 x 521)")
    m.send("poweroff")
    check(m.end(60), "the panel boot powered down")


def early_boots(image, kernel, work, boot_start):
    """The starts that cannot go on (firmware plan, section 16)."""
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    fat = "%s@@%d" % (image, boot_start)
    # No root to be found: the first program says so on the boot partition
    # (found without the root: the partition that holds extlinux.conf), then
    # resets the machine.
    m = Machine(image, kernel, os.path.join(work, "boot7.log"),
                append="root=PARTUUID=deadbeef-02 rootfstype=squashfs ro console=ttyAMA0 apotheca.wait=6 panic=5")
    try:
        t0 = time.time()
        text = m.read_until(rb"reboot: Restarting system", 150)
        took = time.time() - t0
        check("apotheca-early: CANNOT GO ON: no root PARTUUID=deadbeef-02 after 6 s (1 disk seen)" in text,
              "a root that is not there: the first program gives up after its wait and says why")
        check(took > 20, "it waits (the lights' time) before it resets: %.0f s" % took)
    except TimeoutError as e:
        check(False, "a root that is not there: the machine resets (%s)" % e)
    m.end(30)
    log = subprocess.run(["mtype", "-i", fat, "::kernel-log.txt"], capture_output=True, env=env).stdout.decode("utf-8", "replace")
    check("CANNOT GO ON: no root PARTUUID=deadbeef-02" in log and "stage: stopped: could not start the system; resetting now" in log
          and "Linux version" in log and "MBR signature 41504f54" in log,
          "and kernel-log.txt on the boot partition has the reason, the disks and the kernel's messages")
    # A root that will not mount (the wrong kind named)
    m = Machine(image, kernel, os.path.join(work, "boot8.log"),
                append="root=PARTUUID=41504f54-02 rootfstype=vfat ro console=ttyAMA0 apotheca.wait=6 panic=5")
    try:
        text = m.read_until(rb"reboot: Restarting system", 150)
        check("CANNOT GO ON: the root /dev/vda2 will not mount as vfat" in text, "a root that will not mount: said, then a reset")
    except TimeoutError as e:
        check(False, "a root that will not mount: the machine resets (%s)" % e)
    m.end(30)
    # The kernel stopped before any program ran: the root never comes and
    # no first program is there to give up (rdinit names nothing, so the
    # kernel waits for the root itself, for good). Nobody puts the deadline
    # off: at 3 s here it panics, and with panic=3 the machine resets.
    m = Machine(image, kernel, os.path.join(work, "boot9.log"),
                append="root=PARTUUID=deadbeef-02 rootfstype=squashfs ro rootwait console=ttyAMA0 "
                       "rdinit=/none apotheca_deadline.seconds=3 panic=3")
    try:
        text = m.read_until(rb"Kernel panic - not syncing: apotheca_deadline", 60)
        check("apotheca_deadline: 3 s from now" in text, "the kernel's deadline: armed as it starts (3 s here), and it panics when nobody puts it off")
        text = m.read_until(rb"Rebooting in 3 seconds", 30)
        check(True, "and the panic ends in a reset (panic=3)")
    except TimeoutError as e:
        check(False, "the kernel's deadline panics and resets (%s)" % e)
    m.end(30)
    # The way round the first program, should it ever be the trouble:
    # rdinit=/none, and the kernel mounts the root itself, as before.
    m = Machine(image, kernel, os.path.join(work, "boot10.log"), append=CMDLINE + " rdinit=/none")
    m.wait_shell()
    out, _ = m.run("dmesg | grep -c apotheca-early; grep -c ' / squashfs ro' /proc/mounts; cat /sys/module/apotheca_deadline/parameters/seconds")
    check(out.split() == ["0", "1", "0"], "rdinit=/none: the kernel mounts the root itself and the system starts (%s)" % out.split())
    m.send("poweroff")
    check(m.end(60), "and powers down")


MEMORY_NODES = """
	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;
		ramoops@4d300000 {
			compatible = "ramoops";
			reg = <0x0 0x4d300000 0x0 0x100000>;
			record-size = <0x20000>;
			console-size = <0x80000>;
		};
		apotheca-crumbs@4d200000 {
			reg = <0x0 0x4d200000 0x0 0x1000>;
			no-map;
		};
	};
"""


def memory_boot(work, image, kernel):
    """What the kernel said, kept through a reset (ramoops), and U-Boot's
    count in memory: on qemu's own device tree with the two pieces of memory
    the card's trees name (post-image.sh), the same addresses and sizes."""
    dtc = os.path.join(HOST_BIN, "dtc")
    if not os.access(dtc, os.X_OK):
        print("skip  the memory boot: no dtc in build-image/output/host/bin (build the image first)")
        return
    virt = os.path.join(work, "virt-m.dtb")
    subprocess.run(qemu_args(image, kernel, machine="virt,dumpdtb=" + virt),
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dts = subprocess.run([dtc, "-q", "-I", "dtb", "-O", "dts", virt], check=True, capture_output=True, text=True).stdout
    end = dts.rstrip().rfind("};")
    with open(os.path.join(work, "memory.dts"), "w") as f:
        f.write(dts[:end] + MEMORY_NODES + dts[end:])
    dtb = os.path.join(work, "memory.dtb")
    subprocess.run([dtc, "-q", "-I", "dts", "-O", "dtb", "-o", dtb, os.path.join(work, "memory.dts")], check=True)
    m = Machine(image, kernel, os.path.join(work, "boot11.log"), dtb=dtb, append=CMDLINE + " panic=3", resets=True)
    m.wait_shell()
    out, _ = m.run("dmesg | grep -c 'ramoops: using 0x100000@0x4d300000'; grep -c ' /sys/fs/pstore pstore ' /proc/mounts; "
                   "ls /sys/fs/pstore | wc -l")
    check(out.split() == ["1", "1", "0"], "the kernel keeps its messages in the 1 MiB at 0x4d300000 (ramoops); nothing there from before, on a cold start (%s)" % out.split())
    # U-Boot's mark and count, as it leaves them; S00early cleared the mark
    # as the system started, and it can be written and read from Linux.
    out, _ = m.run("devmem 0x4d200000 32; devmem 0x4d200000 32 0x41504f43; devmem 0x4d200000 32; "
                   "/etc/init.d/S00early start > /dev/null 2>&1; sleep 1; devmem 0x4d200000 32")
    marks = [w for w in out.split() if w.startswith("0x")]
    check(marks == ["0x00000000", "0x41504F43", "0x00000000"], "U-Boot's mark at 0x4d200000 is Linux's to clear, and S00early clears it (%s)" % marks)
    # A kernel that dies: its last words stay in memory through the reset.
    m.send("dmesg -n 8; echo apotheca-last-words-4711 > /dev/kmsg; echo c > /proc/sysrq-trigger")
    try:
        m.read_until(rb"Rebooting in 3 seconds", 60)
        m.began = time.time()
        m.wait_shell()
        wait_bootlog(m)
        out, _ = m.run("grep -c -e 'apotheca-last-words-4711' -e 'Kernel panic - not syncing: sysrq triggered crash' "
                       "/sys/fs/pstore/console-ramoops-0; grep -c 'apotheca-last-words-4711' /data/apotheca/logs/boot-latest.txt")
        check(out.split()[0] == "2" and int(out.split()[1]) >= 1,
              "after a panic and a reset, the start that follows finds what the kernel said in memory, and its boot log has it (%s)" % out.split())
    except TimeoutError as e:
        check(False, "a panic resets the machine and it starts again (%s)" % e)
    m.send("poweroff")
    check(m.end(60), "the memory boot powered down")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    keep = "--keep" in sys.argv
    if len(args) != 1:
        print(__doc__)
        return 2
    work = tempfile.mkdtemp(prefix="apotheca-qemu-", dir=os.environ.get("TMPDIR", "/tmp"))
    image = os.path.join(work, "card.img")
    try:
        with gzip.open(args[0], "rb") as src, open(image, "wb") as dst:
            shutil.copyfileobj(src, dst, 16 * MiB)
        built = os.path.getsize(image)
        # A bigger "card": the first boot must grow DATA into the extra.
        with open(image, "r+b") as f:
            f.truncate(built + 512 * MiB)
            f.seek(446 + 16 * 0 + 8)
            boot_start = struct.unpack("<I", f.read(4))[0] * 512
        kernel = os.path.join(work, "Image")
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        subprocess.run(["mcopy", "-n", "-i", "%s@@%d" % (image, boot_start), "::Image", kernel],
                       check=True, env=env)

        # --- first boot
        m = Machine(image, kernel, os.path.join(work, "boot1.log"))
        secs = m.wait_shell()
        check(True, "first boot reached a shell on the serial port in %.1f s (qemu, emulated CPU)" % secs)
        out, _ = m.run("grep ' / ' /proc/mounts")
        check(" squashfs ro" in out, "root is the squashfs, read-only (%s)" % out)
        out, _ = m.run("grep ' /data ' /proc/mounts")
        check("/dev/vda4 /data exfat" in out, "DATA (vda4) on /data as exFAT")
        out, _ = m.run("blkid -s LABEL -o value /dev/vda4")
        check(out == "APOTHECA", "DATA made exFAT, labelled APOTHECA, after growing (%r)" % out)
        out, _ = m.run("cat /sys/class/block/vda4/size")
        grown = int(out) * 512
        check(grown > 500 * MiB, "DATA grew from 32 MiB to %d MiB" % (grown // MiB))
        out, _ = m.run("cat /run/apotheca/boot.log")
        check("first boot, growing" in out, "the start-up log says it grew DATA")
        check(wait_bootlog(m), "the boot log, written in the background, is done")
        out, code = m.run("ls /data/apotheca/logs; head -3 /data/apotheca/logs/boot-latest.txt")
        check("boot-0001.txt" in out and "Apotheca boot log" in out, "boot log written to DATA")
        out, _ = m.run("grep -c '^\\[' /data/apotheca/logs/boot-0001.txt")
        check(int(out or 0) > 20, "the boot log holds the kernel's messages (%s lines)" % out)
        # The kernel's first program (the initramfs's /init) ran before any
        # of that: it found the root by the disk's signature and handed
        # over; a start that goes well writes no kernel-log.txt (a start
        # that waits or fails does: the boots at the end of this test).
        # S00early then put the kernel's deadline off and wrote
        # linux-early.txt.
        out, _ = m.run("dmesg | grep -c 'apotheca-early: the root is vda2, on the disk vda'; "
                       "cat /sys/module/apotheca_deadline/parameters/seconds; grep -c ' / rootfs' /proc/mounts")
        check(out.split() == ["1", "0", "0"],
              "the kernel's first program found the root; the deadline is off; nothing of the initramfs is left mounted (%s)" % out.split())
        # (mounted as the system's own scripts mount it: one of them may have
        # it mounted this moment, and other options would be refused)
        out, _ = m.run("mkdir -p /tmp/b && mount -t vfat -o rw,noatime /dev/vda1 /tmp/b && "
                       "ls /tmp/b/kernel-log.txt /tmp/b/kernel-log.part 2>/dev/null; "
                       "for w in 'S00early' '===== running now' 'Linux version'; "
                       "do grep -q \"$w\" /tmp/b/linux-early.txt && echo e; done; umount /tmp/b")
        check(out.split() == ["e"] * 3, "no kernel-log.txt for a start that went well; linux-early.txt (S00early's) is on the boot partition (%s)" % " ".join(out.split()))
        out, _ = m.run("dmesg | grep -c \"apotheca-early: handing over to the root's /sbin/init\"; "
                       "head -c 1200 /dev/vcs1 2>/dev/null | tr -s ' ' | tr -d '\\000'")
        check(out.startswith("1"), "the first program says in the kernel's log that it hands over")
        print("info  the screen's first words (the text console, where this machine has one): %r" % out[1:].strip()[:60])
        # The same log on the BOOT partition (the FAT one), beside U-Boot's
        # report: the one place to look on a card out of a dark handheld.
        out, _ = m.run("mkdir -p /tmp/b && mount -t vfat -o rw,noatime /dev/vda1 /tmp/b && "
                       "head -n 1 /tmp/b/linux-report.txt; grep -c . /tmp/b/linux-report.txt; "
                       "cmp /tmp/b/linux-report.txt /data/apotheca/logs/boot-latest.txt && echo same; "
                       "ls /tmp/b/README.txt; umount /tmp/b; grep -c 'could not copy' /run/apotheca/boot.log")
        check("Apotheca boot log" in out and "same" in out and "/tmp/b/README.txt" in out and out.strip().endswith("0"),
              "the boot log is also on the boot partition, as linux-report.txt (%s)" % " | ".join(out.split("\n"))[:120])
        out, _ = m.run("awk '{print $4}' /proc/$$/stat")
        # (the last line: a kernel message can land on the console in between)
        check(out.splitlines()[-1].strip() == "1" if out else False,
              "the serial shell is init's own child (started by inittab, through getty)")
        out, _ = m.run("ps | grep -c '[s]leep 2147483647'")
        check(out == "2", "two absent consoles (ttyS0 and ttyGS0) asleep, not starting again (%s)" % out)
        out, _ = m.run("tty; awk '{print $7}' /proc/self/stat")
        check("/dev/ttyAMA0" in out and out.split()[-1] != "0", "the shell has the serial port as its terminal")
        out, _ = m.run("ls /etc/init.d | tr '\\n' ' '")
        check(out == "S00early S02modules S05data S10usbgadget S20sound S90bootlog S95apotheca rcK rcS",
              "only our start-up scripts are there (%s)" % out)
        out, _ = m.run("ps -o comm | sort -u | tr '\\n' ' '")
        check(not any(d in out.split() for d in ("crond", "syslogd", "klogd", "udhcpc")),
              "no daemon we did not ask for is running (%s)" % out)
        out, code = m.run("modprobe rocknix-singleadc-joypad && grep -c rocknix_singleadc_joypad /proc/modules")
        check(code == 0 and out == "1",
              "ROCKNIX's joypad module loads into this kernel (no joypad to bind to here)")
        out, _ = m.run("grep -c 'rocknix-singleadc-joypad' /lib/modules/*/modules.alias")
        check(out != "0", "its device-tree name is in modules.alias, so S02modules loads it on the handheld")
        out, _ = m.run("ifconfig lo | grep -c 127.0.0.1")
        check(out == "1", "loopback is up")

        # Init starts a console again when its shell ends
        m.send("kill -9 $$")
        t0 = time.time()
        m.wait_shell(30)
        check(True, "the serial shell, killed, came back in %.1f s" % (time.time() - t0))

        # The supervisor (batch 2 runs Apotheca under it)
        m.run("mkdir -p /tmp/k && rm -f /tmp/k/*")
        m.run("KEEP_QUICK=3 KEEP_MOST_WAIT=4 KEEP_LOG=/tmp/k KEEP_RUN=/tmp/k "
              "apotheca-keep fails sh -c 'exit 3' >/dev/null 2>&1 & echo $! > /tmp/k/fails.pid")
        m.run("KEEP_QUICK=3 KEEP_LOG=/tmp/k KEEP_RUN=/tmp/k "
              "apotheca-keep runs sleep 4 >/dev/null 2>&1 & echo $! > /tmp/k/runs.pid")
        m.run("touch /tmp/k/held.hold; KEEP_LOG=/tmp/k KEEP_RUN=/tmp/k "
              "apotheca-keep held sleep 100 >/dev/null 2>&1 & echo $! > /tmp/k/held.pid")
        m.run("sleep 14", timeout=40)
        out, _ = m.run("cat /tmp/k/fails.log")
        waits = re.findall(r"next start in (\d+)s", out)
        check(waits[:5] == ["1", "2", "4", "4", "4"][:len(waits[:5])] and len(waits) >= 4,
              "a program that keeps failing waits 1, 2, 4, 4 s between starts (%s)" % ",".join(waits))
        check("code 3" in out, "its exit code is logged")
        out, _ = m.run("cat /tmp/k/runs.log")
        waits = re.findall(r"next start in (\d+)s", out)
        check(len(waits) >= 2 and set(waits) == {"0"},
              "a program that ran long enough starts again at once (%s)" % ",".join(waits))
        out, _ = m.run("cat /tmp/k/held.log; ps -o args | grep -c '^sleep 100$'")
        check("held" in out and out.endswith("\n0"), "nothing is started while held")
        m.run("rm /tmp/k/held.hold; sleep 2")
        out, _ = m.run("cat /tmp/k/held.log; ps -o args | grep -c '^sleep 100$'")
        check("hold lifted" in out and "started" in out and out.endswith("\n1"),
              "the hold lifted, it starts")
        m.run("kill $(cat /tmp/k/held.pid); sleep 1")
        out, _ = m.run("cat /tmp/k/held.log; ps -o args | grep -c '^sleep 100$'")
        check("told to stop" in out and out.endswith("\n0"),
              "told to stop, it stops its program and itself")
        m.run("kill $(cat /tmp/k/fails.pid) $(cat /tmp/k/runs.pid)")
        # Batch 4: a program that leaves the off mark is not started again;
        # the off command is run instead. A mark left from before counts for nothing.
        m.run("KEEP_QUICK=3 KEEP_LOG=/tmp/k KEEP_RUN=/tmp/k KEEP_OFF='touch /tmp/k/off-ran' "
              "apotheca-keep offs sh -c 'sleep 1; echo quit > /tmp/k/offs.off' >/dev/null 2>&1 & echo $! > /tmp/k/offs.pid")
        m.run("echo idle > /tmp/k/stale.off; KEEP_QUICK=1 KEEP_MOST_WAIT=1 KEEP_LOG=/tmp/k KEEP_RUN=/tmp/k "
              "KEEP_OFF='touch /tmp/k/stale-ran' apotheca-keep stale sh -c 'sleep 2' >/dev/null 2>&1 & "
              "echo $! > /tmp/k/stale.pid")
        m.run("sleep 7", timeout=30)
        out, _ = m.run("cat /tmp/k/offs.log; ls /tmp/k")
        check("off-ran" in out and "asking for the handheld off (quit)" in out and out.count("started") == 1,
              "a program that leaves the off mark: the off command runs, it is not started again")
        check("stale-ran" not in out and "stale.off" not in out, "a mark left from before is deleted and counts for nothing")
        out, _ = m.run("grep -c started /tmp/k/stale.log")
        check(int(out or 0) >= 2, "that program is started again as usual (%s starts)" % out)
        m.run("kill $(cat /tmp/k/offs.pid) $(cat /tmp/k/stale.pid)")
        out, _ = m.run("pidof apotheca-power > /dev/null && echo running; head -2 /data/apotheca/logs/power.log")
        check(out.startswith("running") and "started: a short press turns the screen off" in out,
              "apotheca-power is running (inittab) and says what the key does")

        # Batch 2, desk side: Apotheca as built by Buildroot's toolchain,
        # drawing a still with SDL's offscreen driver (no screen, no GPU).
        out, code = m.run("cat /usr/lib/apotheca/VERSION")
        version = out
        check(code == 0 and out.startswith("0."), "Apotheca %s is on the card" % out)
        out, code = m.run("cd /usr/lib/apotheca && SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "
                          "./apotheca --demo --view fx --shot /tmp/fx.bmp > /tmp/fx.log 2>&1; "
                          "echo $?; cd /; ls -l /tmp/fx.bmp | awk '{print $5}'", timeout=180)
        lines = out.split()
        check(len(lines) == 2 and lines[0] == "0" and lines[1].isdigit() and int(lines[1]) >= 640 * 480 * 3,
              "Apotheca ran under qemu and drew the FX screen to a BMP (%s)" % " ".join(lines))
        shot = os.path.join(work, "fx-qemu.bmp")
        out, code = m.run("gzip -c /tmp/fx.bmp | base64 | tr -d '\\n'; echo", timeout=120)
        try:
            open(shot, "wb").write(gzip.decompress(__import__("base64").b64decode(out)))
        except Exception as e:
            check(False, "the still came back over the serial port (%s)" % e)
        desk = os.environ.get("APOTHECA_DESK")
        if desk and os.path.exists(shot):
            ref = os.path.join(work, "fx-desk.bmp")
            subprocess.run([desk, "--demo", "--view", "fx", "--shot", ref],
                           cwd=os.path.dirname(desk), capture_output=True,
                           env=dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy"))
            a, b = open(shot, "rb").read(), open(ref, "rb").read()
            if len(a) == len(b):
                off = struct.unpack_from("<I", a, 10)[0]
                bpp = struct.unpack_from("<H", a, 28)[0] // 8
                total = (len(a) - off) // bpp
                near = far = 0
                for i in range(off, len(a), bpp):
                    if a[i:i + bpp] != b[i:i + bpp]:
                        d = max(abs(x - y) for x, y in zip(a[i:i + bpp], b[i:i + bpp]))
                        if d <= 2:
                            near += 1
                        else:
                            far += 1
                # gcc on arm64 joins a multiply and an add into one rounding
                # (fp-contract), so the lettering's anti-aliasing can land a
                # level or two apart from the desk's (x86). More than that, on
                # more than a few pixels, is a real difference.
                print("info  the still against the desk's: %d of %d pixels identical, %d within 2 levels, %d further"
                      % (total - near - far, total, near, far))
                check(far <= 200, "the still matches the desk's own build (at most 200 pixels more than 2 levels apart: %d)" % far)
            else:
                check(False, "the still is the desk's size (%d and %d bytes)" % (len(a), len(b)))
        else:
            print("skip  the still against the desk's (set APOTHECA_DESK to a desk build of apotheca)")

        # Batch 2: Apotheca started at boot by inittab, under apotheca-keep,
        # on SDL's KMSDRM through Mesa, sound through ALSA.
        t0 = time.time()
        running = False
        while time.time() - t0 < 180:
            out, _ = m.run("pidof apotheca || echo none")
            if out != "none":
                running = True
                break
            time.sleep(5)
        check(running, "Apotheca was started at boot (inittab, apotheca-keep)")
        if running:
            m.run("sleep 20", timeout=60)  # let it settle, and fail if it is going to
        out, _ = m.run("cat /data/apotheca/logs/apotheca-start.log")
        print("info  apotheca-start.log:\n      " + out.replace("\n", "\n      "))
        out, _ = m.run("cat /data/apotheca/logs/apotheca.log")
        print("info  apotheca-keep's log:\n      " + out.replace("\n", "\n      "))
        out, _ = m.run("cat /data/apotheca/apotheca.log")
        app_log = out
        print("info  apotheca.log:\n      " + out.replace("\n", "\n      "))
        check("screen: " in out, "Apotheca opened the screen on KMSDRM (%s)"
              % (re.search(r"screen: .*", out).group(0) if "screen: " in out else "no screen line"))
        check(re.search(r"audio: \d+ Hz", out) is not None, "Apotheca opened the sound card (%s)"
              % (re.search(r"audio: .*", out).group(0) if "audio: " in out else "no audio line"))
        check("controls: as learned (controls.txt)" in out,
              "the RG40XXV's controls.txt was put in place and read: no button setup on the first start")
        out, _ = m.run("pidof apotheca || echo none")
        check(out != "none", "Apotheca is still running after 20 s")
        dump = os.path.join(work, "screen-1.ppm")
        if m.screendump(dump) and os.path.exists(dump):
            data = open(dump, "rb").read()
            # P6, width, height, 255, then RGB
            parts = data.split(b"\n", 3)
            pixels = parts[3]
            colours = len(set(pixels[i:i + 3] for i in range(0, len(pixels), 3 * 7)))
            # Known gap (2026-11-17): on qemu's virtio-gpu, with Mesa drawing
            # through kms_swrast, SDL's KMSDRM mode set is refused (EINVAL:
            # "Could not set videomode on CRTC" in apotheca.log), so the
            # virtual screen stays blank while Apotheca runs. Said, not
            # counted: the handheld's panel and panfrost take another path.
            print("%s  the virtual screen shows Apotheca: %s, %d colours%s"
                  % ("ok   " if colours > 8 else "gap  ", parts[1].decode(), colours,
                     "" if colours > 8 else " (qemu's virtio-gpu refuses SDL's mode set; see apotheca.log)"))
        else:
            print("gap   no picture of the virtual screen (QMP screendump)")

        # The software fallback: told to draw with the CPU, it starts again so.
        m.run("touch /data/apotheca/gpu-off; kill $(pidof apotheca)")
        t0 = time.time()
        back = False
        while time.time() - t0 < 120:
            m.run("sleep 5", timeout=30)
            out, _ = m.run("grep ' start: ' /data/apotheca/logs/apotheca-start.log | tail -n 1; pidof apotheca || echo none")
            if "software drawing" in out and not out.endswith("none"):
                back = True
                break
        check(back, "with gpu-off it starts again drawing with the CPU (Mesa softpipe, SDL software)")
        m.run("sleep 15", timeout=40)
        out, _ = m.run("grep 'screen: ' /data/apotheca/apotheca.log; pidof apotheca || echo none")
        check("screen: " in out and not out.endswith("none"),
              "and keeps running that way (%s)" % out.replace("\n", "; "))
        m.run("rm /data/apotheca/gpu-off")

        # A program copied onto the card by hand (scripts/build-firmware-app.sh,
        # dist/firmware/apotheca): apotheca-start finds it by its sum.
        system_md5, _ = m.run("md5sum /usr/lib/apotheca/apotheca | cut -d' ' -f1")
        out, _ = m.run("cat /data/apotheca/program.md5")
        check(out == system_md5, "program.md5 holds the sum of the program put in place (the system's)")
        # 1. A good one: the system's own program with bytes added (another sum,
        #    the same program). It runs, says it started fine, and stays.
        swap_program(m, "cp /usr/lib/apotheca/apotheca /tmp/new && echo copied-by-hand >> /tmp/new && "
                        "cp /tmp/new /data/apotheca/apotheca")
        new_md5, _ = m.run("md5sum /tmp/new | cut -d' ' -f1")
        check(wait_log(m, "a program copied onto the card (md5 %s" % new_md5),
              "a program copied over apotheca is found and run (its md5 in apotheca-start.log)")
        stayed = False
        t0 = time.time()
        while time.time() - t0 < 90:
            m.run("sleep 5", timeout=30)
            out, _ = m.run("ls /data/apotheca/update-trying 2>/dev/null || echo gone; pidof apotheca || echo none")
            if out.startswith("gone") and not out.endswith("none"):
                stayed = True
                break
        out, _ = m.run("md5sum /data/apotheca/apotheca | cut -d' ' -f1; cat /data/apotheca/program.md5")
        check(stayed and out.split() == [new_md5, new_md5],
              "it got going (update-trying gone), keeps running, and is the program now")
        # 2. An arm64 program that stops at once (BusyBox, run as "apotheca"):
        #    two tries, then the system's program is back, drawing with the GPU.
        swap_program(m, "cp /bin/busybox /data/apotheca/apotheca")
        bb_md5, _ = m.run("md5sum /bin/busybox | cut -d' ' -f1")
        check(wait_log(m, "the new program did not start twice", 120),
              "a program copied by hand that stops at once: after two tries the one before comes back")
        out, _ = m.run("grep -c 'try 1 of 2' /data/apotheca/logs/apotheca-start.log")
        check(out != "0", "its first stop was a try, not yet the end (try 1 of 2)")
        check(wait_running(m, 60), "Apotheca running again")
        out, _ = m.run("md5sum /data/apotheca/apotheca /data/apotheca/apotheca.failed | cut -d' ' -f1 | tr '\\n' ' '; "
                       "cat /data/apotheca/program.md5")
        check(out.split() == [system_md5, bb_md5, system_md5],
              "the system's program is the program again, the one that failed kept as apotheca.failed")
        out, _ = m.run("grep ' start: ' /data/apotheca/logs/apotheca-start.log | tail -n 1")
        check("gpu drawing" in out and "fails before 2" in out,
              "after the two failed starts it still draws with the GPU (%s)" % out[out.find("start:"):])
        # 3. Not a program at all (a file cut short, a wrong file): never run.
        swap_program(m, "echo 'not a program' > /data/apotheca/apotheca")
        check(wait_log(m, "is not an arm64 program"), "a file that is not an arm64 program is not run")
        check(wait_running(m, 60), "the system's program runs instead")
        out, _ = m.run("md5sum /data/apotheca/apotheca | cut -d' ' -f1; cat /data/apotheca/apotheca.failed")
        check(out.split("\n") == [system_md5, "not a program"],
              "it is the system's program, the file kept as apotheca.failed")

        # The joypad as SDL sees it, against the controls.txt put on the card:
        # a uinput stand-in declares what the RG40XXV's driver declares and
        # presses each button; padprobe prints SDL's numbers.
        out, _ = m.run("/usr/libexec/apotheca/fakepad 3 > /tmp/fp.txt 2>&1 & "
                       "/usr/libexec/apotheca/padprobe -t 6 > /tmp/pp.txt 2>&1; wait; "
                       "cat /tmp/fp.txt; echo ===; cat /tmp/pp.txt", timeout=90)
        sent, _, seen = out.partition("===")
        presses = re.findall(r"press 0x[0-9a-f]+ (\w+)", sent)
        downs = [int(n) for n in re.findall(r"button (\d+) down", seen)]
        controls = {}
        for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "board", "rg40xxv",
                                      "rootfs-overlay", "usr", "share", "apotheca", "controls-rg40xxv.txt")):
            w = line.split()
            if len(w) == 5 and w[1] == "button":
                controls[w[0]] = int(w[2])
        check("H700 Gamepad" in seen, "SDL sees the stand-in as a joystick named H700 Gamepad")
        check(len(presses) == 17 and len(downs) == 17,
              "17 buttons pressed, 17 seen by SDL (%d, %d)" % (len(presses), len(downs)))
        wrong = [(p, d, controls.get(p)) for p, d in zip(presses, downs) if controls.get(p) != d]
        check(not wrong, "every button is where controls-rg40xxv.txt says (%s)"
              % (", ".join("%s: SDL %d, file %s" % w for w in wrong) or "all 17"))
        axes = re.findall(r"axis (\d+) (-?\d+)", seen)
        right = [int(v) for a, v in axes if a == "0" and int(v) > 16000]
        down = [int(v) for a, v in axes if a == "1" and int(v) > 16000]
        check(right and down, "the stick right is axis 0 up, down is axis 1 up, as the file says")

        # Batch 4: the power key and a joypad, made after apotheca-power
        # started (it finds them), and held while the screen is off.
        pad = start_stand_in(m)
        out, _ = m.run("cat /data/apotheca/logs/power.log")
        check("(axp20x-pek): the power key" in out and "(H700 Gamepad): a joypad" in out,
              "apotheca-power found the stand-in power key and joypad, made after it started")

        def grabbed():
            # (--grab: evtest then ends on TERM and its output is written; without it,
            # killed, its buffered lines are lost. It tries the grab first and says
            # if another process holds it.)
            out, _ = m.run("evtest --grab %s > /tmp/ev.txt 2>&1 & e=$!; sleep 1; echo 'tap A' > /tmp/pad; sleep 1; "
                           "kill $e; wait $e; cat /tmp/ev.txt" % pad, timeout=30)
            return "grabbed by another process" in out, re.search(r"code 305 \(\w+\), value 1", out) is not None
        held, seen = grabbed()
        check(not held and seen, "the screen on: the joypad is free, a press reaches a reader")
        m.run("echo 'tap POWER' > /tmp/pad; sleep 1")
        held, seen = grabbed()
        out, _ = m.run("grep -c 'the screen off, the buttons held' /data/apotheca/logs/power.log")
        check(out == "1" and held and not seen,
              "a short press of the power key: the screen off, the joypad held (evtest is told so and sees no press)")
        m.run("echo 'tap POWER' > /tmp/pad; sleep 1")
        held, seen = grabbed()
        check(not held and seen, "another short press: the joypad let go, presses reach a reader again")

        # The headphone socket (section 18): a stand-in input device with the
        # switch SW_HEADPHONE_INSERT. apotheca-power finds it, follows it,
        # and ASKS a real device its state when it starts with headphones
        # in. (This machine's sound card has no "Speaker Switch", so the
        # switching itself is said to fail here; the desk test and the
        # handheld have that part.)
        m.run("echo 'jack in' > /tmp/pad; sleep 2; echo 'jack out' > /tmp/pad; sleep 1; echo 'jack in' > /tmp/pad; sleep 1")
        # The switching runs amixer, slow under the emulator: wait up to 10 s for both lines.
        out, _ = m.run("for i in $(seq 20); do grep -q 'headphones out (just now)' /data/apotheca/logs/power.log && "
                       "grep -q 'headphones in (just now)' /data/apotheca/logs/power.log && break; sleep 0.5; done; "
                       "grep -e 'headphone' /data/apotheca/logs/power.log", timeout=40)
        if "headphones in (just now)" not in out:
            info, _ = m.run("tail -n 40 /data/apotheca/logs/power.log; grep -l . /sys/class/input/event*/device/capabilities/sw; "
                            "for e in /sys/class/input/event*; do echo $e $(cat $e/device/name) sw=$(cat $e/device/capabilities/sw); done; cat /tmp/fp.txt")
            print("info  power.log and the input devices:\n" + "\n".join("      " + l for l in info.splitlines()))
        check("(H616 Audio Codec Headphone Jack): the headphone socket" in out and "headphones out (just now): the speaker on" in out
              and "headphones in (just now): the speaker off" in out,
              "apotheca-power finds the stand-in headphone socket and follows it: out, the speaker on; in, off")
        before, _ = m.run("grep -c 'headphones in (as found): the speaker off' /data/apotheca/logs/power.log")
        m.run("kill $(pidof apotheca-power); sleep 5")
        out, _ = m.run("pidof apotheca-power > /dev/null && echo running; grep -c 'headphones in (as found): the speaker off' /data/apotheca/logs/power.log")
        # Counted against before the restart: the first start may have found the stand-in already in.
        check(out.split()[:1] == ["running"] and len(out.split()) == 2 and before.strip().isdigit()
              and int(out.split()[1]) == int(before.strip()) + 1,
              "started again with headphones in: it asks the socket its state (EVIOCGSW) and turns the speaker off (%s)" % out.split())
        out, code = m.run("/usr/libexec/apotheca/speaker loud; echo $?; grep -c 'Speaker Switch' /etc/init.d/S20sound; "
                          "grep -c 'set_control \"Speaker Switch\"' /etc/init.d/S20sound")
        check(out.split()[-3:] == ["2", "1", "0"] or out.split()[-3] == "2" and out.split()[-1] == "0",
              "the speaker's switch takes on or off only, and the sound set-up at boot no longer turns the speaker on (%s)" % out.split()[-3:])
        m.run("echo 'jack out' > /tmp/pad; sleep 1")

        # Quit is off: Apotheca, started again with the stand-in joypad there,
        # quit with SEL + START: it saves, leaves the mark, and the machine goes down.
        m.run("echo kept > /data/kept.txt")  # something to find again after the power cycle
        m.run("kill $(pidof apotheca)")
        opened = False
        t0 = time.time()
        while time.time() - t0 < 120:
            m.run("sleep 3", timeout=30)
            out, _ = m.run("grep -c 'H700 Gamepad' /data/apotheca/apotheca.log")
            if out != "0":
                opened = True
                break
        check(opened, "Apotheca started again and opened the stand-in joypad")
        m.run("sleep 8", timeout=30)  # its first passes, and past apotheca-keep's 10 s
        m.send("echo 'down SELECT' > /tmp/pad; echo 'wait 400' > /tmp/pad; echo 'tap START' > /tmp/pad; "
               "echo 'wait 400' > /tmp/pad; echo 'up SELECT' > /tmp/pad")
        check(m.end(120), "SEL + START: the machine went down by itself")
        out = open(os.path.join(work, "boot1.log"), "rb").read().decode("utf-8", "replace")
        check("apotheca-keep: apotheca asked for the handheld off (quit)" in out,
              "apotheca-keep was asked for the handheld off by Apotheca (quit)")
        check("reboot: Power down" in out, "the kernel powered down cleanly")

        # --- second boot
        m = Machine(image, kernel, os.path.join(work, "boot2.log"))
        secs = m.wait_shell()
        check(True, "second boot reached a shell in %.1f s" % secs)
        out, _ = m.run("cat /data/kept.txt")
        check(out == "kept", "a file written on the first boot is still on DATA")
        out, _ = m.run("cat /run/apotheca/boot.log")
        check("growing" not in out, "DATA was not grown or made again")
        wait_bootlog(m)
        out, _ = m.run("cat /data/apotheca/logs/count; ls /data/apotheca/logs")
        check(out.startswith("2") and "boot-0002.txt" in out, "the second boot's log is boot-0002")
        out, _ = m.run("grep 'asking for the handheld off' /data/apotheca/logs/apotheca.log")
        check("code 0" in out and "(quit)" in out, "the first boot's quit is in apotheca-keep's log: code 0, quit (%s)" % out)
        out, _ = m.run("grep -c 'turning the handheld off (quit)' /data/apotheca/logs/boot-0002.txt")
        check(out != "0", "and Apotheca's own log (in the boot log) says it turned off after saving")
        check(wait_running(m), "Apotheca started at boot again")

        # The power key held: off, Apotheca stopped first (S95apotheca).
        start_stand_in(m)
        m.send("echo 'down POWER' > /tmp/pad; echo 'wait 2600' > /tmp/pad; echo 'up POWER' > /tmp/pad")
        check(m.end(120), "the power key held 2.6 s: the machine went down")
        out = open(os.path.join(work, "boot2.log"), "rb").read().decode("utf-8", "replace")
        check("apotheca-power: power key held: turning off" in out, "apotheca-power said why")
        check(re.search(r"Apotheca stopped \(in [0-9.]+ s\), its song saved", out) is not None,
              "S95apotheca stopped Apotheca before the card went read-only")
        check("reboot: Power down" in out, "the kernel powered down cleanly")

        # --- third boot: a made-up battery
        m = Machine(image, kernel, os.path.join(work, "boot3.log"))
        m.wait_shell()
        check(wait_running(m), "third boot: Apotheca running")
        out, code = m.run("modprobe test_power && ls /sys/class/power_supply | tr '\\n' ' '")
        check(code == 0 and "test_battery" in out, "the kernel's made-up battery is there (%s)" % out)
        m.run("P=/sys/module/test_power/parameters; echo 4 > $P/battery_capacity; echo discharging > $P/battery_status")
        m.run("sleep 50", timeout=90)
        out, _ = m.run("cat /data/apotheca/logs/power.log")
        out = out[out.rfind("started:"):]  # this boot's lines (the log goes on from boot to boot)
        check("battery 4%" in out and "on a charger" in out and "turning off" not in out,
              "4% with the charger plugged in (test_ac online): nothing happens")
        m.send("P=/sys/module/test_power/parameters; echo off > $P/ac_online; echo off > $P/usb_online")
        check(m.end(120), "unplugged at 4%: the machine went down by itself")
        out = open(os.path.join(work, "boot3.log"), "rb").read().decode("utf-8", "replace")
        check("apotheca-power: battery low (4%), read 1 of 2" in out, "apotheca-power read it low once")
        check("apotheca-power: battery low (4%): turning off" in out, "then again, and turned off")
        check(re.search(r"Apotheca stopped \(in [0-9.]+ s\), its song saved", out) is not None,
              "Apotheca stopped first")
        check("reboot: Power down" in out, "the kernel powered down cleanly")

        # --- fourth boot: idle-off
        m = Machine(image, kernel, os.path.join(work, "boot4.log"))
        m.wait_shell()
        check(wait_running(m), "fourth boot: Apotheca running")
        m.run("echo 'idle-off 1' > /data/apotheca/power.txt; kill $(pidof apotheca)")
        t0 = time.time()
        check(m.end(240), "idle-off 1: the machine went down by itself after %.0f s" % (time.time() - t0))
        out = open(os.path.join(work, "boot4.log"), "rb").read().decode("utf-8", "replace")
        check("apotheca-keep: apotheca asked for the handheld off (idle)" in out,
              "Apotheca, left alone a minute, asked for the handheld off (idle)")
        check("reboot: Power down" in out, "the kernel powered down cleanly")

        # --- fifth boot: poweroff from the shell also stops Apotheca first
        m = Machine(image, kernel, os.path.join(work, "boot5.log"))
        m.wait_shell()
        check(wait_running(m), "fifth boot: Apotheca running")
        out, _ = m.run("grep -c 'idle off after 1 min' /data/apotheca/logs/apotheca-start.log")
        check(out != "0", "run handed power.txt's idle-off to Apotheca")
        m.run("rm /data/apotheca/power.txt")
        m.send("poweroff")
        check(m.end(60), "poweroff ends the machine")
        out = open(os.path.join(work, "boot5.log"), "rb").read().decode("utf-8", "replace")
        check(re.search(r"Apotheca stopped \(in [0-9.]+ s\), its song saved", out) is not None,
              "a shell poweroff stops Apotheca first too")

        # --- sixth boot: the panel driver, on panels that exist only in this
        # boot's device tree (qemu's own, with a simulated GPIO chip, two
        # bit-banged SPI buses and a panel on each): one takes the panel file
        # built into the kernel, the other the one os/tools/panel-dtb.sh put
        # in the device tree (patch 0170): try 4's, 18-bit, stock timings.
        panel_boot(work, image, kernel)
        early_boots(image, kernel, work, boot_start)
        memory_boot(work, image, kernel)
    except TimeoutError as e:
        check(False, "timed out: %s" % e)
    finally:
        if keep:
            print("work folder kept: %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("%d check(s) failed" % len(fails))
        return 1
    print("every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
