#!/usr/bin/env python3
"""uboot-pick-test.py IMAGE.img.gz

The desk test of how U-Boot picks the device tree (uboot.fragment's
CONFIG_PREBOOT and extlinux.conf's FDTDIR /). qemu cannot run the H700's own
U-Boot, so this builds mainline U-Boot v2026.01 (the same release) for qemu's
"virt" machine, with the card's own PREBOOT text in it, three words changed:
the card's disk is "virtio 0:1" here, not "mmc 0:1"; the gpio set and clear
commands only echo; and "gpio input PE6" is "test -e virtio 0:1 pe6-low", so a
file on the boot partition stands in for the pin being low. Then it boots the
card's own boot partition three ways and reads which tree U-Boot loaded:

  no dtb.img, PE6 "low"    device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb
  no dtb.img, PE6 "high"   device_trees/sun50i-h700-anbernic-rg40xx-v.dtb
  dtb.img there            dtb.img, whatever PE6 says

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


def test_preboot(pre):
    pre = pre.replace("mmc 0:1", "virtio 0:1")
    pre = pre.replace("gpio input PE6", "test -e virtio 0:1 pe6-low")
    pre = re.sub(r"gpio (set|clear) (P[A-Z][0-9]+)", r"echo gpio \1 \2", pre)
    assert "gpio" not in pre.replace("echo gpio", ""), pre
    return "virtio scan; " + pre


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
             if not l.startswith(("CONFIG_PREBOOT=", "CONFIG_BOOTDELAY="))]
    lines += ['CONFIG_PREBOOT="%s"' % pre, "CONFIG_BOOTDELAY=0"]
    open(config, "w").write("\n".join(lines) + "\n")
    subprocess.run(make + ["olddefconfig"], check=True, env=env, stdout=subprocess.DEVNULL)
    subprocess.run(make + ["-j%d" % (os.cpu_count() or 4)], check=True, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    open(stamp, "w").write(pre)
    return binary


def boot(uboot, image, log):
    """Runs U-Boot on the card until it starts the kernel; returns its output."""
    p = subprocess.Popen(["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a53", "-m", "1024",
                          "-nographic", "-monitor", "none", "-serial", "stdio", "-bios", uboot,
                          "-drive", "file=%s,format=raw,if=none,id=card" % image,
                          "-device", "virtio-blk-device,drive=card", "-no-reboot"],
                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = b""
    t0 = time.time()
    os.set_blocking(p.stdout.fileno(), False)
    while time.time() - t0 < 90 and b"Starting kernel" not in out and p.poll() is None:
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
        cases = [("no dtb.img, PE6 low", ["pe6-low"], [], "/" + v2, "v2"),
                 ("no dtb.img, PE6 high", [], ["pe6-low"], "/" + first, "first"),
                 ("dtb.img copied there (the first screen's), PE6 low", ["pe6-low", "dtb.img"], [], "/dtb.img", "v2")]
        for n, (what, add, remove, want, screen) in enumerate(cases):
            for name in add:
                src = os.path.join(work, "flag") if name == "pe6-low" else None
                if name == "dtb.img":
                    src = os.path.join(work, "first.dtb")
                    open(src, "wb").write(mt("mtype", "-i", fat, "::" + first).stdout)
                mt("mcopy", "-o", "-i", fat, src, "::" + name)
            for name in remove:
                mt("mdel", "-i", fat, "::" + name)
            out = boot(uboot, image, os.path.join(work, "uboot%d.log" % n))
            got = re.findall(r"Retrieving file: (\S+\.(?:dtb|img))", out)
            dtbs = [g for g in got if g != "/Image"]
            check(dtbs == [want] and "Starting kernel" in out,
                  "%s: U-Boot loads %s and starts the kernel (%s)" % (what, want, ", ".join(dtbs) or "no tree"))
            check("apotheca: PE6 says the %s screen" % screen in out, "and says the %s screen" % screen)
            for name in add:
                mt("mdel", "-i", fat, "::" + name)
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
