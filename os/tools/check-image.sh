#!/bin/sh
# check-image.sh IMAGE[.gz]
#
# Checks an Apotheca SD card image without a handheld:
#   - the MBR: its signature, the disk signature the kernel finds its root by
#     (0x41504f54, so PARTUUID 41504f54-02), four partitions of the right
#     types, in order, on 1 MiB boundaries, none overlapping, none past the end
#   - U-Boot's SPL at 8 KiB, where the H700's boot ROM reads it: the eGON.BT0
#     header, its length and its checksum (the boot ROM refuses a bad one),
#     and U-Boot proper (a FIT image) after it, all ending before partition 1
#   - partition 1: FAT, holding Image, dtb.img (a device tree), the three
#     RG40XXV trees and extlinux/extlinux.conf naming the right root
#   - partition 2: a squashfs that fits its partition; with unsquashfs (on the
#     PATH, or the one the image build made) also what batch 4 put in it:
#     apotheca-power started by inittab, S95apotheca, the off mark handed to
#     Apotheca and read by apotheca-keep, Apotheca that knows the mark
#   - partition 3: empty (the second system slot)
#   - partition 4: zeros (the first boot grows it and makes it exFAT)
#
# Needs python3. With mtools (mdir, mtype) it also reads the boot partition.
# Exit code 0 when every check passes.

set -eu
[ $# -eq 1 ] || { echo "usage: $0 IMAGE[.gz]" >&2; exit 2; }
img=$1
tmp=
cleanup() { [ -n "$tmp" ] && rm -rf "$tmp"; }
trap cleanup EXIT
case "$img" in
*.gz)
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/apotheca-check-XXXXXX")
    gzip -dc "$img" > "$tmp/image.img"
    img="$tmp/image.img"
    ;;
esac

here=$(cd "$(dirname "$0")/../.." && pwd)
python3 -I - "$img" "$here/build-image/output/host/bin/unsquashfs" "$here" <<'EOF'
import os, re, struct, subprocess, shutil, sys, tempfile

path = sys.argv[1]
data = open(path, 'rb')
size = data.seek(0, 2)
MiB = 1024 * 1024
fails = []

def read(off, n):
    data.seek(off)
    return data.read(n)

def check(ok, what):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        fails.append(what)

# --- the MBR
mbr = read(0, 512)
check(mbr[510:512] == b'\x55\xaa', "MBR signature 55 AA")
sig = struct.unpack_from('<I', mbr, 440)[0]
check(sig == 0x41504f54, "disk signature 0x%08x (want 0x41504f54)" % sig)
parts = []
for i in range(4):
    e = mbr[446 + 16 * i: 446 + 16 * (i + 1)]
    boot, ptype = e[0], e[4]
    start, count = struct.unpack_from('<II', e, 8)
    parts.append((boot, ptype, start * 512, count * 512))
want = [(0x0c, 64 * MiB, True), (0x83, 256 * MiB, False), (0x83, 256 * MiB, False), (0x07, 32 * MiB, False)]
for i, ((boot, ptype, off, length), (wtype, wlen, wboot)) in enumerate(zip(parts, want)):
    n = i + 1
    check(ptype == wtype, "p%d type 0x%02x (want 0x%02x)" % (n, ptype, wtype))
    check(length == wlen, "p%d is %d MiB (want %d)" % (n, length // MiB, wlen // MiB))
    check(off % MiB == 0, "p%d starts on a 1 MiB boundary (%d MiB)" % (n, off // MiB))
    check(off + length <= size, "p%d ends inside the image" % n)
    check((boot == 0x80) == wboot, "p%d bootable flag %s" % (n, "on" if boot == 0x80 else "off"))
for i in range(3):
    check(parts[i][2] + parts[i][3] <= parts[i + 1][2], "p%d ends before p%d starts" % (i + 1, i + 2))

# --- U-Boot's SPL at 8 KiB
SPL = 8 * 1024
hdr = read(SPL, 32)
check(hdr[4:12] == b'eGON.BT0', "eGON.BT0 at 8 KiB + 4 (the boot ROM's mark)")
checksum, length = struct.unpack_from('<II', hdr, 12)
check(length % 4 == 0 and 0 < length <= 64 * 1024, "SPL length %d bytes" % length)
body = bytearray(read(SPL, length))
struct.pack_into('<I', body, 12, 0x5F0A6C39)
total = sum(struct.unpack('<%dI' % (length // 4), bytes(body))) & 0xffffffff
check(total == checksum, "SPL checksum 0x%08x matches its header" % checksum)
check(read(0, 8 * 1024).count(0) == 8 * 1024 - 512 + mbr.count(0), "nothing but the MBR before 8 KiB")

# U-Boot proper: a FIT image (a flattened device tree) straight after the
# SPL (the H616 family's SPL is not padded: CONFIG_SPL_PAD_TO is 0).
fit = SPL + length
magic, fitsize = struct.unpack('>II', read(fit, 8))
check(magic == 0xd00dfeed, "U-Boot FIT image right after the SPL (8 KiB + %d KiB)" % (length // 1024))
end = fit + fitsize
check(end <= parts[0][2], "U-Boot ends at %d KiB, before p1 at %d KiB" % (end // 1024, parts[0][2] // 1024))
fitdata = read(fit, fitsize)
check(b'bl31' in fitdata or b'atf' in fitdata, "U-Boot's FIT holds Trusted Firmware (BL31)")
# U-Boot's start (uboot.fragment's CONFIG_PREBOOT): the green light, the
# backlight off for the reset pulse, PE6, the panel off and the backlight ON,
# then the tree (dtb.img, else screen-first.txt / screen-v2.txt, else PE6)
# and the report written to the boot partition.
uboot = read(8 * 1024, 4088 * 1024)
pre = re.search(rb'gpio clear PI12; mmc dev 0; mmc rescan; [^\0]*done; fi', uboot)
pre = pre.group(0).decode() if pre else ''
check(pre.startswith('gpio clear PI12; mmc dev 0; mmc rescan; setenv apotheca_first failed;')
      and 0 < pre.find('if fatwrite mmc 0:1 ${scriptaddr} uboot-started.txt ${filesize}; then setenv apotheca_first written; fi; gpio clear PD28;') < pre.find('gpio set PI15'),
      "U-Boot puts the green light out (red alone: U-Boot runs), looks at the card afresh, and writes uboot-started.txt before anything else")
spl = read(8 * 1024, 32 * 1024)
check(True, "the SPL is %d KiB" % (struct.unpack_from('<I', spl, 16)[0] // 1024))
check('if test -e mmc 0:1 dtb.img; then setenv fdtfile dtb.img;' in pre,
      "U-Boot takes dtb.img when the boot partition has one")
check('if gpio input PE6; then setenv apotheca_pe6 low; setenv apotheca_screen v2;' in pre
      and 'setenv fdtfile device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb' in pre
      and 'setenv fdtfile device_trees/sun50i-h700-anbernic-rg40xx-v.dtb' in pre
      and 0 < pre.find('gpio set PI14; sleep 0.1;') < pre.find('gpio input PE6'),
      "else it reads PE6 after the panel's reset pulse: low the v2 tree, high the first")
check('gpio clear PI14; gpio clear PI15; gpio set PD28;' in pre
      and pre.find('gpio input PE6') < pre.find('gpio clear PI15; gpio set PD28;'),
      "after the read: the panel off and the backlight ON (a lit, empty screen until Linux)")
check('test -e mmc 0:1 screen-first.txt' in pre and 'test -e mmc 0:1 screen-v2.txt' in pre
      and 0 < pre.find('if test ${apotheca_why} = PE6; then') < pre.find('screen-first.txt'),
      "screen-first.txt or screen-v2.txt on the boot partition overrides the pin (dtb.img overrides both)")
check('fatwrite mmc 0:1 ${scriptaddr} uboot-report.txt ${filesize};' in pre
      and pre.find('setenv fdtfile device_trees') < pre.find('env export -t'),
      "it writes uboot-report.txt to the boot partition, after the tree is chosen")
check('if itest.l *0x4d380000 == 0x43474244; then' in pre and 'mmc write 0x4d380000 0x1000 0x400;' in pre
      and 'fatwrite mmc 0:1 0x4d380000 kernel-from-memory.txt 0x80000;' in pre,
      "the kernel's messages left in memory (ramoops's console piece) are written to the card, bare at 2 MiB and as a file")
check(0x1000 * 512 >= end and (0x1000 + 0x400) * 512 <= parts[0][2],
      "those bare sectors (2 MiB to 2.5 MiB) are after U-Boot (%d KiB) and before p1 (%d KiB)" % (end // 1024, parts[0][2] // 1024))
check('if itest.l *0x4d200000 == 0x41504f43; then setexpr.l apotheca_resets *0x4d200004 + 1;' in pre
      and pre.endswith('if itest ${apotheca_resets} -ge 3; then echo apotheca: three resets in a row with no start between: not starting again; '
                       'mw.l 0x4d200000 0; i2c dev 0; i2c mw 0x34 0x27 1; i2c dev 1; i2c mw 0x34 0x27 1; '
                       'while true; do gpio toggle PI11; sleep 0.25; done; fi')
      and pre.find('uboot-report.txt ${filesize}') < pre.find('-ge 3'),
      "resets in a row are counted in memory; at three it does not start again (after the report is written)")
check(b'Power on by plug-in' not in uboot,
      "the SPL does not turn the power off for a start by the charger (AXP_DISABLE_BOOT_ON_POWERON is off)")

# --- p1: FAT boot partition
off = parts[0][2]
bs = read(off, 512)
check(bs[82:90] == b'FAT32   ', "p1 is FAT32")
if shutil.which('mdir') and shutil.which('mtype'):
    spec = '%s@@%d' % (path, off)
    env = {'MTOOLS_SKIP_CHECK': '1', 'PATH': '/usr/bin:/bin:/usr/local/bin'}
    def mt(*a):
        return subprocess.run(a, capture_output=True, text=True, env=env)
    inuse = mt('mtype', '-i', spec, '::dtb-in-use.txt').stdout.strip()
    auto = inuse.startswith('auto')
    for f in ['Image', 'extlinux/extlinux.conf', 'README.txt', 'dtb-in-use.txt',
              'device_trees/sun50i-h700-anbernic-rg40xx-v.dtb',
              'device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb',
              'device_trees/sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dtb',
              'device_trees/try/1-pins-40ma/dtb.img', 'device_trees/try/2-backlight-50khz/dtb.img',
              'device_trees/try/3-dither/dtb.img', 'device_trees/try/4-all-as-stock/dtb.img']:
        r = mt('mdir', '-b', '-i', spec, '::' + f)
        check(r.returncode == 0, "p1 holds %s" % f)
    conf = mt('mtype', '-i', spec, '::extlinux/extlinux.conf').stdout
    check('root=PARTUUID=41504f54-02' in conf, "extlinux.conf: root=PARTUUID=41504f54-02")
    check('FDTDIR /' in conf and 'LINUX /Image' in conf and 'FDT ' not in conf.replace('FDTDIR', ''),
          "extlinux.conf: LINUX /Image, FDTDIR / (the tree is $fdtfile, which U-Boot sets)")
    r = subprocess.run(['mtype', '-i', spec, '::dtb.img'], capture_output=True, env=env)
    if auto:
        check(r.returncode != 0, "no dtb.img: U-Boot picks the tree by the screen (dtb-in-use.txt: %s)" % inuse)
    else:
        check(r.stdout[:4] == b'\xd0\x0d\xfe\xed', "dtb.img is a device tree")
        named = subprocess.run(['mtype', '-i', spec, '::device_trees/' + inuse], capture_output=True, env=env)
        check(inuse.endswith('.dtb') and named.returncode == 0 and named.stdout == r.stdout,
              "dtb.img is device_trees/%s, as dtb-in-use.txt says" % inuse)
    # Every tree blinks the green light from the moment Linux has the LEDs
    # (the heartbeat post-image.sh puts in): the sign that Linux runs.
    blink = True
    kept = True
    for f in ['device_trees/sun50i-h700-anbernic-rg40xx-v.dtb', 'device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb',
              'device_trees/sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dtb'] + \
             ['device_trees/try/%s/dtb.img' % t for t in ['1-pins-40ma', '2-backlight-50khz', '3-dither', '4-all-as-stock']] + \
             ([] if auto else ['dtb.img']):
        d = subprocess.run(['mtype', '-i', spec, '::' + f], capture_output=True, env=env).stdout
        blink = blink and b'linux,default-trigger\0' in d and b'heartbeat\0' in d
        kept = kept and b'ramoops@4d300000\0' in d and b'apotheca-crumbs@4d200000\0' in d and b'console-size\0' in d
    check(blink, "every device tree on p1 blinks the power light once Linux runs (heartbeat)")
    check(kept, "every device tree gives the kernel its piece of memory to keep its messages in (ramoops), and U-Boot's count its own")
    conf2 = mt('mtype', '-i', spec, '::extlinux/extlinux.conf').stdout
    check(' panic=5' in conf2, "extlinux.conf: panic=5 (a kernel that panics resets, so U-Boot can save what it said)")
    kimage = subprocess.run(['mtype', '-i', spec, '::Image'], capture_output=True, env=env).stdout
    check(b'apotheca_deadline: nothing put the deadline off' in kimage and b'ramoops' in kimage,
          "the kernel has the deadline (patch 0171) and ramoops")
    readme = mt('mtype', '-i', spec, '::README.txt').stdout
    check(all(w in readme for w in ['screen-first.txt', 'uboot-started.txt', 'memory-kind.txt', 'uboot-report.txt', 'linux-report.txt', 'kernel-log.txt',
                                    'linux-early.txt', 'kernel-from-memory.txt', 'kernel-from-card.txt']),
          "README.txt tells of screen-first.txt and of every report file")
    tries = {}
    for t in ['1-pins-40ma', '2-backlight-50khz', '3-dither', '4-all-as-stock']:
        tries[t] = subprocess.run(['mtype', '-i', spec, '::device_trees/try/%s/dtb.img' % t],
                                  capture_output=True, env=env).stdout
    v2 = subprocess.run(['mtype', '-i', spec, '::device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb'],
                        capture_output=True, env=env).stdout
    panel_dir = os.path.join(sys.argv[3], 'os', 'board', 'rg40xxv', 'firmware', 'panels-to-try')
    dither = open(os.path.join(panel_dir, 'v2-dither.panel'), 'rb').read()
    stock_dither = open(os.path.join(panel_dir, 'v2-stock-dither.panel'), 'rb').read()
    check(all(d[:4] == b'\xd0\x0d\xfe\xed' and d != v2 for d in tries.values()),
          "the four trees to try are device trees, each changed from the v2 tree")
    fdtget = os.path.join(sys.argv[3], 'build-image', 'output', 'host', 'bin', 'fdtget')
    if os.access(fdtget, os.X_OK):
        with tempfile.TemporaryDirectory(prefix='apotheca-check-') as t:
            def prop(name, node, key):
                path = os.path.join(t, name + '.dtb')
                with open(path, 'wb') as f:
                    f.write(tries[name] if name in tries else v2)
                return subprocess.run([fdtget, path, node, key], capture_output=True, text=True).stdout.strip()
            pins = '/soc/pinctrl@300b000/lcd0-rgb888-pins'
            check(prop('1-pins-40ma', pins, 'drive-strength') == '40' and prop('v2', pins, 'drive-strength') == '',
                  "try 1 drives the RGB pins at 40 mA (the v2 tree sets nothing)")
            check(prop('2-backlight-50khz', '/backlight', 'pwms').split()[2:3] == ['20000']
                  and prop('v2', '/backlight', 'pwms').split()[2:3] == ['40000'],
                  "try 2's backlight PWM period is 20000 ns, 50 kHz (the v2 tree's 40000)")
            check(prop('4-all-as-stock', pins, 'drive-strength') == '40'
                  and prop('4-all-as-stock', '/backlight', 'pwms').split()[2:3] == ['20000'],
                  "try 4 has both too")
    else:
        print("skip  the trees' values (no fdtget in build-image/output/host/bin)")
    check(dither in tries['3-dither'] and stock_dither in tries['4-all-as-stock'],
          "tries 3 and 4 carry their panel files, byte for byte")
    r = subprocess.run(['mtype', '-i', spec, '::Image'], capture_output=True, env=env)
    check(r.stdout[56:60] == b'ARM\x64', "Image is an arm64 kernel")
    check(b'apotheca,panel-firmware' in r.stdout,
          "the kernel's panel driver takes a panel file from the device tree (patch 0170)")
    for f in ['anbernic,rg40xx-panel.panel', 'anbernic,rg40xx-v2-panel.panel', 'anbernic,rg40xx-v2-stock-panel.panel']:
        panel_bytes = open(os.path.join(sys.argv[3], 'os', 'board', 'rg40xxv', 'firmware', 'panels', f), 'rb').read()
        check(panel_bytes in r.stdout, "the kernel holds %s, byte for byte" % f)
else:
    print("skip  p1's files (mtools not installed)")

# --- p2: squashfs
off, plen = parts[1][2], parts[1][3]
sb = read(off, 96)
check(sb[0:4] == b'hsqs', "p2 is a squashfs")
used = struct.unpack_from('<Q', sb, 40)[0]
check(used <= plen, "p2's squashfs (%.1f MiB) fits its partition" % (used / MiB))
comp = struct.unpack_from('<H', sb, 20)[0]
check(comp == 5, "p2 is compressed with LZ4 (id %d)" % comp)
unsquashfs = shutil.which('unsquashfs') or (sys.argv[2] if os.access(sys.argv[2], os.X_OK) else None)
if unsquashfs:
    with tempfile.TemporaryDirectory(prefix='apotheca-check-') as t:
        root = os.path.join(t, 'root.squashfs')
        with open(root, 'wb') as f:
            f.write(read(off, used))
        listing = subprocess.run([unsquashfs, '-lls', root], capture_output=True, text=True).stdout
        def mode(name):
            for line in listing.splitlines():
                if line.endswith('squashfs-root' + name):
                    return line.split()[0]
            return ''
        def cat(name):
            return subprocess.run([unsquashfs, '-cat', root, name], capture_output=True).stdout
        for f in ['/usr/sbin/apotheca-power', '/usr/sbin/apotheca-keep', '/usr/sbin/apotheca-start',
                  '/etc/init.d/S95apotheca', '/usr/libexec/apotheca/run', '/usr/libexec/apotheca/fakepad']:
            check(mode(f).startswith('-rwx'), "p2 holds %s, executable (%s)" % (f, mode(f) or "missing"))
        inittab = cat('/etc/inittab').decode('utf-8', 'replace')
        check(b'screen-seen' in cat('/usr/sbin/apotheca-power') and b'no screen found' in cat('/usr/sbin/apotheca-power'),
              "apotheca-power turns off a screen nobody has seen working, and one that is not there")
        check(mode('/etc/init.d/S00early').startswith('-rwx') and b'linux-early.txt' in cat('/etc/init.d/S00early')
              and b'apotheca_deadline' in cat('/etc/init.d/S00early') and b'devmem 0x4d200000 32 0' in cat('/etc/init.d/S00early'),
              "S00early: puts the deadline off, clears U-Boot's count, writes linux-early.txt")
        check(b'kernel-from-card.txt' in cat('/etc/init.d/S90bootlog') and b'skip=4096 count=1024' in cat('/etc/init.d/S90bootlog'),
              "S90bootlog turns a kernel log U-Boot wrote onto the bare card into a file")
        check(b'linux-report.txt' in cat('/etc/init.d/S90bootlog'), "S90bootlog copies the boot log to the boot partition (linux-report.txt)")
        check('::respawn:/usr/sbin/apotheca-power' in inittab, "inittab starts apotheca-power (and again if it stops)")
        check('::respawn:/usr/sbin/apotheca-keep apotheca /usr/libexec/apotheca/run' in inittab,
              "inittab starts Apotheca under apotheca-keep")
        check('::shutdown:/etc/init.d/rcK' in inittab, "inittab runs rcK going down (S95apotheca stops Apotheca)")
        check(b'export APOTHECA_OFF_MARK=/run/apotheca/apotheca.off' in cat('/usr/libexec/apotheca/run'),
              "run hands Apotheca the off mark's place")
        check(b'export APOTHECA_OWNS_SCREEN=1' in cat('/usr/libexec/apotheca/run') and b'export APOTHECA_STICK_LIGHT=' in cat('/usr/libexec/apotheca/run'),
              "run gives Apotheca the backlight and the stick's light (app/lights.hpp)")
        check(b'APOTHECA_STICK_LIGHT' in cat('/usr/lib/apotheca/apotheca'), "the Apotheca on the card knows the stick's light")
        # The owner's boot log (Linux 7.2): ttyS2's device is the port below the UART.
        run = cat('/usr/libexec/apotheca/run').decode('utf-8', 'replace')
        pattern = re.search(r'^\s*(\S*5001400\.serial\S*)\)', run, re.M)
        found = []
        if pattern:
            for path in ['/sys/devices/platform/soc/5001400.serial/5001400.serial:0/5001400.serial:0.0',
                         '/sys/devices/platform/soc/5001400.serial',
                         '/sys/devices/platform/soc/5000000.serial/5000000.serial:0/5000000.serial:0.0']:
                r = subprocess.run(['sh', '-c', 'case "$1" in %s) echo yes ;; *) echo no ;; esac' % pattern.group(1), 'sh', path],
                                   capture_output=True, text=True)
                found.append(r.stdout.strip())
        check(found == ['yes', 'yes', 'no'],
              "run finds the stick light's serial line as Linux 7.2 places it, and as before (%s)" % found)
        check(b'KEEP_OFF:-poweroff' in cat('/usr/sbin/apotheca-keep'), "apotheca-keep turns the handheld off on the mark")
        check(b'APOTHECA_OFF_MARK' in cat('/usr/lib/apotheca/apotheca'), "the Apotheca on the card knows the off mark")
        check(any(l.endswith('/test_power.ko') for l in listing.splitlines()),
              "the kernel's made-up battery (test_power) is there for the desk test, as a module")
        check(any(l.endswith('/gpio-sim.ko') for l in listing.splitlines()),
              "the simulated GPIO chip (gpio-sim) is there for the desk test of the panel driver, as a module")
        start = cat('/usr/sbin/apotheca-start')
        check(b'a program copied onto the card' in start and b'program.md5' in start,
              "apotheca-start runs a program copied onto the card by hand (found by its sum)")
        check(b'7f454c460201????????????????????????b700' in start,
              "and runs it only if it is an arm64 program")
        check(b'apotheca.rolledback' in cat('/usr/libexec/apotheca/run'),
              "run does not fall back to the CPU for the program put back after a new one failed")
else:
    print("skip  p2's files (no unsquashfs)")

# --- p3: empty
off = parts[2][2]
check(read(off, MiB).count(0) == MiB, "p3 (second slot) starts empty")

# --- p4: zeros, made on the first boot
off = parts[3][2]
check(read(off, MiB).count(0) == MiB, "p4 (DATA) starts as zeros: the first boot makes it")

print()
if fails:
    print("%d check(s) failed" % len(fails))
    sys.exit(1)
print("every check passed")
EOF
