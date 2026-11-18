#!/usr/bin/env python3
"""uboot-pick-test.py IMAGE.img.gz

The desk test of how U-Boot picks the device tree (uboot.fragment's
CONFIG_PREBOOT and extlinux.conf's FDTDIR /). qemu cannot run the H700's own
U-Boot, so this builds mainline U-Boot v2026.01 (the same release) for qemu's
"virt" machine, with the card's own PREBOOT text in it, three words changed:
the card's disk is "virtio 0:1" here, not "mmc 0:1"; the gpio set and clear
commands only echo; and "gpio input PE6" is "test -e virtio 0:1 pe6-low", so a
file on the boot partition stands in for the pin being low. Then it boots the
card's own boot partition five ways and reads which tree U-Boot loaded, the
order of the gpio commands, and the uboot-report.txt it wrote to the card:

  no dtb.img, PE6 "low"    device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb
  no dtb.img, PE6 "high"   device_trees/sun50i-h700-anbernic-rg40xx-v.dtb
  screen-first.txt there   the first screen's tree, whatever PE6 says
  screen-v2.txt there      the newer screen's tree, whatever PE6 says
  dtb.img there            dtb.img, whatever PE6 or a screen-*.txt says

and that the boot partition is still a sound FAT after U-Boot's writes;
that the kernel's messages left in memory are saved; and that three resets in
a row stop it.

A second part (when arm-linux-gnueabihf-gcc is there) runs the card commands
for real: mainline U-Boot for the Orange Pi PC, an Allwinner board qemu
emulates with its SD card reader, so the same sunxi card driver as the
handheld's U-Boot writes the report, the memory's 512 KiB as a file and onto
the card's bare sectors.

What it cannot show: that PE6 on the H700 reads as the stock boot loader's
code says (low on the newer screen), that the reset pulse before the read is
what the pin needs, and the gpio names on the H700. Only the two handhelds
can (their boot logs: U-Boot's choice is in rocknix-dt-id, PE6 in S90bootlog's
"screen" line).

Needs qemu-system-aarch64, mtools, and an image built once (Buildroot's
toolchain and U-Boot's source download). The qemu U-Boot is built once into
build-image/uboot-pick-test/ (about two minutes). Exit 0 when all pass.
"""
import gzip, os, re, shutil, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, "..", ".."))
work_root = os.path.join(repo, "build-image")
host_bin = os.path.join(work_root, "output", "host", "bin")
fails = []


def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what, flush=True)
    if not ok:
        fails.append(what)


def card_preboot():
    text = open(os.path.join(repo, "os", "board", "rg40xxv", "uboot.fragment")).read()
    return re.search(r'^CONFIG_PREBOOT="(.*)"$', text, re.M).group(1)


# The memory-kind mark: on the handheld the clock's spare register 7
# (0x0700011c), which is no memory on either emulated board. Here it is a
# word of ordinary memory, loaded before the start and shown after PREBOOT.
MARK_REAL = "0x0700011c"
MARK_HERE = 0x4d100000


def mark_here(pre):
    assert pre.count(MARK_REAL) == 4, pre     # read twice (lpddr4? lpddr3?), written for each of the two files
    return pre.replace(MARK_REAL, "0x%08x" % MARK_HERE) + "; echo the mark is now; md.l 0x%08x 1" % MARK_HERE


def mark_file(work, value):
    path = os.path.join(work, "mark-%08x.bin" % value)
    open(path, "wb").write(value.to_bytes(4, "little"))
    return path


def test_preboot(pre):
    pre = mark_here(pre)
    pre = pre.replace("mmc 0:1", "virtio 0:1")
    pre = pre.replace("gpio input PE6", "test -e virtio 0:1 pe6-low")
    pre = re.sub(r"gpio (set|clear|toggle) (P[A-Z][0-9]+)", r"echo gpio \1 \2", pre)
    assert "gpio" not in pre.replace("echo gpio", ""), pre
    # No card reader and no power chip here: those commands only echo. (The
    # second part of this test runs them for real on an emulated sunxi board.)
    pre = re.sub(r"\b(mmc (?:dev|rescan|write)[^;]*|i2c [^;]*)", r"echo \1", pre)
    # fdt_addr_r: where extlinux loads a tree (sunxi's environment sets it;
    # qemu's does not, and without it U-Boot passes qemu's own tree).
    return "virtio scan; setenv fdt_addr_r 0x48000000; " + pre


def build_uboot(pre):
    out = os.path.join(work_root, "uboot-pick-test")
    stamp = os.path.join(out, "preboot.txt")
    binary = os.path.join(out, "src", "u-boot.bin")
    if os.path.exists(binary) and os.path.exists(stamp) and open(stamp).read() == pre:
        return binary
    tarball = os.path.join(work_root, "dl", "uboot", "u-boot-2026.01.tar.bz2")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "src"))
    subprocess.run(["tar", "-xjf", tarball, "-C", os.path.join(out, "src"), "--strip-components=1"], check=True)
    env = dict(os.environ, PATH=host_bin + ":" + os.environ["PATH"])
    make = ["make", "-C", os.path.join(out, "src"), "CROSS_COMPILE=aarch64-linux-"]
    subprocess.run(make + ["qemu_arm64_defconfig"], check=True, env=env, stdout=subprocess.DEVNULL)
    config = os.path.join(out, "src", ".config")
    lines = [l for l in open(config).read().splitlines()
             if not l.startswith(("CONFIG_PREBOOT=", "CONFIG_BOOTDELAY=", "CONFIG_TOOLS_MKEFICAPSULE="))]
    # (mkeficapsule, a host tool this test does not need, wants GnuTLS's headers)
    lines += ['CONFIG_PREBOOT="%s"' % pre, "CONFIG_BOOTDELAY=0", "# CONFIG_TOOLS_MKEFICAPSULE is not set"]
    open(config, "w").write("\n".join(lines) + "\n")
    subprocess.run(make + ["olddefconfig"], check=True, env=env, stdout=subprocess.DEVNULL)
    subprocess.run(make + ["-j%d" % (os.cpu_count() or 4)], check=True, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    open(stamp, "w").write(pre)
    return binary


def sunxi_preboot(pre):
    """The card's PREBOOT for the emulated sunxi board: its card commands
    (test -e, fatload, fatwrite, mmc dev / rescan / write) as they are."""
    pre = mark_here(pre)
    pre = pre.replace("gpio input PE6", "test -e mmc 0:1 pe6-low")
    pre = re.sub(r"gpio (set|clear|toggle) (P[A-Z][0-9]+)", r"echo gpio \1 \2", pre)
    pre = re.sub(r"\b(i2c [^;]*)", r"echo \1", pre)
    return pre


def build_sunxi_uboot(pre):
    """Mainline U-Boot v2026.01 for the Orange Pi PC (Allwinner H3), which
    qemu emulates with its SD card reader: the same sunxi card driver and FAT
    code as the handheld's U-Boot, 32-bit. Needs arm-linux-gnueabihf-gcc."""
    out = os.path.join(work_root, "uboot-sunxi-test")
    stamp = os.path.join(out, "preboot.txt")
    binary = os.path.join(out, "src", "u-boot-sunxi-with-spl.bin")
    if os.path.exists(binary) and os.path.exists(stamp) and open(stamp).read() == pre:
        return binary
    tarball = os.path.join(work_root, "dl", "uboot", "u-boot-2026.01.tar.bz2")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "src"))
    subprocess.run(["tar", "-xjf", tarball, "-C", os.path.join(out, "src"), "--strip-components=1"], check=True)
    env = dict(os.environ, PATH=host_bin + ":" + os.environ["PATH"])
    make = ["make", "-C", os.path.join(out, "src"), "CROSS_COMPILE=arm-linux-gnueabihf-"]
    subprocess.run(make + ["orangepi_pc_defconfig"], check=True, env=env, stdout=subprocess.DEVNULL)
    config = os.path.join(out, "src", ".config")
    drop = ("CONFIG_PREBOOT=", "CONFIG_BOOTDELAY=", "CONFIG_USE_PREBOOT", "# CONFIG_USE_PREBOOT",
            "CONFIG_TOOLS_MKEFICAPSULE=", "CONFIG_EFI_LOADER=", "# CONFIG_CMD_SETEXPR", "# CONFIG_FAT_WRITE")
    lines = [l for l in open(config).read().splitlines() if not l.startswith(drop)]
    lines += ["CONFIG_USE_PREBOOT=y", 'CONFIG_PREBOOT="%s"' % pre, "CONFIG_BOOTDELAY=0", "# CONFIG_TOOLS_MKEFICAPSULE is not set",
              "# CONFIG_EFI_LOADER is not set", "CONFIG_CMD_SETEXPR=y", "CONFIG_FAT_WRITE=y", "CONFIG_CMD_FAT=y"]
    open(config, "w").write("\n".join(lines) + "\n")
    subprocess.run(make + ["olddefconfig"], check=True, env=env, stdout=subprocess.DEVNULL)
    r = subprocess.run(make + ["-j%d" % (os.cpu_count() or 4)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0 or not os.path.exists(binary):
        print(r.stdout.decode("utf-8", "replace")[-3000:])
        raise SystemExit("the sunxi test U-Boot did not build")
    open(stamp, "w").write(pre)
    return binary


def sunxi_part(image_gz, work):
    """The card commands for real: U-Boot's sunxi card driver writing our
    boot partition and the card's bare sectors, on qemu's Orange Pi PC."""
    if not shutil.which("arm-linux-gnueabihf-gcc") or not shutil.which("qemu-system-arm"):
        print("skip  the sunxi part: needs arm-linux-gnueabihf-gcc and qemu-system-arm")
        return
    pre = sunxi_preboot(card_preboot())
    uboot = build_sunxi_uboot(pre)
    image = os.path.join(work, "sunxi-card.img")
    with gzip.open(image_gz, "rb") as src, open(image, "wb") as dst:
        shutil.copyfileobj(src, dst, 16 << 20)
    with open(image, "r+b") as f:
        f.truncate(1 << 30)                      # qemu wants an SD card of 2^n bytes
        f.seek(8 * 1024)
        f.write(open(uboot, "rb").read())        # this board's U-Boot where the H700's was
    boot_start = 4 << 20
    fat = "%s@@%d" % (image, boot_start)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    mt = lambda *a: subprocess.run(list(a), env=env, capture_output=True)
    open(os.path.join(work, "flag"), "w").write("for this test\n")
    mt("mcopy", "-o", "-i", fat, os.path.join(work, "flag"), "::screen-first.txt")
    zone = os.path.join(work, "sunxi-zone.bin")
    text = b"".join(b"[%5d.000000] a line the kernel said before it stopped\n" % i for i in range(3000))
    blob = (b"DBGC" + (0).to_bytes(4, "little") + len(text).to_bytes(4, "little") + text).ljust(0x80000, b"\0")
    open(zone, "wb").write(blob)
    command = lambda: ["qemu-system-arm", "-M", "orangepi-pc", "-nographic", "-monitor", "none", "-serial", "stdio",
                       "-drive", "file=%s,format=raw,if=sd" % image, "-no-reboot"]
    done = (b"Retrieving file: /Image", b"Retrieving file: /device_trees", b"=> ")
    out = boot(None, image, os.path.join(work, "sunxi1.log"), memory=[(zone, 0x4d380000), (mark_file(work, 0x41504413), MARK_HERE)],
               command=command(), until=done, seconds=120)
    check("apotheca_dram=lpddr3" in mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace"),
          "sunxi: the report says the kind of memory the mark holds (LPDDR3 here)")
    check("apotheca: device tree device_trees/sun50i-h700-anbernic-rg40xx-v.dtb by screen-first.txt" in out,
          "sunxi: U-Boot's PREBOOT runs before anything else has touched the card, and reads screen-first.txt from it")
    report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
    started = mt("mtype", "-i", fat, "::uboot-started.txt").stdout.decode("utf-8", "replace")
    check(started.startswith("apotheca_note=U-Boot started") and "apotheca_first=written" in report,
          "sunxi: uboot-started.txt, the first thing U-Boot does, is on the card, and the report says so")
    check("apotheca_starts=1" in report and "apotheca_why=screen-first.txt" in report and "apotheca_memory=saved" in report,
          "sunxi: uboot-report.txt is on the card's boot partition (%s)" % " | ".join(report.split("\n"))[:260])
    saved = mt("mtype", "-i", fat, "::kernel-from-memory.txt").stdout
    check(saved == blob, "sunxi: kernel-from-memory.txt holds the 512 KiB left in memory (%d bytes)" % len(saved))
    with open(image, "rb") as f:
        f.seek(0x1000 * 512)
        raw = f.read(0x80000)
        f.seek(0x1000 * 512 - 512)
        before = f.read(512)
        f.seek(0x1000 * 512 + 0x80000)
        after = f.read(512)
    check(raw == blob and before == bytes(512) and after == bytes(512),
          "sunxi: the same 512 KiB are on the card's bare sectors from 2 MiB, and nothing beside them was written")
    out = boot(None, image, os.path.join(work, "sunxi2.log"), command=command(), until=done, seconds=120)
    report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
    check("apotheca_starts=2" in report and "apotheca_memory=nothing" in report,
          "sunxi: the second start reads the first report back and counts on (2)")
    part = os.path.join(work, "sunxi-boot.fat")
    with open(image, "rb") as f:
        f.seek(boot_start)
        open(part, "wb").write(f.read(64 << 20))
    fsck = shutil.which("fsck.vfat") or shutil.which("fsck.fat")
    subprocess.run([fsck, "-a", part], capture_output=True)
    r = subprocess.run([fsck, "-n", part], capture_output=True)
    with gzip.open(image_gz, "rb") as src:
        src.seek(boot_start)
        open(os.path.join(work, "sunxi-boot0.fat"), "wb").write(src.read(64 << 20))
    same = all(len(mt("mtype", "-i", part, "::" + n).stdout) > 0 and
               mt("mtype", "-i", part, "::" + n).stdout == mt("mtype", "-i", os.path.join(work, "sunxi-boot0.fat"), "::" + n).stdout
               for n in ["Image", "extlinux/extlinux.conf", "device_trees/sun50i-h700-anbernic-rg40xx-v.dtb"])
    check(r.returncode == 0 and same, "sunxi: the boot partition is sound after it (the free-clusters note put right), its files as made")
    with gzip.open(image_gz, "rb") as src, open(image, "rb") as f:
        src.seek(68 << 20)
        f.seek(68 << 20)
        check(src.read(8 << 20) == f.read(8 << 20), "sunxi: the root partition's first 8 MiB are untouched")


def boot(uboot, image, log, memory=(), command=None, until=(b"Starting kernel",), seconds=90):
    """Runs U-Boot on the card until it starts the kernel; returns its output.
    memory: (file, address) pairs put into RAM before it starts."""
    if command is None:
        command = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a53", "-m", "1024",
                   "-nographic", "-monitor", "none", "-serial", "stdio", "-bios", uboot,
                   "-drive", "file=%s,format=raw,if=none,id=card" % image,
                   "-device", "virtio-blk-device,drive=card", "-no-reboot"]
    for path, address in memory:
        command += ["-device", "loader,file=%s,addr=0x%x,force-raw=on" % (path, address)]
    p = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = b""
    t0 = time.time()
    os.set_blocking(p.stdout.fileno(), False)
    while time.time() - t0 < seconds and not any(u in out for u in until) and p.poll() is None:
        try:
            chunk = p.stdout.read()
        except BlockingIOError:
            chunk = None
        if chunk:
            out += chunk
        else:
            time.sleep(0.2)
    p.kill()
    p.wait()
    open(log, "wb").write(out)
    return out.decode("utf-8", "replace")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    pre = test_preboot(card_preboot())
    print("info  the PREBOOT under test: " + pre)
    uboot = build_uboot(pre)
    work = tempfile.mkdtemp(prefix="apotheca-uboot-", dir=os.environ.get("TMPDIR", "/tmp"))
    try:
        image = os.path.join(work, "card.img")
        with gzip.open(args[0], "rb") as src, open(image, "wb") as dst:
            shutil.copyfileobj(src, dst, 16 << 20)
        boot_start = 4 << 20                       # genimage.cfg: p1 at 4 MiB
        fat = "%s@@%d" % (image, boot_start)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        mt = lambda *a: subprocess.run(list(a), env=env, capture_output=True)
        had = mt("mtype", "-i", fat, "::dtb-in-use.txt").stdout.decode()
        check(had.startswith("auto"), "the card is made with no dtb.img (dtb-in-use.txt: %s)" % had.strip())
        open(os.path.join(work, "flag"), "w").write("PE6 low, for this test\n")
        v2 = "device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb"
        first = "device_trees/sun50i-h700-anbernic-rg40xx-v.dtb"
        cases = [("no dtb.img, PE6 low", ["pe6-low"], "/" + v2, "v2", "low", "PE6"),
                 ("no dtb.img, PE6 high", [], "/" + first, "first", "high", "PE6"),
                 ("screen-first.txt there, PE6 low", ["pe6-low", "screen-first.txt"], "/" + first, "v2", "low", "screen-first.txt"),
                 ("screen-v2.txt there, PE6 high", ["screen-v2.txt"], "/" + v2, "first", "high", "screen-v2.txt"),
                 ("dtb.img copied there (the first screen's), PE6 low, screen-v2.txt too",
                  ["pe6-low", "dtb.img", "screen-v2.txt"], "/dtb.img", "v2", "low", "dtb.img")]
        mt("mdel", "-i", fat, "::uboot-report.txt")
        for n, (what, add, want, screen, pin, why) in enumerate(cases):
            for name in add:
                src = os.path.join(work, "flag")
                if name == "dtb.img":
                    src = os.path.join(work, "first.dtb")
                    open(src, "wb").write(mt("mtype", "-i", fat, "::" + first).stdout)
                mt("mcopy", "-o", "-i", fat, src, "::" + name)
            out = boot(uboot, image, os.path.join(work, "uboot%d.log" % n))
            got = re.findall(r"Retrieving file: (\S+\.(?:dtb|img))", out)
            dtbs = [g for g in got if g != "/Image"]
            check(dtbs == [want] and "Starting kernel" in out,
                  "%s: U-Boot loads %s and starts the kernel (%s)" % (what, want, ", ".join(dtbs) or "no tree"))
            check("apotheca: PE6 says the %s screen" % screen in out, "and says PE6 reads as the %s screen" % screen)
            # The lights and the backlight: green first of all, the backlight
            # off for the reset pulse and on again after the panel is off.
            gp = re.findall(r"^gpio (?:set|clear) P[A-Z][0-9]+", out, re.M)
            check(gp[:2] == ["gpio clear PI12", "gpio clear PD28"] and gp[-3:] == ["gpio clear PI14", "gpio clear PI15", "gpio set PD28"],
                  "the green light out first (red alone: U-Boot), the backlight off for the pulse, on again after it (%s)" % ", ".join(gp))
            # The report U-Boot leaves on the boot partition
            report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
            said = dict(l.split("=", 1) for l in report.splitlines() if "=" in l)
            check(said.get("apotheca_starts") == "%x" % (n + 1) and said.get("apotheca_pe6") == pin
                  and said.get("apotheca_why") == why and said.get("fdtfile") == want[1:]
                  and said.get("apotheca_note", "").startswith("U-Boot ran")
                  and said.get("apotheca_memory") == "nothing" and said.get("apotheca_resets") == "0",
                  "uboot-report.txt: start %d, PE6 %s, the tree %s by %s (%s)"
                  % (n + 1, pin, want[1:], why, " | ".join(report.split("\n"))[:300]))
            started = mt("mtype", "-i", fat, "::uboot-started.txt").stdout.decode("utf-8", "replace")
            check(started.startswith("apotheca_note=U-Boot started") and said.get("apotheca_first") == "written",
                  "uboot-started.txt is there (U-Boot's first act), and the report says that write went")
            check(said.get("apotheca_dram") == "unknown" and said.get("apotheca_dram_asked") == "nothing",
                  "and, with no mark and no file: memory kind unknown, nothing asked")
            mt("mdel", "-i", fat, "::uboot-started.txt")
            for name in add:
                mt("mdel", "-i", fat, "::" + name)
        # The kind of memory: the mark the first program left is read into
        # the report; dram-lpddr3.txt / dram-lpddr4.txt write "this kind
        # first at the next start" into it.
        for what, mark, files, found, asked, after in [
                ("the mark says LPDDR4 passed", 0x41504414, [], "lpddr4", "nothing", 0x41504414),
                ("the mark says LPDDR3 passed", 0x41504413, [], "lpddr3", "nothing", 0x41504413),
                ("a mark nobody knows", 0x12345678, [], "unknown", "nothing", 0x12345678),
                ("a try under way (no answer)", 0x41504424, [], "unknown", "nothing", 0x41504424),
                ("LPDDR4 passed, dram-lpddr3.txt there", 0x41504414, ["dram-lpddr3.txt"], "lpddr4", "lpddr3", 0x41504433),
                ("LPDDR3 passed, dram-lpddr4.txt there", 0x41504413, ["dram-lpddr4.txt"], "lpddr3", "lpddr4", 0x41504434),
                ("both files there", 0x41504414, ["dram-lpddr3.txt", "dram-lpddr4.txt"], "lpddr4", "lpddr3", 0x41504433)]:
            for name in files:
                mt("mcopy", "-o", "-i", fat, os.path.join(work, "flag"), "::" + name)
            out = boot(uboot, image, os.path.join(work, "uboot-dram.log"), memory=[(mark_file(work, mark), MARK_HERE)])
            report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
            said = dict(l.split("=", 1) for l in report.splitlines() if "=" in l)
            now = re.search(r"the mark is now\s+%08x: ([0-9a-f]{8})" % MARK_HERE, out)
            check(said.get("apotheca_dram") == found and said.get("apotheca_dram_asked") == asked
                  and now and int(now.group(1), 16) == after and "Starting kernel" in out,
                  "memory kind, %s: the report says found %s, asked %s; the mark afterwards %08x (%s, %s, %s)"
                  % (what, found, asked, after, said.get("apotheca_dram"), said.get("apotheca_dram_asked"), now and now.group(1)))
            for name in files:
                mt("mdel", "-i", fat, "::" + name)
        # What the kernel of the start before left in memory (ramoops's
        # console piece, 512 KiB at 0x4d380000, beginning "DBGC"): U-Boot
        # saves it as kernel-from-memory.txt, and (here only said) raw.
        zone = os.path.join(work, "zone.bin")
        text = b"".join(b"[%5d.000000] a line the kernel said before it stopped\n" % i for i in range(3000))
        blob = (b"DBGC" + (0).to_bytes(4, "little") + len(text).to_bytes(4, "little") + text).ljust(0x80000, b"\0")
        open(zone, "wb").write(blob)
        mt("mcopy", "-o", "-i", fat, os.path.join(work, "flag"), "::pe6-low")
        out = boot(uboot, image, os.path.join(work, "uboot-memory.log"), memory=[(zone, 0x4d380000)])
        saved = mt("mtype", "-i", fat, "::kernel-from-memory.txt").stdout
        report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
        check(saved == blob and "apotheca_memory=saved" in report and "Starting kernel" in out,
              "the kernel's messages left in memory are saved as kernel-from-memory.txt, whole (%d bytes), and the start goes on" % len(saved))
        check("mmc write 0x4d380000 0x1000 0x400" in out and out.index("mmc write") < out.index("Retrieving file"),
              "and written straight onto the card at 2 MiB (here the command is only said)")
        # Resets in a row with no start between: counted in memory, and at
        # three no fourth start.
        crumbs = os.path.join(work, "crumbs.bin")
        open(crumbs, "wb").write((0x41504f43).to_bytes(4, "little") + (1).to_bytes(4, "little"))
        out = boot(uboot, image, os.path.join(work, "uboot-resets2.log"), memory=[(crumbs, 0x4d200000)])
        report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
        check("apotheca_resets=2" in report and "Starting kernel" in out, "a second reset in a row: counted (2), and it starts")
        open(crumbs, "wb").write((0x41504f43).to_bytes(4, "little") + (2).to_bytes(4, "little"))
        out = boot(uboot, image, os.path.join(work, "uboot-resets3.log"), memory=[(crumbs, 0x4d200000)],
                   until=(b"Starting kernel", b"gpio toggle PI11\r\ngpio toggle PI11\r\ngpio toggle PI11"), seconds=25)
        report = mt("mtype", "-i", fat, "::uboot-report.txt").stdout.decode("utf-8", "replace")
        check("apotheca_resets=3" in report and "Starting kernel" not in out and "not starting again" in out
              and "i2c mw 0x34 0x27 1" in out and out.count("gpio toggle PI11") >= 3,
              "a third: no start; the power chip is told to turn off and the red light blinks")
        mt("mdel", "-i", fat, "::pe6-low", "::kernel-from-memory.txt")
        # The FAT after U-Boot's writes: still sound, and the files the
        # card was made with still read as they were.
        part = os.path.join(work, "boot.fat")
        with open(image, "rb") as f:
            f.seek(boot_start)
            open(part, "wb").write(f.read(64 << 20))
        fsck = shutil.which("fsck.vfat") or shutil.which("fsck.fat") or os.path.join(host_bin, "..", "sbin", "fsck.fat")
        # U-Boot's FAT code does not keep the "free clusters" note in the
        # FSInfo sector (a hint for a quick "space left"; mtools, Linux and
        # Windows work the count out themselves when it is off), so fsck may
        # say that one thing and nothing else.
        r = subprocess.run([fsck, "-n", part], capture_output=True)
        said = [l for l in r.stdout.decode("utf-8", "replace").splitlines()[1:]
                if l.strip() and not l.startswith((os.path.basename(part), part))
                and "Auto-correcting" not in l and "Leaving filesystem unchanged" not in l]
        check(all(l.startswith("Free cluster summary wrong") for l in said) and len(said) <= 1,
              "the boot partition is sound after U-Boot's writes (fsck.fat -n says: %s)" % (said or "nothing"))
        subprocess.run([fsck, "-a", part], capture_output=True)
        r = subprocess.run([fsck, "-n", part], capture_output=True)
        check(r.returncode == 0, "and with that note put right, fsck.fat finds nothing at all")
        with gzip.open(args[0], "rb") as src:
            src.seek(boot_start)
            open(os.path.join(work, "boot0.fat"), "wb").write(src.read(64 << 20))
        same = True
        for name in ["Image", "extlinux/extlinux.conf", first, v2]:
            a = mt("mtype", "-i", part, "::" + name).stdout
            b = mt("mtype", "-i", os.path.join(work, "boot0.fat"), "::" + name).stdout
            same = same and len(a) > 0 and a == b
        check(same, "and its Image, extlinux.conf and trees read as they were made")
        sunxi_part(args[0], work)
    finally:
        if "--keep" in sys.argv:
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
