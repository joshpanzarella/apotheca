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
import os, struct, subprocess, shutil, sys, tempfile

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

# --- p1: FAT boot partition
off = parts[0][2]
bs = read(off, 512)
check(bs[82:90] == b'FAT32   ', "p1 is FAT32")
if shutil.which('mdir') and shutil.which('mtype'):
    spec = '%s@@%d' % (path, off)
    env = {'MTOOLS_SKIP_CHECK': '1', 'PATH': '/usr/bin:/bin:/usr/local/bin'}
    def mt(*a):
        return subprocess.run(a, capture_output=True, text=True, env=env)
    for f in ['Image', 'dtb.img', 'extlinux/extlinux.conf', 'README.txt', 'dtb-in-use.txt',
              'device_trees/sun50i-h700-anbernic-rg40xx-v.dtb',
              'device_trees/sun50i-h700-anbernic-rg40xx-v-v2-panel.dtb',
              'device_trees/sun50i-h700-anbernic-rg40xx-v-v2-stock-panel.dtb']:
        r = mt('mdir', '-b', '-i', spec, '::' + f)
        check(r.returncode == 0, "p1 holds %s" % f)
    conf = mt('mtype', '-i', spec, '::extlinux/extlinux.conf').stdout
    check('root=PARTUUID=41504f54-02' in conf, "extlinux.conf: root=PARTUUID=41504f54-02")
    check('FDT /dtb.img' in conf and 'LINUX /Image' in conf, "extlinux.conf: LINUX /Image, FDT /dtb.img")
    r = subprocess.run(['mtype', '-i', spec, '::dtb.img'], capture_output=True, env=env)
    check(r.stdout[:4] == b'\xd0\x0d\xfe\xed', "dtb.img is a device tree")
    inuse = mt('mtype', '-i', spec, '::dtb-in-use.txt').stdout.strip()
    named = subprocess.run(['mtype', '-i', spec, '::device_trees/' + inuse], capture_output=True, env=env)
    check(inuse.endswith('.dtb') and named.returncode == 0 and named.stdout == r.stdout,
          "dtb.img is device_trees/%s, as dtb-in-use.txt says" % inuse)
    r = subprocess.run(['mtype', '-i', spec, '::Image'], capture_output=True, env=env)
    check(r.stdout[56:60] == b'ARM\x64', "Image is an arm64 kernel")
    check(b'apotheca,panel-firmware' in r.stdout,
          "the kernel's panel driver takes a panel file from the device tree (patch 0170)")
    for f in ['anbernic,rg40xx-panel.panel', 'anbernic,rg40xx-v2-panel.panel', 'anbernic,rg40xx-v2-stock-panel.panel']:
        data = open(os.path.join(sys.argv[3], 'os', 'board', 'rg40xxv', 'firmware', 'panels', f), 'rb').read()
        check(data in r.stdout, "the kernel holds %s, byte for byte" % f)
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
        check('::respawn:/usr/sbin/apotheca-power' in inittab, "inittab starts apotheca-power (and again if it stops)")
        check('::respawn:/usr/sbin/apotheca-keep apotheca /usr/libexec/apotheca/run' in inittab,
              "inittab starts Apotheca under apotheca-keep")
        check('::shutdown:/etc/init.d/rcK' in inittab, "inittab runs rcK going down (S95apotheca stops Apotheca)")
        check(b'export APOTHECA_OFF_MARK=/run/apotheca/apotheca.off' in cat('/usr/libexec/apotheca/run'),
              "run hands Apotheca the off mark's place")
        check(b'export APOTHECA_OWNS_SCREEN=1' in cat('/usr/libexec/apotheca/run') and b'export APOTHECA_STICK_LIGHT=' in cat('/usr/libexec/apotheca/run'),
              "run gives Apotheca the backlight and the stick's light (app/lights.hpp)")
        check(b'APOTHECA_STICK_LIGHT' in cat('/usr/lib/apotheca/apotheca'), "the Apotheca on the card knows the stick's light")
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
