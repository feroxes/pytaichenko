import functools

import manifold3d
import numpy as np
import pytest

from conftest import CASE_DIR, _manifold, mesh, overhang_area, overlap, params

TOL = 1e-3

CAP_CLEAR_OF = [
    "body",
    "button",
    "filament",
    "devkit",
    "usb_plug",
    "speaker",
    "amp",
    "cap",
    "mic",
    "tray_screws",
    "top_screws",
    "speaker_screws",
]

MIN_TRAVEL = 0.5
MIN_LIFT = 0.5
MIN_RETENTION = 0.5
OVERHANG_MAX = 5


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def _button_xy():
    p = params()
    a = np.radians(p["button_az"])
    return p["button_r"] * np.cos(a), p["button_r"] * np.sin(a)


def _plate(hole_d=None):
    p = params()
    bx, by = _button_xy()
    t = p["top_plate_t"]
    disc = manifold3d.Manifold.cylinder(t, 70, 70, 96).translate([0, 0, p["top_underside_z"]])
    d = p["button_hole_d"] if hole_d is None else hole_d
    hole = manifold3d.Manifold.cylinder(t + 2, d / 2, d / 2, 96).translate(
        [bx, by, p["top_underside_z"] - 1]
    )
    return disc - hole


def _area_at(part, z):
    return float(_solid(part).slice(z).area())


@pytest.mark.parametrize("name", ["button_cap", "foot", "top_plug"])
def test_small_parts_watertight_fit_bed(name):
    m = mesh(name)
    assert m.is_watertight
    assert m.volume > 0
    bed = params()["bed"]
    assert m.extents[0] < bed and m.extents[1] < bed
    assert abs(m.bounds[0][2]) < 1e-6


def test_button_cap_clear():
    p = params()
    assert overlap("asm_button_cap", "asm_button") < TOL
    body_top = p["btn_z0"] + p["btn_body_h"]
    travel = mesh("asm_button_cap").bounds[0][2] - body_top
    assert travel >= MIN_TRAVEL
    assert mesh("asm_button").bounds[1][2] == pytest.approx(p["btn_tip_z"], abs=1e-6)


@pytest.mark.parametrize("name", CAP_CLEAR_OF)
def test_button_cap_clear_of(name):
    assert overlap("asm_button_cap", f"asm_{name}") < TOL


def _loops(part, z):
    return [np.asarray(q) for q in _solid(part).slice(z).to_polygons()]


def test_button_cap_sits_on_the_pusher():
    p = params()
    bx, by = _button_xy()
    cap = _solid("asm_button_cap")
    assert mesh("asm_button_cap").bounds[1][2] == pytest.approx(p["H"], abs=1e-6)
    ceiling = p["btn_tip_z"]
    s = 0.1
    probe = manifold3d.Manifold.cube([s, s, s], True)
    above = cap ^ probe.translate([bx, by, ceiling + 0.2])
    below = cap ^ probe.translate([bx, by, ceiling - 0.2])
    assert float(above.volume()) / s ** 3 > 0.9
    assert float(below.volume()) < TOL
    pocket_r = min(np.linalg.norm(q - [bx, by], axis=1).min() for q in _loops("asm_button_cap", ceiling - 0.2))
    assert p["btn_pusher_d"] / 2 + 0.1 < pocket_r < p["btn_pusher_d"] / 2 + 0.5


def test_button_cap_stem_in_hole():
    p = params()
    bx, by = _button_xy()
    stem_d = p["button_hole_d"] - 2 * p["clr"]
    for z in (p["top_underside_z"] + 0.5, p["H"] - 1.0):
        loops = _loops("asm_button_cap", z)
        assert len(loops) == 1
        assert _area_at("asm_button_cap", z) == pytest.approx(np.pi * stem_d ** 2 / 4, rel=0.01)
        c = loops[0].mean(axis=0)
        assert np.hypot(c[0] - bx, c[1] - by) < 0.05


def test_button_cap_fits_the_plate():
    cap = _solid("asm_button_cap")
    assert float((cap ^ _plate()).volume()) < TOL
    for dz in (MIN_LIFT, -MIN_TRAVEL):
        assert float((cap.translate([0, 0, dz]) ^ _plate()).volume()) < TOL


def test_button_cap_cannot_fall_out():
    p = params()
    bx, by = _button_xy()
    hole_r = p["button_hole_d"] / 2
    cap = _solid("asm_button_cap")
    flange_r = max(
        np.linalg.norm(q - [bx, by], axis=1).max()
        for z in np.arange(p["btn_tip_z"] - 1.4, p["btn_tip_z"] - 0.6, 0.1)
        for q in _loops("asm_button_cap", z)
    )
    assert flange_r >= hole_r + MIN_RETENTION
    assert p["top_underside_z"] - p["btn_tip_z"] >= MIN_LIFT
    pulled = cap.translate([0, 0, p["top_underside_z"] - p["btn_tip_z"] + 0.8])
    assert float((pulled ^ _plate()).volume()) > 0.0


def test_button_cap_print_orientation():
    p = params()
    a = mesh("asm_button_cap")
    b = mesh("button_cap")
    assert b.volume == pytest.approx(a.volume, rel=1e-6)
    assert b.extents[2] == pytest.approx(a.extents[2], abs=1e-6)
    assert b.bounds[0][2] == pytest.approx(0.0, abs=1e-6)
    assert np.allclose((b.bounds[0][:2] + b.bounds[1][:2]) / 2, [0, 0], atol=0.05)
    assert max(b.extents[:2]) < p["bed"]
    assert _area_at("button_cap", 0.05) > 0.8 * _area_at("button_cap", 1.0)


def test_button_cap_overhangs():
    assert overhang_area(mesh("button_cap")) < OVERHANG_MAX


def _foot_area(p):
    return p["foot_w"] * p["foot_l"] - (4 - np.pi) * p["foot_corner_r"] ** 2


def test_foot_size():
    p = params()
    m = mesh("foot")
    assert m.extents[0] == pytest.approx(p["foot_l"], abs=0.05)
    assert m.extents[1] == pytest.approx(p["foot_w"], abs=0.05)
    assert m.extents[2] == pytest.approx(p["foot_h"], abs=1e-4)
    assert p["foot_h"] - p["foot_out"] == pytest.approx(p["foot_niche_h"], abs=1e-9)
    assert p["foot_niche_h"] == pytest.approx(p["tray_flange_t"] - p["foot_niche_skin"], abs=1e-9)


def test_foot_has_bottom_chamfer():
    p = params()
    full = _foot_area(p)
    assert _area_at("foot", 0.05) < 0.9 * full
    assert _area_at("foot", p["foot_h"] / 2) == pytest.approx(full, rel=0.01)
    assert _area_at("foot", p["foot_h"] - 0.05) == pytest.approx(full, rel=0.01)


def test_foot_sits_in_its_niche():
    p = params()
    foot = _solid("foot")
    dummy = _solid("asm_feet")
    tray = _solid("asm_tray")
    for a in p["foot_angles"]:
        placed = foot.translate([p["foot_r"], 0, p["tray_bot_z"] - p["foot_out"]]).rotate([0, 0, a])
        assert float((placed ^ tray).volume()) < TOL
        inside = float((placed ^ dummy).volume())
        assert inside == pytest.approx(float(foot.volume()), rel=1e-3)
        assert float((placed.translate([0, 0, 0.1]) ^ tray).volume()) > 1.0
    under = mesh("asm_feet").bounds[0][2]
    assert mesh("asm_tray").bounds[0][2] - under == pytest.approx(p["foot_out"], abs=1e-4)


def test_foot_overhangs():
    assert overhang_area(mesh("foot")) < OVERHANG_MAX


def test_foot_outline_lives_in_lib_and_small_parts_do_not_use_the_dummies():
    lib = (CASE_DIR / "lib.scad").read_text()
    dummies = (CASE_DIR / "dummies.scad").read_text()
    small = (CASE_DIR / "small.scad").read_text()
    assert "module foot2d(" in lib
    assert "module foot2d(" not in dummies
    assert "dummies.scad" not in small
    assert "use <lib.scad>" in small


def test_top_plug_prints_face_down_with_a_chamfered_entry_edge():
    p = params()
    m = mesh("top_plug")
    d = p["top_plug_d"] + p["top_plug_fit"]
    c = p["top_plug_chamfer"]
    h = p["top_plug_depth"]
    assert p["top_plug_fit"] == pytest.approx(0.1)
    assert m.bounds[0][2] == pytest.approx(0.0, abs=1e-6)
    assert m.extents[2] == pytest.approx(h, abs=1e-4)
    assert m.extents[0] == pytest.approx(d, abs=0.01)
    assert _area_at("top_plug", 0.02) == pytest.approx(np.pi * d * d / 4, rel=0.01)
    assert _area_at("top_plug", h - c - 0.02) == pytest.approx(np.pi * d * d / 4, rel=0.01)
    top_d = d - 2 * c
    assert _area_at("top_plug", h - 0.02) == pytest.approx(np.pi * (top_d / 2 + 0.02) ** 2, rel=0.03)
    assert overhang_area(m) < 1e-6
    assert m.volume == pytest.approx(mesh("asm_top_plugs").volume / 2, rel=1e-5)
