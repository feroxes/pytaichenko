import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent
SMALL = ["button_cap", "speaker_gasket", "top_gasket", "tray_gasket", "top_plug", "foot", "com_bushing"]
MIN_TRAVEL = 0.5
MIN_LIFT = 0.5
MIN_RETENTION = 0.5
OVERHANG_MAX = 5


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def _area_at(part, z):
    return float(_solid(part).slice(float(z)).area())


def _loops(part, z):
    return [np.asarray(q) for q in _solid(part).slice(float(z)).to_polygons()]


def _button_xy():
    p = params()
    a = np.radians(p["button_az"])
    return p["button_r"] * np.cos(a), p["button_r"] * np.sin(a)


def _gap(part_up, part_down, origin):
    up = first_hit(mesh(part_up), origin, (0, 0, 1))
    down = first_hit(mesh(part_down), origin, (0, 0, -1))
    assert up is not None and down is not None
    return up + down


def _squeeze(t, gap):
    return (t - gap) / t


def _at(r, deg, z):
    a = np.radians(deg)
    return (r * np.cos(a), r * np.sin(a), z)


@pytest.mark.parametrize("name", SMALL)
def test_small_parts_watertight_one_body_on_bed(name):
    m = mesh(name)
    assert m.is_watertight
    assert m.volume > 0
    assert len(_solid(name).decompose()) == 1
    bed = params()["bed"]
    assert m.extents[0] < bed and m.extents[1] < bed
    assert m.bounds[0][2] == pytest.approx(0.0, abs=1e-6)


@pytest.mark.parametrize("name", SMALL)
def test_small_parts_overhangs(name):
    assert overhang_area(mesh(name)) < OVERHANG_MAX


def test_button_cap_sits_on_the_pusher():
    p = params()
    bx, by = _button_xy()
    cap = _solid("asm_button_cap")
    assert overlap("asm_button_cap", "asm_button") < TOL
    assert mesh("asm_button_cap").bounds[1][2] == pytest.approx(p["H"], abs=1e-4)
    s = 0.1
    probe = manifold3d.Manifold.cube([s, s, s], True)
    above = cap ^ probe.translate([bx, by, p["btn_tip_z"] + 0.2])
    below = cap ^ probe.translate([bx, by, p["btn_tip_z"] - 0.2])
    assert float(above.volume()) / s ** 3 > 0.9
    assert float(below.volume()) < TOL
    pocket_r = min(np.linalg.norm(q - [bx, by], axis=1).min() for q in _loops("asm_button_cap", p["btn_tip_z"] - 0.2))
    assert p["btn_pusher_d"] / 2 + 0.1 < pocket_r < p["btn_pusher_d"] / 2 + 0.5
    travel = mesh("asm_button_cap").bounds[0][2] - (p["btn_z0"] + p["btn_body_h"])
    assert travel >= MIN_TRAVEL


def test_button_cap_stem_in_hole():
    p = params()
    bx, by = _button_xy()
    stem_d = p["button_hole_d"] - 2 * p["clr"]
    for z in (p["btn_plate_z"] + 0.5, p["H"] - 1.0):
        loops = _loops("asm_button_cap", z)
        assert len(loops) == 1
        assert _area_at("asm_button_cap", z) == pytest.approx(np.pi * stem_d ** 2 / 4, rel=0.01)
        c = loops[0].mean(axis=0)
        assert np.hypot(c[0] - bx, c[1] - by) < 0.05


def test_button_cap_moves_freely_in_the_lid():
    cap = _solid("asm_button_cap")
    top = _solid("asm_top")
    for dz in (0.0, MIN_LIFT, -MIN_TRAVEL):
        assert float((cap.translate([0, 0, dz]) ^ top).volume()) < TOL, dz


def test_button_cap_cannot_fall_out():
    p = params()
    bx, by = _button_xy()
    hole_r = p["button_hole_d"] / 2
    flange_r = max(
        np.linalg.norm(q - [bx, by], axis=1).max()
        for z in np.arange(p["btn_tip_z"] - 1.4, p["btn_tip_z"] - 0.6, 0.1)
        for q in _loops("asm_button_cap", z)
    )
    assert flange_r >= hole_r + MIN_RETENTION
    pulled = _solid("asm_button_cap").translate([0, 0, p["btn_plate_z"] - p["btn_tip_z"] + 0.8])
    assert float((pulled ^ _solid("asm_top")).volume()) > 0.0


def test_button_cap_print_orientation():
    p = params()
    a = mesh("asm_button_cap")
    b = mesh("button_cap")
    assert b.volume == pytest.approx(a.volume, rel=1e-6)
    assert b.extents[2] == pytest.approx(a.extents[2], abs=1e-6)
    assert np.allclose((b.bounds[0][:2] + b.bounds[1][:2]) / 2, [0, 0], atol=0.05)
    assert _area_at("button_cap", 0.05) > 0.8 * _area_at("button_cap", 1.0)
    assert max(b.extents[:2]) < p["bed"]


def _radii(part):
    v = mesh(part).vertices
    return np.hypot(v[:, 0], v[:, 1])


@pytest.mark.parametrize(
    "name,t,od,id_",
    [
        ("speaker_gasket", "spk_gasket_t", "spk_gasket_od", "spk_gasket_id"),
        ("top_gasket", "top_gasket_t", "top_gasket_od", "top_gasket_id"),
        ("tray_gasket", "tray_gasket_t", "tray_gasket_od", "tray_gasket_id"),
    ],
)
def test_gasket_print_size(name, t, od, id_):
    p = params()
    m = mesh(name)
    r = _radii(name)
    assert m.extents[2] == pytest.approx(p[t], abs=1e-4)
    assert r.max() == pytest.approx(p[od] / 2, abs=0.01)
    assert r.min() == pytest.approx(p[id_] / 2, abs=0.02)
    assert _area_at(name, p[t] / 2) == pytest.approx(np.pi * ((p[od] / 2) ** 2 - (p[id_] / 2) ** 2), rel=0.01)


@pytest.mark.parametrize(
    "name,asm,fit",
    [
        ("speaker_gasket", "asm_speaker_gasket", None),
        ("top_gasket", "asm_top_gasket", "top_gasket_fit"),
        ("tray_gasket", "asm_tray_gasket", "tray_gasket_fit"),
    ],
)
def test_gasket_matches_its_assembly_dummy(name, asm, fit):
    p = params()
    pr, ar = _radii(name), _radii(asm)
    shift = 0.0 if fit is None else p[fit]
    assert pr.max() - ar.max() == pytest.approx(shift, abs=0.02)
    assert pr.min() - ar.min() == pytest.approx(shift, abs=0.02)


@pytest.mark.parametrize("part,asm,pre", [("top_gasket", "asm_top", "top"), ("tray_gasket", "asm_tray", "tray")])
def test_gasket_drops_into_its_groove_with_room_on_both_sides(part, asm, pre):
    p = params()
    out_r, in_r = p[f"{pre}_groove_out_r"], p[f"{pre}_groove_in_r"]
    assert -0.25 <= p[f"{pre}_gasket_fit"] <= -0.15
    assert p[f"{pre}_gasket_od"] / 2 == pytest.approx(out_r + p[f"{pre}_gasket_fit"])
    assert p[f"{pre}_gasket_id"] / 2 - in_r >= 0.15
    z = p["top_z0"] + p["top_groove_depth"] / 2 if pre == "top" else p["body_bot_z"] - p["tray_groove_depth"] / 2
    cut = _solid(asm).slice(float(z))
    pts = np.concatenate([np.asarray(q) for q in cut.to_polygons()])
    r = np.hypot(pts[:, 0], pts[:, 1])
    groove_out = r[(r > out_r - 0.3) & (r < out_r + 0.3)].min()
    assert groove_out == pytest.approx(out_r, abs=0.01)
    gr = _radii(part)
    assert 0.15 <= groove_out - gr.max() <= 0.25
    assert 0.15 <= gr.min() - in_r <= 0.25


def test_speaker_gasket_compresses_on_the_rim():
    p = params()
    r = (p["spk_gasket_id"] + p["spk_gasket_od"]) / 4
    z = (p["spk_rim_top_z"] + p["top_under_z"]) / 2
    t = mesh("speaker_gasket").extents[2]
    for deg in (45, 135, 225, 315):
        gap = _gap("asm_top", "asm_speaker", _at(r, deg, z))
        assert 0.2 <= _squeeze(t, gap) <= 0.3, deg
    g = mesh("asm_speaker_gasket")
    assert g.bounds[0][2] == pytest.approx(p["spk_rim_top_z"], abs=1e-4)
    assert g.bounds[1][2] == pytest.approx(p["top_under_z"], abs=1e-4)


def test_top_gasket_compresses_where_the_lid_seats():
    p = params()
    r = p["top_groove_out_r"] - p["top_gasket_w"] / 2
    t = mesh("top_gasket").extents[2]
    z = p["top_z0"] + 0.05
    for deg in (230, 250, 290, 310):
        gap = _gap("asm_top", "asm_body", _at(r, deg, z))
        assert 0.2 <= _squeeze(t, gap) <= 0.3, deg
    for deg in (0, 45, 90, 135, 180):
        up = first_hit(mesh("asm_top"), _at(r, deg, z), (0, 0, 1))
        down = first_hit(mesh("asm_ring_arc"), _at(r, deg, z), (0, 0, -1))
        assert 0.12 <= _squeeze(t, up + down) <= 0.3, deg


def test_tray_gasket_compresses_between_tray_and_body():
    p = params()
    r = p["tray_groove_out_r"] - p["tray_gasket_w"] / 2
    t = mesh("tray_gasket").extents[2]
    z = p["body_bot_z"] - 0.05
    for deg in (0, 90, 180, 270):
        gap = _gap("asm_body", "asm_tray", _at(r, deg, z))
        assert 0.2 <= _squeeze(t, gap) <= 0.3, deg


def _seat_d():
    p = params()
    x, y = p["top_screw_xy"][0]
    pts = np.concatenate(_loops("asm_top", p["top_seat_z"] + p["top_plug_depth"] / 2))
    d = np.hypot(pts[:, 0] - x, pts[:, 1] - y)
    return 2 * d[d < p["top_plug_d"] / 2 + 0.3].min()


def test_top_plug_fits_its_seat_with_interference():
    p = params()
    m = mesh("top_plug")
    plug_d = 2 * np.hypot(m.vertices[:, 0], m.vertices[:, 1]).max()
    seat = _seat_d()
    assert seat == pytest.approx(p["top_plug_d"], abs=0.02)
    assert plug_d - seat == pytest.approx(p["top_plug_fit"], abs=0.03)
    assert 0 <= p["top_plug_fit"] <= 0.05
    assert p["top_plug_clr"] >= 0.15
    assert m.extents[2] == pytest.approx(p["top_plug_h"], abs=1e-4)
    assert p["top_plug_depth"] - p["top_plug_h"] >= 0.15


def test_top_plug_prints_face_down_with_a_chamfered_entry_edge():
    p = params()
    d = p["top_plug_d"] + p["top_plug_fit"]
    c = p["top_plug_chamfer"]
    e = p["top_plug_edge"]
    h = p["top_plug_h"]
    assert _area_at("top_plug", h - 0.02) < _area_at("top_plug", h - c - 0.02)
    assert _area_at("top_plug", 0.02) < _area_at("top_plug", e + 0.02)
    m = mesh("top_plug")
    r = np.hypot(m.vertices[:, 0], m.vertices[:, 1])
    assert r[m.vertices[:, 2] < 1e-6].max() == pytest.approx(d / 2 - e, abs=0.01)


def test_top_plug_holds_by_crush_ribs_with_a_clearance_body():
    p = params()
    seat = _seat_d()
    z = (p["top_plug_edge"] + p["top_plug_h"] - p["top_plug_chamfer"]) / 2
    loops = _loops("top_plug", z)
    assert len(loops) == 1
    q = loops[0]
    r = np.hypot(q[:, 0], q[:, 1])
    assert r.min() == pytest.approx(seat / 2 - p["top_plug_clr"], abs=0.02)
    assert r.max() == pytest.approx((p["top_plug_d"] + p["top_plug_fit"]) / 2, abs=0.02)
    a = np.degrees(np.arctan2(q[:, 1], q[:, 0]))
    tips = np.unique(np.round(a[r > seat / 2 - p["top_plug_clr"] + 0.05] / (360 / p["top_plug_ribs"])) % p["top_plug_ribs"])
    assert len(tips) == int(p["top_plug_ribs"])
    assert p["top_plug_grip_h"] >= 0.6


def test_top_plugs_sit_flush_over_the_screw_heads():
    p = params()
    plugs = _solid("asm_top_plugs")
    parts = plugs.decompose()
    assert len(parts) == len(p["top_screw_xy"]) == 3
    m = mesh("asm_top_plugs")
    assert m.bounds[1][2] == pytest.approx(p["H"], abs=1e-4)
    assert m.bounds[0][2] == pytest.approx(p["H"] - p["top_plug_h"], abs=1e-4)
    assert m.bounds[0][2] > p["top_seat_z"]
    assert m.bounds[0][2] >= mesh("asm_top_screws").bounds[1][2] - 1e-6
    for x, y in p["top_screw_xy"]:
        assert first_hit(m, (x, y, p["H"] + 1), (0, 0, -1)) == pytest.approx(1.0, abs=1e-3)
    one = float(parts[0].volume())
    assert one * 0.85 < mesh("top_plug").volume < one * 1.05


def _foot_area(p):
    return p["foot_w"] * p["foot_l"] - (4 - np.pi) * p["foot_corner_r"] ** 2


def test_foot_size_and_chamfer():
    p = params()
    m = mesh("foot")
    assert m.extents[0] == pytest.approx(p["foot_l"], abs=0.05)
    assert m.extents[1] == pytest.approx(p["foot_w"], abs=0.05)
    assert m.extents[2] == pytest.approx(p["foot_h"], abs=1e-4)
    full = _foot_area(p)
    assert _area_at("foot", 0.05) < 0.9 * full
    assert _area_at("foot", p["foot_h"] / 2) == pytest.approx(full, rel=0.01)
    assert _area_at("foot", p["foot_h"] - 0.05) == pytest.approx(full, rel=0.01)


def test_foot_sits_in_each_niche():
    p = params()
    foot = _solid("foot")
    dummy = _solid("asm_feet")
    tray = _solid("asm_tray")
    for (x, y), a in zip(p["foot_xy"], p["foot_rot"]):
        placed = foot.rotate([0, 0, a]).translate([x, y, -p["foot_out"]])
        assert float((placed ^ tray).volume()) < TOL
        assert float((placed ^ dummy).volume()) == pytest.approx(float(foot.volume()), rel=1e-3)
        assert float((placed.translate([0, 0, 0.1]) ^ tray).volume()) > 1.0


def _ray(part, o, d):
    return first_hit(mesh(part), o, d)


def test_com_bushing_press_fits_the_slot():
    p = params()
    m = mesh("com_bushing")
    assert 0.1 <= m.extents[0] - p["tray_com_w"] <= 0.2
    assert 0.1 <= m.extents[1] - p["tray_com_h"] <= 0.2
    cx, cz = p["dk_com_x"], p["usb_z"]
    o = (cx, -39.5, cz)
    assert _ray("asm_tray", o, (1, 0, 0)) == pytest.approx(p["tray_com_w"] / 2, abs=0.01)
    assert _ray("asm_tray", o, (-1, 0, 0)) == pytest.approx(p["tray_com_w"] / 2, abs=0.01)
    assert _ray("asm_tray", o, (0, 0, 1)) == pytest.approx(p["tray_com_h"] / 2, abs=0.01)
    assert _ray("asm_tray", o, (0, 0, -1)) == pytest.approx(p["tray_com_h"] / 2, abs=0.01)
    assert p["com_hole_w"] == pytest.approx(p["com_plug_w"] + 2 * p["com_plug_clr"])
    assert p["com_hole_h"] == pytest.approx(p["com_plug_h"] + 2 * p["com_plug_clr"])
    assert _ray("com_bushing", (0, 0, -1), (0, 0, 1)) is None
    sl = _loops("com_bushing", 0.5)
    pts = np.concatenate(sl)
    inner = pts[(np.abs(pts[:, 0]) < p["com_hole_w"] / 2 + 0.05) & (np.abs(pts[:, 1]) < p["com_hole_h"] / 2 + 0.05)]
    assert np.abs(inner[:, 0]).max() == pytest.approx(p["com_hole_w"] / 2, abs=0.01)
    assert np.abs(inner[:, 1]).max() == pytest.approx(p["com_hole_h"] / 2, abs=0.01)
    assert (p["tray_com_h"] - p["com_hole_h"]) / 2 >= 0.4
    assert (p["tray_com_w"] - p["com_hole_w"]) / 2 >= 0.4


def test_com_bushing_stays_in_the_tunnel_and_is_flush():
    p = params()
    a = mesh("asm_com_bushing")
    assert a.bounds[1][1] == pytest.approx(p["com_bush_y1"], abs=1e-4)
    assert a.bounds[1][1] < p["usb_face_y"]
    assert a.bounds[1][1] < p["com_slot_far_y"]
    r = np.hypot(a.vertices[:, 0], a.vertices[:, 1])
    assert r.max() <= p["case_r"] + 1e-3
    outer_end = a.vertices[a.vertices[:, 1] < p["com_bush_y1"] - 0.5]
    assert np.hypot(outer_end[:, 0], outer_end[:, 1]).min() == pytest.approx(p["case_r"], abs=0.01)
    assert overlap("asm_com_bushing", "asm_tray") < TOL
    assert overlap("asm_com_bushing", "asm_com_plug") < TOL
    assert mesh("com_bushing").volume > a.volume


def test_small_parts_do_not_use_the_dummies():
    small = (CASE_DIR / "small.scad").read_text()
    lib = (CASE_DIR / "lib.scad").read_text()
    assert "dummies.scad" not in small
    assert "use <lib.scad>" in small
    assert "module foot2d(" in lib
    assert "module ring_band(" in lib
