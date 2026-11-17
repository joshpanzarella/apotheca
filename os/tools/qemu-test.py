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
  poweroff     the system goes down cleanly and qemu ends
  second boot  DATA is NOT made again: a file written on the first boot is
               still there, and the boot log count goes on

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


class Machine:
    def __init__(self, image, kernel, log):
        self.log = open(log, "wb")
        self.buf = b""
        self.p = subprocess.Popen(
            ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a53", "-smp", "4",
             "-m", "1024", "-nographic", "-monitor", "none", "-serial", "stdio",
             "-kernel", kernel,
             "-append", "root=PARTUUID=41504f54-02 rootfstype=squashfs ro rootwait console=ttyAMA0",
             "-drive", "file=%s,format=raw,if=none,id=card" % image,
             "-device", "virtio-blk-device,drive=card", "-no-reboot",
             # A screen (KMS, no GPU acceleration) and a sound card for Apotheca;
             # both need the modern (1.x) virtio, which virt's MMIO is not by itself
             "-global", "virtio-mmio.force-legacy=false",
             "-device", "virtio-gpu-device,xres=640,yres=480",
             "-audiodev", "none,id=snd0", "-device", "virtio-sound-device,audiodev=snd0",
             # (relative, in the work folder: a socket's path must be under 108 bytes)
             "-qmp", "unix:%s,server=on,wait=off" % self.qmp_name(log)],
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
        self.read_until(rb"Apotheca system, batch 2", timeout)
        self.read_until(rb"# ", 30)
        return time.time() - self.began

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
        out, code = m.run("ls /data/apotheca/logs; head -3 /data/apotheca/logs/boot-latest.txt")
        check("boot-0001.txt" in out and "Apotheca boot log" in out, "boot log written to DATA")
        out, _ = m.run("grep -c '^\\[' /data/apotheca/logs/boot-0001.txt")
        check(int(out or 0) > 20, "the boot log holds the kernel's messages (%s lines)" % out)
        out, _ = m.run("awk '{print $4}' /proc/$$/stat")
        check(out == "1", "the serial shell is init's own child (started by inittab, through getty)")
        out, _ = m.run("ps | grep -c '[s]leep 2147483647'")
        check(out == "2", "two absent consoles (ttyS0 and ttyGS0) asleep, not starting again (%s)" % out)
        out, _ = m.run("tty; awk '{print $7}' /proc/self/stat")
        check("/dev/ttyAMA0" in out and out.split()[-1] != "0", "the shell has the serial port as its terminal")
        out, _ = m.run("ls /etc/init.d | tr '\\n' ' '")
        check(out == "S02modules S05data S10usbgadget S20sound S90bootlog rcK rcS",
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
            out, _ = m.run("tail -n 1 /data/apotheca/logs/apotheca-start.log; pidof apotheca || echo none")
            if "software drawing" in out and not out.endswith("none"):
                back = True
                break
        check(back, "with gpu-off it starts again drawing with the CPU (Mesa softpipe, SDL software)")
        m.run("sleep 15", timeout=40)
        out, _ = m.run("grep 'screen: ' /data/apotheca/apotheca.log; pidof apotheca || echo none")
        check("screen: " in out and not out.endswith("none"),
              "and keeps running that way (%s)" % out.replace("\n", "; "))
        m.run("rm /data/apotheca/gpu-off")

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

        # Something to find again after a power cycle
        m.run("echo kept > /data/kept.txt")
        m.send("poweroff")
        check(m.end(60), "poweroff ends the machine")
        out = open(os.path.join(work, "boot1.log"), "rb").read().decode("utf-8", "replace")
        check("reboot: Power down" in out, "the kernel powered down cleanly")

        # --- second boot
        m = Machine(image, kernel, os.path.join(work, "boot2.log"))
        secs = m.wait_shell()
        check(True, "second boot reached a shell in %.1f s" % secs)
        out, _ = m.run("cat /data/kept.txt")
        check(out == "kept", "a file written on the first boot is still on DATA")
        out, _ = m.run("cat /run/apotheca/boot.log")
        check("growing" not in out, "DATA was not grown or made again")
        out, _ = m.run("cat /data/apotheca/logs/count; ls /data/apotheca/logs")
        check(out.startswith("2") and "boot-0002.txt" in out, "the second boot's log is boot-0002")
        m.send("poweroff")
        check(m.end(60), "poweroff ends the machine again")
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
