import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent

CLEAR_OF = [
    "speaker",
    "devkit",
    "antenna",
    "amp",
    "cap",
    "mic_l",
    "mic_r",
    "button",
    "filament",
    "filament_tails",
    "transistor",
    "speaker_screws",
    "metal_washers",
    "top_screws",
    "inserts",
    "top",
    "top_gasket",
    "body",
]


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def _dir(az):
    a = np.radians(az)
    return np.array([np.cos(a), np.sin(a), 0.0])


def _axis_samples(p, count=41, margin=0.5):
    out = []
    for az in np.linspace(90 - p["fil_ring_half"] + margin, 90 + p["fil_ring_half"] - margin, count):
        out.append((p["filament_r"] * _dir(az) + [0, 0, p["fil_z"]], _dir(az)))
    return out


def _exit_after(m, origin, d, first):
    d = np.asarray(d, dtype=float) / np.linalg.norm(d)
    nxt = first_hit(m, np.asarray(origin) + (first + 0.01) * d, d)
    return None if nxt is None else first + 0.01 + nxt


def printed_top(h, layer, first=0.2):
    t = first
    while t + layer / 2 < h - 1e-9:
        t += layer
    return t


def test_ring_arc_watertight_one_body_fits_bed_on_its_top_face():
    p = params()
    m = mesh("ring_arc")
    assert m.is_watertight
    assert len(_manifold(m).decompose()) == 1
    assert m.volume > 0
    assert m.extents[0] < p["bed"] and m.extents[1] < p["bed"]
    assert abs(m.bounds[0][2]) < 1e-6
    assert m.bounds[1][2] == pytest.approx(p["ring_arc_h"] + p["ring_lip_h"], abs=1e-4)
    bed = m.vertices[m.vertices[:, 2] < 1e-6]
    r = np.hypot(bed[:, 0], bed[:, 1])
    assert r.min() == pytest.approx(p["ring_in_r"], abs=0.02)
    assert r.max() == pytest.approx(p["ring_outer_r"], abs=0.02)


def test_ring_arc_print_is_the_flipped_assembly_arc():
    p = params()
    a = mesh("asm_ring_arc")
    b = mesh("ring_arc")
    assert a.volume == pytest.approx(b.volume, rel=1e-6)
    assert a.bounds[1][2] == pytest.approx(p["ring_top_z"], abs=1e-4)
    assert a.bounds[0][2] == pytest.approx(p["ring_z0"] - p["ring_lip_h"], abs=1e-4)
    assert np.hypot(a.vertices[:, 0], a.vertices[:, 1]).max() <= p["case_r"] + 0.01


@pytest.mark.parametrize("name", CLEAR_OF)
def test_ring_clear(name):
    assert overlap("asm_ring_arc", f"asm_{name}") < TOL


def test_filament_sits_in_the_arc_pocket_behind_a_thin_front_wall():
    p = params()
    m = mesh("asm_ring_arc")
    assert overlap("asm_ring_arc", "asm_filament") < TOL
    for origin, rad in _axis_samples(p):
        for tilt in (-0.3, 0.0, 0.3):
            d = rad + [0, 0, tilt]
            first = first_hit(m, origin, d)
            assert first is not None
            last = _exit_after(m, origin, d, first)
            assert last is not None
            assert first_hit(m, origin + (last + 0.01) * d / np.linalg.norm(d), d) is None
            assert (last - first) * np.cos(np.arctan(tilt)) == pytest.approx(p["ring_front_t"], abs=0.05)
        for d in ((0, 0, 1), -rad, -rad + [0, 0, 1], rad + [0, 0, 1]):
            t = first_hit(m, origin, d)
            assert t is not None
            assert p["fil_d"] / 2 + 0.05 <= t <= p["ring_pocket_r"] + 0.01
        assert first_hit(m, origin, (0, 0, -1)) is None
    assert p["ring_front_wall"] == pytest.approx(0.6)
    assert p["ring_above_t"] >= 0.55 and p["ring_below_t"] >= 0.55


def test_slit_opens_down_onto_the_wall_top():
    p = params()
    body = mesh("asm_body")
    mid = p["filament_r"] + (p["ring_slit_out"] - p["ring_slit_in"]) / 2
    for origin, rad in _axis_samples(p, count=9):
        q = mid * rad + [0, 0, p["ring_z0"] + 0.3]
        t = first_hit(body, q, (0, 0, -1))
        assert t == pytest.approx(0.3, abs=1e-3)
    assert p["inner_r"] < p["filament_r"] - p["ring_slit_in"] - 0.5
    assert p["ring_neck_w"] < p["fil_d"]


def test_arc_height_matches_the_bridge_of_the_printed_body():
    p = params()
    base = p["body_bot_z"]
    layer = p["body_layer"]
    assert layer == pytest.approx(0.16)
    bridge = printed_top(p["top_z0"] - base, layer) - printed_top(p["ring_z0"] - base, layer)
    assert bridge == pytest.approx(2.88)
    assert p["ring_arc_h"] == pytest.approx(bridge)
    assert 0 <= printed_top(p["ring_arc_h"], layer) - bridge <= 0.05
    for dz in (-0.015, 0.015):
        moved = printed_top(p["top_z0"] - base + dz, layer) - printed_top(p["ring_z0"] - base + dz, layer)
        assert moved == pytest.approx(bridge)
    assert p["fil_z"] == pytest.approx(p["ring_z0"] + p["ring_arc_h"] / 2)


def test_ring_gap_is_centred_on_the_back():
    p = params()
    for part in ("asm_ring_arc", "asm_filament"):
        v = mesh(part).vertices
        rel = (np.degrees(np.arctan2(v[:, 1], v[:, 0])) - 90 + 180) % 360 - 180
        lo, hi = rel.min(), rel.max()
        assert (lo + hi) / 2 == pytest.approx(0, abs=1.0), part
        assert hi == pytest.approx(p["fil_ring_half"], abs=0.05), part
    assert p["fil_end_az"] == pytest.approx(90 + p["fil_ring_half"])
    assert (p["bridge_a0"] + p["bridge_a1"]) / 2 == pytest.approx(270, abs=1.0)


def _circumradius(a, b, c):
    ab, bc, ca = np.linalg.norm(b - a), np.linalg.norm(c - b), np.linalg.norm(a - c)
    area = np.linalg.norm(np.cross(b - a, c - a)) / 2
    return np.inf if area < 1e-12 else ab * bc * ca / (4 * area)


@pytest.mark.parametrize("key", ["fil_tail_pts", "fil_tail_pts_m"])
def test_filament_ends_bend_gently_through_the_slots_into_the_inside(key):
    p = params()
    pts = np.asarray(p[key])
    n = int(p["fil_bend_steps"])
    assert np.hypot(*pts[0][:2]) == pytest.approx(p["filament_r"], abs=1e-3)
    az0 = np.degrees(np.arctan2(pts[0][1], pts[0][0])) % 360
    ends = sorted([p["fil_end_az"] % 360, (180 - p["fil_end_az"]) % 360])
    assert min(abs(az0 - e) for e in ends) < 1e-3
    tangent = np.array([-pts[0][1], pts[0][0]]) / np.hypot(pts[0][0], pts[0][1])
    first = (pts[1] - pts[0])[:2]
    cosang = abs(np.dot(tangent, first)) / np.linalg.norm(first)
    assert np.degrees(np.arccos(min(1.0, cosang))) <= p["fil_turn"] / n / 2 + 0.01
    for i in range(1, n):
        assert _circumradius(pts[i - 1], pts[i], pts[i + 1]) >= p["fil_bend_r_min"]
    bend = sum(np.linalg.norm(pts[i + 1] - pts[i]) for i in range(n))
    assert bend == pytest.approx(p["fil_bend_len"], rel=0.01)
    s0, s1, s2 = pts[n], pts[n + 1], pts[n + 2]
    assert np.linalg.norm(np.cross(s1 - s0, s2 - s0)) / (np.linalg.norm(s1 - s0) * np.linalg.norm(s2 - s0)) < 1e-4
    assert np.linalg.norm(s2 - s0) == pytest.approx(p["fil_straight"], abs=1e-3)
    assert p["fil_straight"] >= p["fil_trans"] + p["fil_contact"]
    assert np.linalg.norm(s1 - s0) == pytest.approx(p["fil_trans"], abs=1e-3)
    assert np.hypot(*s2[:2]) + p["fil_d"] / 2 <= p["inner_r"] - 1.0
    assert np.hypot(*s1[:2]) + p["fil_d"] / 2 <= p["inner_r"] + 1e-3
    assert np.allclose(pts[:, 2], p["fil_z"])
    ring_len = 2 * np.pi * p["filament_r"] * p["fil_ring_deg"] / 360
    assert ring_len + 2 * (p["fil_bend_len"] + p["fil_trans"]) == pytest.approx(p["fil_lit"], abs=1e-3)
    assert p["fil_bend_r"] >= p["fil_bend_r_min"] + (p["chan_d"] - p["fil_d"]) - 1e-9


@pytest.mark.parametrize("name", ["body", "ring_arc", "top", "top_gasket", "inserts", "speaker", "button",
                                  "top_screws", "cap", "mic_l", "mic_r"])
def test_filament_tails_clear(name):
    assert overlap("asm_filament_tails", f"asm_{name}") < TOL


def test_tail_slots_are_open_from_above_for_laying_the_filament_in():
    p = params()
    assert overlap("asm_tail_drop_probe", "asm_body") < TOL
    assert mesh("asm_tail_drop_probe").bounds[1][2] >= p["top_z0"] + 1.0
    assert p["fil_col_clr"] >= 0.5


def test_nothing_opaque_in_front_of_the_lit_arc():
    p = params()
    ring = mesh("asm_ring_arc")
    others = [mesh(n) for n in ("asm_body", "asm_top", "asm_top_gasket", "asm_speaker", "asm_button", "asm_top_screws")]
    for origin, rad in _axis_samples(p, count=31):
        for tilt in (-0.6, -0.3, 0.0, 0.3, 0.6):
            d = rad + [0, 0, tilt]
            for m in others:
                assert first_hit(m, origin, d) is None
            assert first_hit(ring, origin, d) is not None


def test_ring_lip_locates_on_the_inner_wall():
    p = params()
    v = mesh("asm_ring_arc").vertices
    low = v[v[:, 2] < p["ring_z0"] - 0.01]
    r = np.hypot(low[:, 0], low[:, 1])
    assert p["ring_z0"] - low[:, 2].min() == pytest.approx(p["ring_lip_h"], abs=1e-3)
    assert p["inner_r"] - r.max() == pytest.approx(p["ring_lip_clr"], abs=0.02)
    assert r.min() == pytest.approx(p["ring_in_r"], abs=0.02)
    assert p["ring_in_r"] <= p["top_skirt_in_r"] - 0.3


def test_ring_overhangs():
    m = mesh("ring_arc")
    assert overhang_area(m) < 20


def test_build_lists_one_white_arc():
    text = (CASE_DIR / "build.sh").read_text()
    assert '"ring_arc:PETG-white_ring_arc"' in text
    assert "ring_arc_x" not in text
