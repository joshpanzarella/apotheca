#!/usr/bin/env python3
"""make-v2-stock-panel.py: writes board/rg40xxv/firmware/panels/anbernic,rg40xx-v2-stock-panel.panel
(and, from the same two, board/rg40xxv/firmware/panels-to-try/: see the end)

ROCKNIX's v2 panel file (anbernic,rg40xx-v2-panel.panel) with its start-up
commands untouched and two things set as Anbernic's own system sets them for
the RG40XX's screen (the lcd0 node of the stock boot package's device tree, as
muOS ships it at commit a55cdbe; it is the same node for both screens):

  pixel clock edge   lcd_hv_clk_phase 2: Allwinner's TCON code makes that
                     "clk_inv = 1", the bit Linux's sun4i TCON sets for
                     DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE. ROCKNIX's v2 file
                     asks for POSEDGE (and SYNC_DRIVE_POSEDGE, which sun4i
                     does not use); its first-screen file asks for NEGEDGE.
  timings            lcd_dclk_freq 24 MHz, lcd_ht 768, lcd_hbp 46, lcd_hspw 20,
                     lcd_vt 521, lcd_vbp 15, lcd_vspw 4 (Allwinner's back porch
                     counts from the start of sync): front porch 82, sync 20,
                     back porch 26; 26, 4, 11; 59.98 Hz. ROCKNIX's: 27 MHz,
                     64/4/42, 100/4/16, 60 Hz.

The sync polarities (lcd_hv_sync_polarity 3: both active high) and the DE
polarity already agree with ROCKNIX's v2 file and are kept.

Run from anywhere; it reads and writes beside itself. The output is committed.
"""
import os, struct

here = os.path.dirname(os.path.abspath(__file__))
panels = os.path.join(here, "..", "board", "rg40xxv", "firmware", "panels")
src = open(os.path.join(panels, "anbernic,rg40xx-v2-panel.panel"), "rb").read()

HEADER = 16
assert src[:15] == b"PANEL-FIRMWARE\0" and src[15] == 1
config = bytearray(src[HEADER:HEADER + 48])
assert config[47] == 1, "ROCKNIX's v2 file has one timing"
timing = bytearray(src[HEADER + 48:HEADER + 48 + 32])
commands = src[HEADER + 48 + 32:]

DE_HIGH, PIXDATA_DRIVE_NEGEDGE = 1 << 1, 1 << 3
assert struct.unpack(">I", config[40:44])[0] == 0x46   # DE_HIGH | PIXDATA_DRIVE_POSEDGE | SYNC_DRIVE_POSEDGE
struct.pack_into(">I", config, 40, DE_HIGH | PIXDATA_DRIVE_NEGEDGE)

PHSYNC, PVSYNC = 1 << 0, 1 << 2
assert struct.unpack(">8HII", timing[:24]) == (640, 64, 4, 42, 480, 100, 4, 16, 27000, PHSYNC | PVSYNC)
struct.pack_into(">8HII", timing, 0, 640, 82, 20, 26, 480, 26, 4, 11, 24000, PHSYNC | PVSYNC)

out = os.path.join(panels, "anbernic,rg40xx-v2-stock-panel.panel")
open(out, "wb").write(src[:HEADER] + bytes(config) + bytes(timing) + commands)
print(out)

# Panel files to try, not built into the kernel: post-image.sh puts each into
# a device tree (apotheca,panel-firmware, patch 0170) under
# device_trees/try/ on the boot partition. Their pixel format is
# RGB666_PACKED, an 18-bit bus, for which the display controller dithers, as
# Anbernic's own system does (lcd_frm 1); patch 0170 reads it.
RGB666_PACKED = 2
tries = os.path.join(here, "..", "board", "rg40xxv", "firmware", "panels-to-try")
os.makedirs(tries, exist_ok=True)
for name, cfg, tim in (("v2-dither.panel", bytearray(src[HEADER:HEADER + 48]), src[HEADER + 48:HEADER + 48 + 32]),
                       ("v2-stock-dither.panel", bytearray(config), bytes(timing))):
    assert struct.unpack(">H", cfg[34:36])[0] == 0            # RGB888 in both
    struct.pack_into(">H", cfg, 34, RGB666_PACKED)
    out = os.path.join(tries, name)
    open(out, "wb").write(src[:HEADER] + bytes(cfg) + bytes(tim) + commands)
    print(out)
