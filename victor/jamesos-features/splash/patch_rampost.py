#!/usr/bin/env python3
"""Swap the startup picture inside Vector's rampost program.

    patch_rampost.py ORIGINAL_RAMPOST WIREOS_PICTURE.raw NEW_PICTURE.raw OUTPUT

rampost keeps its 184x96 picture as 35328 raw bytes inside the program. This finds that exact
picture and replaces it with ours (same size), so nothing else in the program moves.
It refuses to write anything if the WireOS picture isn't found exactly once.
"""
import sys

orig_path, old_pic_path, new_pic_path, out_path = sys.argv[1:5]
data = open(orig_path, "rb").read()
old = open(old_pic_path, "rb").read()
new = open(new_pic_path, "rb").read()
if len(old) != 35328 or len(new) != 35328:
    sys.exit("picture files must be exactly 35328 bytes (184x96 RGB565)")
pos = data.find(old)
if pos < 0:
    sys.exit("couldn't find the WireOS picture inside rampost (different WireOS version?) - nothing changed")
if data.find(old, pos + 1) >= 0:
    sys.exit("found the WireOS picture more than once - not sure which to change, nothing changed")
patched = data[:pos] + new + data[pos + len(old):]
assert len(patched) == len(data)
open(out_path, "wb").write(patched)
print("picture found at byte %d, replaced" % pos)
