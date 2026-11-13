"""Convert Windows gantry .bbcap into an Atari-addressed reference image.

Requires a *real prepared* recreate 1 MiB baseline from the same game assets.
This converter refuses missing baselines; native pointers are never treated as
Atari addresses. Run from remaster/test, where adapter.py is importable.
"""
import argparse
import struct
from pathlib import Path
import adapter

SIZE = 0x100000
def u16(b, off):
    return int.from_bytes(b[off:off + 2], "big")
def u32(b, off):
    return int.from_bytes(b[off:off + 4], "big")
def w16(b, off, value):
    b[off:off + 2] = (value & 0xffff).to_bytes(2, "big")
def write(b, off, payload):
    if off < 0 or off + len(payload) > len(b):
        raise ValueError(f"section outside Atari image: {off:#x} + {len(payload):#x}")
    b[off:off + len(payload)] = payload

def read_bundle(path):
    raw = Path(path).read_bytes()
    if raw[:8] != b"BBCAP01\x00":
        raise ValueError("not a BBCAP01 bundle")
    sections = {}
    pos = 8
    while pos < len(raw):
        if pos + 8 > len(raw):
            raise ValueError("truncated section header")
        tag = raw[pos:pos + 4].decode("ascii")
        size = int.from_bytes(raw[pos + 4:pos + 8], "little")
        pos += 8
        if tag in sections or pos + size > len(raw):
            raise ValueError(f"duplicate/truncated section {tag}")
        sections[tag] = raw[pos:pos + size]
        pos += size
    required = {"META", "FB00", "BUFA", "BUFC", "LIST", "FLAG",
                "XOFF", "COLR", "VIEW", "PFXT", "JUMP"}
    if set(sections) != required:
        raise ValueError(f"bundle sections mismatch: missing {required - set(sections)}")
    if len(sections["META"]) != 24 or len(sections["FB00"]) != 32000:
        raise ValueError("bad metadata/framebuffer size")
    return sections

def convert(bundle, baseline):
    sections = read_bundle(bundle)
    img = bytearray(Path(baseline).read_bytes())
    if len(img) != SIZE:
        raise ValueError("baseline must be an actual 1 MiB recreate memory image")
    buf_a = u32(img, adapter.A_buf_a)
    buf_c = u32(img, adapter.A_buf_c)
    flip = u16(img, adapter.A_flip_idx)
    draw_buf = u32(img, adapter.A_physbase_tbl + flip)
    if draw_buf != adapter.SCREEN_BASE:
        raise ValueError("baseline draw buffer differs from SCREEN_BASE; cannot safely compare")
    frame, view, parity, bonus, scan, p24 = struct.unpack("<6I", sections["META"])
    write(img, buf_a, sections["BUFA"])
    write(img, buf_c, sections["BUFC"])
    # These are immutable program tables. Never overwrite neighbouring globals\n    # with a generous diagnostic read: require them to match the oracle image.\n    for tag, off in [("LIST", adapter.A_obj_list_base),\n                     ("COLR", adapter.A_color_pairs),\n                     ("VIEW", adapter.A_obj_view_xform),\n                     ("PFXT", adapter.A_objsh2p_tbl),\n                     ("JUMP", adapter.A_obj_type_jumptable)]:\n        data = sections[tag]\n        if img[off:off + len(data)] != data:\n            raise ValueError(f"{tag} static table differs from prepared Atari reference")
    if len(sections["FLAG"]) != 14 * 32:\n        raise ValueError("invalid serialized ring grid")\n    write(img, adapter.A_ring_base, sections["FLAG"])
    # Road control table: 106 longs plus 8-byte spill; xoff begins 42\n    # bytes into it. Older BBCAP01 files contain an overlong 1024-byte\n    # diagnostic read; retain only the allocated 390-byte region.\n    xoff_size = 106 * 4 + 8 - 0x28 - 2\n    if len(sections["XOFF"]) < xoff_size:\n        raise ValueError("truncated XOFF control table")\n    write(img, adapter.A_obj_xoff_tbl, sections["XOFF"][:xoff_size])




    write(img, adapter.SCREEN_BASE, sections["FB00"])
    for off, val in [(adapter.A_view_flags, view),
                     (adapter.A_view_parity, parity),
                     (adapter.A_bonus_timer, bonus),
                     (adapter.A_obj_scan_off, scan)]:
        w16(img, off, val)
    img[adapter.A_p24_flag] = p24 & 255
    return img, frame

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bundle")
    ap.add_argument("baseline", help="genuine prepared Atari 1 MiB image (not a zero template)")
    ap.add_argument("output")
    args = ap.parse_args()
    img, frame = convert(args.bundle, args.baseline)
    Path(args.output).write_bytes(img)
    print(f"frame={frame} wrote {args.output}; run BB_GANTRY_FIXED_STATE={args.output} pytest -q test_object_list_rm.py")

if __name__ == "__main__":
    main()
