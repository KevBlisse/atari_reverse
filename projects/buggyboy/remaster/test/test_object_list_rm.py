"""test_object_list.py — remaster draw_object_list pixel-equivalence vs recreate's g_draw_object_list.

draw_object_list is the roadside-object display-list dispatcher: two nested loops walk the per-frame
object list and dispatch each object through obj_type_jumptable to a fine-x blit engine / handler.
Each case stages a mid-race frame, runs recreate's g_draw_object_list for each of draw_game_objects'
real passes (the two sprite passes split at the active-slot count, then the fixed-object pass) as the
reference and the remaster dispatcher on the same background, and asserts the whole framebuffer is
byte-identical.

The remaster dispatcher is pointer-independent (arena pointers + arena-relative cursors); the harness
drives it on a view of recreate's flat image (every arena pointer = the image base) so the exact
per-frame streams/records/jump-table are exercised — see equiv.compare_object_list.
"""
import os
from pathlib import Path

import equiv
import pytest

# Frames spanning the legs / warmups where the object passes draw a variety of handlers (probed to
# each contribute hundreds-to-thousands of object-list bytes).
CASES = [(0, 60), (1, 90), (2, 120), (3, 120), (4, 60), (4, 90)]


@pytest.mark.parametrize("leg,warmup", CASES)
def test_draw_object_list_matches(leg, warmup, capsys):
    lib = equiv._lib()
    image = equiv.object_background(leg=leg, warmup=warmup)
    diff, footprint = equiv.compare_object_list(lib, image)
    with capsys.disabled():
        print(f"  leg={leg} warmup={warmup}: footprint={footprint} diff={diff}")
    assert footprint > 0, f"draw_object_list drew nothing (leg={leg}, warmup={warmup})"
    assert diff == 0, f"draw_object_list differs from recreate in {diff} bytes (leg={leg}, warmup={warmup})"


def test_first_gantry_approach_matches_recreate(capsys):
    """Differential check of the actual failing approach, not a synthetic warmup.

    BB_GANTRY_STATE must point to a *recreate-format* 1 MiB flat memory image
    captured immediately before the object-list drawing passes at the first
    finishing straight (the state in which the Windows build shows LEG 4).
    It must include graphics, object records, display lists, and screen state.
    A Windows framebuffer dump alone is NOT a valid input: the reference
    renderer needs the original Atari address-space image.
    """
    filename = os.environ.get("BB_GANTRY_STATE")
    if not filename:
        pytest.skip("Set BB_GANTRY_STATE to a captured 1 MiB pre-object-list Atari image")
    image = Path(filename).read_bytes()
    assert len(image) == equiv.bench_frame.IMAGE_SIZE, (
        f"Expected a full {equiv.bench_frame.IMAGE_SIZE}-byte Atari memory image, "
        f"got {len(image)} bytes; a framebuffer-only capture cannot be compared"
    )
    lib = equiv._lib()
    diff, footprint = equiv.compare_object_list(lib, bytearray(image))
    with capsys.disabled():
        print(f"  first-gantry approach: footprint={footprint} diff={diff}")
    assert footprint > 0, "The captured state produced no object pixels; check capture timing"
    assert diff == 0, (
        f"First-gantry object-list rendering differs from recreate in {diff} framebuffer bytes"
    )


def test_first_gantry_fixed_pass_matches_recreate(capsys):
    """The exact gantry pass, using a pre-fixed-pass Atari-format capture.

    This is intentionally distinct from BB_GANTRY_STATE, which is the earlier
    pre-object-list snapshot used by test_first_gantry_approach_matches_recreate.
    Do not feed a Windows-native struct dump or a framebuffer-only file here.
    """
    filename = os.environ.get("BB_GANTRY_FIXED_STATE")
    if not filename:
        pytest.skip("Set BB_GANTRY_FIXED_STATE to a 1 MiB pre-fixed-pass Atari image")
    image = Path(filename).read_bytes()
    assert len(image) == equiv.bench_frame.IMAGE_SIZE, (
        f"Expected {equiv.bench_frame.IMAGE_SIZE} bytes, got {len(image)}"
    )
    diff, footprint = equiv.compare_fixed_object_pass(equiv._lib(), bytearray(image))
    with capsys.disabled():
        print(f"  first-gantry fixed pass: footprint={footprint} diff={diff}")
    assert footprint > 0, "Fixed-object pass did not draw; check capture timing and view"
    assert diff == 0, f"First-gantry fixed pass differs in {diff} framebuffer bytes"
