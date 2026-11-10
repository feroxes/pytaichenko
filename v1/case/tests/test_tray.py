import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3

CLEAR_OF = [
    "body",
    "devkit",
    "usb_plug",
    "amp",
    "cap",
    "tray_screws",
    "feet",
    "speaker",
    "mic",
    "button",
    "filament",
    "top_screws",
    "speaker_screws",
    "metal_washers",
    "devkit_screws",
]

BRIDGE_ROOF_MAX_Z = 3.3
SLOPED_OVERHANG_MAX = 150


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def solid_at(part, point, s=0.1):
    cube = manifold3d.Manifold.cube([s, s, s], True).translate(list(point))
    return float((_solid(part) ^ cube).volume()) / s ** 3 > 0.5


def _xy(r, a):
    return r * np.cos(np.radians(a)), r * np.sin(np.radians(a))


def _loops(part, z):
    return [np.asarray(q) for q in _solid(part).slice(z).to_polygons()]


def _area(loop):
    x, y = loop[:, 0], loop[:, 1]
    return 0.5 * abs(float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1))))


def _sweep_down(part, depth=30.0):
    seg = manifold3d.Manifold.cube([0.001, 0.001, depth], False).translate([0, 0, -depth])
    return _solid(part).minkowski_sum(seg)


def _flat_roofs(m):
    on_bed = np.all(m.triangles[:, :, 2] <= m.bounds[0][2] + 1e-6, axis=1)
    mask = (m.face_normals[:, 2] < -0.999) & ~on_bed
    return m.triangles[mask][:, :, 2].max(axis=1), m.area_faces[mask]


def test_tray_watertight_fits_bed():
    m = mesh("tray")
    assert m.is_watertight
    assert m.volume > 0
    bed = params()["bed"]
    assert m.extents[0] < bed and m.extents[1] < bed
    assert abs(m.bounds[0][2]) < 1e-6


def test_print_tray_matches_assembly_orientation():
    p = params()
    a = mesh("asm_tray")
    b = mesh("tray")
    shift = np.array([0.0, 0.0, -p["tray_bot_z"]])
    assert np.allclose(a.bounds + shift, b.bounds, atol=1e-6)
    assert abs(a.volume - b.volume) < 1e-5 * a.volume


def test_tray_gap():
    p = params()
    assert 0.2 <= p["tray_gap"] <= 0.3
    opening = p["inner_r_profile"][0]
    for z in (0.3, 0.6, 0.85):
        tray_r = max(np.linalg.norm(q, axis=1).max() for q in _loops("asm_tray", z))
        body_in = min(np.linalg.norm(q, axis=1).min() for q in _loops("asm_body", z))
        assert tray_r == pytest.approx(opening - p["tray_gap"], abs=0.01)
        assert body_in - tray_r >= 0.2


def test_tray_passes_the_opening():
    p = params()
    opening = p["inner_r_profile"][0]
    v = mesh("asm_tray").vertices
    above = v[v[:, 2] > -p["tray_gasket_t"] + 1e-6]
    assert np.all(np.linalg.norm(above[:, :2], axis=1) <= opening - p["tray_gap_min"])
    below = v[v[:, 2] <= -p["tray_gasket_t"] + 1e-6]
    assert np.linalg.norm(below[:, :2], axis=1).max() == pytest.approx(p["tray_flange_r"], abs=0.01)


def test_tray_floor_thickness():
    p = params()
    m = mesh("tray")
    t = p["tray_floor_t"]
    zb = p["tray_bot_z"]
    assert solid_at("asm_tray", (0, 0, t - 0.2))
    assert solid_at("asm_tray", (0, 0, 0.2))
    assert solid_at("asm_tray", (0, 0, zb + 0.2))
    assert not solid_at("asm_tray", (0, 0, t + 0.2))
    assert not solid_at("asm_tray", (0, 0, zb - 0.2))
    assert m.bounds[0][2] == pytest.approx(0.0, abs=1e-6)
    assert mesh("asm_tray").bounds[0][2] == pytest.approx(zb, abs=1e-6)


@pytest.mark.parametrize("name", CLEAR_OF)
def test_tray_clear(name):
    assert overlap("asm_tray", f"asm_{name}") < TOL


def test_tray_screws_flush():
    p = params()
    tray_min = mesh("asm_tray").bounds[0][2]
    screws = mesh("asm_tray_screws").bounds
    assert screws[0][2] >= tray_min
    assert screws[0][2] == pytest.approx(p["tray_screw_head_z"], abs=1e-6)
    assert screws[0][2] - tray_min == pytest.approx(p["tray_screw_recess"] + p["csk_head_z"], abs=1e-6)
    assert screws[0][2] - p["tray_seat_z"] <= p["csk_clr"]
    assert p["tray_screw_recess"] <= p["tray_flange_t"] - 1.5 + 1e-9
    assert screws[1][2] - screws[0][2] == pytest.approx(p["screw_len"], abs=1e-6)
    assert screws[1][2] - p["tray_pad_t"] >= p["min_thread"]
    hole_end = p["tray_pad_t"] + p["tray_boss_hole_depth"]
    assert screws[1][2] + p["tray_pad_gap"] + p["insert_tip_gap"] <= hole_end
    assert p["tray_screw_tip_z"] == pytest.approx(screws[1][2], abs=1e-6)
    assert p["tray_screw_engage"] == pytest.approx(screws[1][2] - p["tray_pad_t"], abs=1e-6)
    assert min(p["insert_len"], p["tray_screw_engage"]) >= p["min_thread"]


def test_tray_screw_head_sits_on_the_countersink():
    p = params()
    n = len(p["tray_screw_angles"])
    shank = n * np.pi * p["screw_d"] ** 2 / 4
    head_z = p["tray_screw_head_z"]
    s = _solid("asm_tray_screws")
    rim = n * np.pi * p["screw_head_d"] ** 2 / 4
    assert s.slice(head_z + 0.05).area() == pytest.approx(rim, rel=0.03)
    mid_h = p["screw_rim_h"] + 0.5
    r_mid = p["screw_head_d"] / 2 - 0.5
    assert s.slice(head_z + mid_h).area() == pytest.approx(n * np.pi * r_mid ** 2, rel=0.03)
    assert s.slice(head_z + p["screw_head_h"] + 0.3).area() == pytest.approx(shank, rel=0.03)
    seat_gap = p["csk_gap"]
    for a in p["tray_screw_angles"]:
        x, y = _xy(p["tray_screw_r"], a)
        z_cone = p["tray_seat_z"] + p["csk_bore_h"] + 0.5
        seat_r = p["csk_d"] / 2 - 0.5
        scr_r = p["screw_head_d"] / 2 - (z_cone - head_z - p["screw_rim_h"])
        assert seat_r - scr_r == pytest.approx(seat_gap, abs=0.005)
        assert solid_at("asm_tray", (x + seat_r + 0.1, y, z_cone), 0.05)
        assert not solid_at("asm_tray", (x + seat_r - 0.1, y, z_cone), 0.05)


@pytest.mark.parametrize("a", [60, 150, 270])
def test_screw_seat_and_hard_stop(a):
    p = params()
    assert a in p["tray_screw_angles"]
    x, y = _xy(p["tray_screw_r"], a)
    top = p["tray_pad_top_z"]
    zb = p["tray_seat_z"]
    csk_r0 = p["csk_d"] / 2
    for z in np.linspace(p["tray_bot_z"] + 0.1, zb - 0.1, 3):
        assert not solid_at("asm_tray", (x + csk_r0 - 0.1, y, z), 0.05)
        assert solid_at("asm_tray", (x + csk_r0 + 0.1, y, z), 0.05)
    csk_r = p["csk_d"] / 2
    thru_r = p["screw_thru_d"] / 2
    assert top == pytest.approx(p["tray_floor_t"])
    assert not solid_at("asm_tray", (x + csk_r - 0.1, y, zb + 0.2), 0.05)
    assert solid_at("asm_tray", (x + csk_r + 0.1, y, zb + 0.2), 0.05)
    assert not solid_at("asm_tray", (x + csk_r - 0.1, y, zb + p["csk_bore_h"] - 0.1), 0.05)
    z_c = zb + p["csk_bore_h"] + 0.5
    assert not solid_at("asm_tray", (x + csk_r - 0.5 - 0.1, y, z_c), 0.05)
    assert solid_at("asm_tray", (x + csk_r - 0.5 + 0.1, y, z_c), 0.05)
    z_n = zb + p["csk_h"] + 0.3
    assert not solid_at("asm_tray", (x + thru_r - 0.1, y, z_n), 0.05)
    assert solid_at("asm_tray", (x + thru_r + 0.1, y, z_n), 0.05)
    assert solid_at("asm_tray", (x + 3.6, y, top - 0.1))
    assert not solid_at("asm_tray", (x + 3.6, y, top + 0.2))
    assert not solid_at("asm_tray", (x, y, top - 0.5))
    assert solid_at("asm_tray", (x + 3.0, y, top - p["tray_pad_relief_depth"] - 0.1))
    assert p["tray_pad_t"] - top == pytest.approx(p["tray_pad_gap"])
    assert p["tray_pad_gap"] >= p["tray_gasket_squeeze"] + 0.2 - 1e-9
    assert top - p["tray_pad_relief_depth"] - (zb + p["csk_h"]) >= 1.5


@pytest.mark.parametrize("a", [60, 150, 270])
def test_pad_relief_takes_a_proud_insert(a):
    p = params()
    x, y = _xy(p["tray_screw_r"], a)
    top = p["tray_pad_top_z"]
    depth = p["tray_pad_relief_depth"]
    relief_r = p["tray_pad_relief_d"] / 2
    assert depth >= 0.8
    assert relief_r >= p["insert_od"] / 2 + 0.5
    assert not solid_at("asm_tray", (x + relief_r - 0.15, y, top - depth + 0.1), 0.05)
    assert not solid_at("asm_tray", (x + relief_r - 0.15, y, top - 0.1), 0.05)
    assert solid_at("asm_tray", (x + relief_r + 0.15, y, top - 0.1), 0.05)
    assert solid_at("asm_tray", (x + relief_r - 0.15, y, top - depth - 0.1), 0.05)


def test_feet_protrude():
    p = params()
    d = mesh("asm_tray").bounds[0][2] - mesh("asm_feet").bounds[0][2]
    assert d == pytest.approx(2.0, abs=0.1)
    assert p["foot_out"] == pytest.approx(2.0, abs=1e-9)


def test_foot_niches():
    p = params()
    h = p["foot_niche_h"]
    zb = p["tray_bot_z"]
    half_w = p["foot_w"] / 2 + p["foot_niche_clr"]
    inner = p["foot_r"] - p["foot_l"] / 2 - p["foot_niche_clr"]
    for a in p["foot_angles"]:
        rad = np.array(_xy(1.0, a))
        tan = np.array([-rad[1], rad[0]])
        for r in np.linspace(inner + p["foot_corner_r"] + p["foot_niche_clr"] + 0.1, p["tray_flange_r"] - 0.05, 5):
            for t in (-half_w + 0.1, 0.0, half_w - 0.1):
                q = r * rad + t * tan
                assert not solid_at("asm_tray", (q[0], q[1], zb + h / 2), 0.05), (a, r, t)
                if np.hypot(*q) < p["tray_flange_r"] - 0.1:
                    assert solid_at("asm_tray", (q[0], q[1], zb + h + 0.1), 0.05), (a, r, t)
            for t in (-half_w - 0.15, half_w + 0.15):
                q = r * rad + t * tan
                if np.hypot(*q) < p["tray_flange_r"] - 0.1:
                    assert solid_at("asm_tray", (q[0], q[1], zb + h / 2), 0.05), (a, r, t)
        q = (inner - 0.15) * rad
        assert solid_at("asm_tray", (q[0], q[1], zb + h / 2), 0.05)
    roofs = np.array([zz for zz, _ in zip(*_flat_roofs(mesh("tray")))])
    assert np.any(np.abs(roofs - h) < 0.01)


def test_tray_overhangs():
    m = mesh("tray")
    roofs_z, roofs_area = _flat_roofs(m)
    assert roofs_z.max() <= BRIDGE_ROOF_MAX_Z
    p = params()
    rec = p["tray_screw_recess"]
    allowed = [p["foot_niche_h"], rec + p["csk_bore_h"], rec + p["csk_h"]]
    assert all(min(abs(z - a) for a in allowed) < 0.01 for z in roofs_z)
    seat_roofs = np.array([min(abs(z - a) for a in allowed[1:]) < 0.01 for z in roofs_z])
    assert roofs_area[seat_roofs].sum() < 2.0
    assert overhang_area(m) - roofs_area.sum() < SLOPED_OVERHANG_MAX


def test_devkit_cradle():
    p = params()
    y0, y1 = p["dk_y0"], p["dk_y1"]
    zb, zt = p["dk_z0"], p["dk_z1"]
    half = p["dk_w"] / 2
    for sx in (-1, 1):
        for y in (y0 + 0.7, y1 - 0.7):
            assert solid_at("asm_tray", (sx * 12.3, y, zb - 0.1))
            assert not solid_at("asm_tray", (sx * 12.3, y, zb + 0.1))
        for y in (y0 + 4, y0 + 7.0, y1 - 4):
            assert solid_at("asm_tray", (sx * (half + 0.8), y, zt - 0.2))
            assert not solid_at("asm_tray", (sx * (half - 0.2), y, zt - 0.2))
            assert not solid_at("asm_tray", (sx * (half + 0.8), y, zt + 0.1))
    assert not solid_at("asm_tray", (0, (y0 + y1) / 2, zb - 0.1))
    assert not solid_at("asm_tray", (0, y0 + 10, zb - 0.1))


def test_devkit_latches_hold_the_board():
    p = params()
    y0, y1 = p["dk_y0"], p["dk_y1"]
    zt = p["dk_z1"]
    assert solid_at("asm_tray", (0, y0 + 0.2, zt + 1.0))
    assert not solid_at("asm_tray", (0, y0 + 0.2, zt + 0.05))
    assert not solid_at("asm_tray", (0, y0 + 1.5, zt + 1.0))
    for sx in (-1, 1):
        assert solid_at("asm_tray", (sx * 7.6, y1 - 0.2, zt + 1.0))
        assert not solid_at("asm_tray", (sx * 7.6, y1 - 0.2, zt + 0.05))
    assert not solid_at("asm_tray", (0, y1 - 0.2, zt + 1.0))


def test_amp_cradle_and_latches():
    p = params()
    x0 = p["amp_x"] - p["amp_w"] / 2
    x1 = p["amp_x"] + p["amp_w"] / 2
    ya0 = p["amp_y"] - p["amp_l"] / 2
    ya1 = p["amp_y"] + p["amp_l"] / 2
    zb = p["amp_z0"]
    zt = zb + p["amp_t"]
    pad_x = x0 + p["amp_hdr_in"] + p["amp_hdr_w"] + p["amp_hdr_pad_gap"] + p["amp_hdr_pad_w"] / 2
    for x in (x1 - 1.5, pad_x):
        for y in (ya0 + 1.5, ya1 - 1.5):
            assert solid_at("asm_tray", (x, y, zb - 0.1))
            assert not solid_at("asm_tray", (x, y, zb + 0.1))
    assert not solid_at("asm_tray", (p["amp_x"], p["amp_y"], zb - 0.1))
    dx = p["amp_latch_dx"]
    for sx in (-1, 1):
        for edge, sy in ((ya1, 1), (ya0, -1)):
            x = p["amp_x"] + sx * dx
            y_in = edge - sy * 0.25
            assert solid_at("asm_tray", (x, y_in, zt + 0.7))
            assert not solid_at("asm_tray", (x, y_in, zt + 0.05))
            assert not solid_at("asm_tray", (x, edge - sy * 2.0, zt + 0.7))
    for sy, edge in ((1, ya1), (-1, ya0)):
        assert solid_at("asm_tray", (x1 - 1.0, edge + sy * 0.8, zt - 0.2))
        assert solid_at("asm_tray", (x0 + 1.0, edge + sy * 0.8, zt - 0.2))
        assert not solid_at("asm_tray", (x0 + 3.0, edge - sy * 1.5, zt - 0.2))


def test_amp_retainers_leave_the_header_and_terminal_edges_open():
    p = params()
    x0 = p["amp_x"] - p["amp_w"] / 2
    x1 = p["amp_x"] + p["amp_w"] / 2
    zt = p["amp_z0"] + p["amp_t"]
    clr = p["amp_clr"]
    for x in (x1 + clr + 0.4, x0 - clr - 0.4):
        for dy in np.arange(-(p["amp_l"] / 2 - p["amp_stop_len"] - 0.3), p["amp_l"] / 2 - p["amp_stop_len"] - 0.2, 0.5):
            for z in (p["amp_z0"] + 0.5, zt - 0.3, zt + 0.5, zt + 3.0):
                assert not solid_at("asm_tray", (x, p["amp_y"] + dy, z), 0.05), (x, dy, z)
    keep = p["amp_block_l"] / 2
    assert p["amp_l"] / 2 - p["amp_stop_len"] > keep + 0.5
    for x in (x1 + clr + 0.4, x0 - clr - 0.4):
        for z in (zt + 0.5, zt + 3.0, zt + 6.0):
            for sy in (-1, 1):
                assert not solid_at("asm_tray", (x, p["amp_y"] + sy * (p["amp_l"] / 2 - 1.0), z), 0.05)
    for sx in (-1, 1):
        for sy in (-1, 1):
            hx = p["amp_x"] + sx * p["amp_w"] / 2
            assert solid_at("asm_tray", (hx + sx * (clr + 0.6), p["amp_y"] + sy * (p["amp_l"] / 2 - 1.0), zt - 0.3), 0.05)


def test_amp_header_and_terminal_dummy_geometry():
    p = params()
    x0 = p["amp_x"] - p["amp_w"] / 2
    x1 = p["amp_x"] + p["amp_w"] / 2
    zt = p["amp_z0"] + p["amp_t"]
    assert p["amp_hdr_n"] == 7 and p["amp_hdr_pitch"] == pytest.approx(2.54)
    assert p["amp_hdr_h"] == pytest.approx(2.5)
    xc = x0 + p["amp_hdr_in"] + p["amp_hdr_w"] / 2
    for k in range(7):
        y = p["amp_y"] + (k - 3) * 2.54
        assert solid_at("asm_amp", (xc, y, zt + p["amp_hdr_h"] + p["amp_hdr_pin"] - 0.2), 0.04)
        assert solid_at("asm_amp", (xc, y, p["amp_z0"] - p["amp_hdr_tail"] + 0.2), 0.04)
        ys = y + 1.27 if k < 6 else y - 1.27
        assert not solid_at("asm_amp", (xc, ys, zt + p["amp_hdr_h"] + 1.0), 0.04)
        assert solid_at("asm_amp", (xc + 1.1, ys, zt + p["amp_hdr_h"] - 0.3), 0.04)
    assert not solid_at("asm_amp", (xc, p["amp_y"], zt + p["amp_hdr_h"] + p["amp_hdr_pin"] + 0.3), 0.04)
    b = mesh("asm_amp").bounds
    assert b[1][2] == pytest.approx(p["amp_z0"] + p["amp_total_h"], abs=1e-4)
    assert zt + p["amp_hdr_h"] + p["amp_hdr_pin"] < b[1][2]
    assert solid_at("asm_amp", (x1 - 1.0, p["amp_y"], zt + p["amp_block_h"] - 0.3), 0.05)
    assert not solid_at("asm_amp", (x1 - 1.0, p["amp_y"] + p["amp_block_l"] / 2 + 0.5, zt + 3.0), 0.05)


def test_amp_header_tails_clear_the_tray_and_the_header_side_pads():
    p = params()
    x0 = p["amp_x"] - p["amp_w"] / 2
    xc = x0 + p["amp_hdr_in"] + p["amp_hdr_w"] / 2
    pad_x0 = x0 + p["amp_hdr_in"] + p["amp_hdr_w"] + p["amp_hdr_pad_gap"]
    zb = p["amp_z0"]
    for k in range(7):
        y = p["amp_y"] + (k - 3) * 2.54
        assert not solid_at("asm_tray", (xc, y, zb - 0.6), 0.05)
        assert not solid_at("asm_tray", (xc + 0.9, y, zb - 0.6), 0.05)
    y_end = p["amp_y"] + 3 * 2.54
    assert solid_at("asm_tray", (pad_x0 + 0.5, p["amp_y"] + p["amp_l"] / 2 - 1.0, zb - 0.1), 0.05)
    assert not solid_at("asm_tray", (pad_x0 - 0.3, y_end, zb - 0.1), 0.05)
    assert overlap("asm_tray", "asm_amp") < TOL


def test_tray_has_no_transistor_pocket():
    p = params()
    assert not any(k.startswith("drv_") for k in p)
    zone = manifold3d.Manifold.cube([18.0, 40.0, 10.0]).translate([-37.0, -24.0, p["tray_floor_t"] + 0.05])
    assert float((_solid("asm_tray") ^ zone).volume()) < TOL
    floor = _solid("asm_tray").slice(p["tray_floor_t"] - 0.1)
    patch = manifold3d.CrossSection.square([16.0, 36.0]).translate([-36.0, -22.0])
    assert float((floor ^ patch).area()) == pytest.approx(float(patch.area()), rel=1e-3)


@pytest.mark.parametrize("name", ["tray", "devkit", "amp", "cap", "devkit_screws"])
def test_tray_insertable(name):
    swept = _sweep_down(f"asm_{name}")
    assert float((swept ^ _solid("asm_body")).volume()) < TOL


def _cube(x0, x1, y0, y1, z0, z1):
    return manifold3d.Manifold.cube([x1 - x0, y1 - y0, z1 - z0], False).translate([x0, y0, z0])


def test_usb_pull_stop():
    p = params()
    body = _solid("asm_body")
    engaged = _solid("asm_devkit").translate([0, 0.6, 0]) ^ body
    assert float(engaged.volume()) > 0.5
    x0, _, z0, x1, _, z1 = engaged.bounding_box()
    assert z0 >= p["dk_z0"] - 1e-3 and z1 <= p["dk_z1"] + 1e-3
    assert x0 >= -13.8 - 1e-3 and x1 <= 13.8 + 1e-3
    for lo, hi in ((-13.8, -9.8), (9.8, 13.8)):
        assert float((engaged ^ _cube(lo, hi, 0, 100, 0, 100)).volume()) > 0.2
    assert float((engaged ^ _cube(-9.7, 9.7, 0, 100, 0, 100)).volume()) < TOL
    loose = _solid("asm_devkit").translate([0, 0.3, 0]) ^ body
    assert float(loose.volume()) < TOL


def test_usb_pull_stop_is_rigid_and_fused():
    p = params()
    face = p["dk_y1"] + 0.4
    z0 = p["dk_z0"] - 0.4
    z1 = p["dk_z1"] + 0.4
    zm = (z0 + z1) / 2
    prof = p["inner_r_profile"]
    for sx in (-1, 1):
        x = sx * 11.8
        assert not solid_at("asm_body", (x, face - 0.2, zm))
        assert not solid_at("asm_body", (x, face - 0.2, z0 - 0.6))
        for z in (z0 + 0.1, zm, z1 - 0.1):
            assert solid_at("asm_body", (x, face + 0.2, z))
        assert not solid_at("asm_body", (x, face + 0.3, z0 - 3.6))
        inner_y = np.sqrt(np.interp(zm, np.arange(len(prof)), prof) ** 2 - x * x)
        for y in np.arange(face + 0.2, inner_y + 1.0, 0.5):
            assert solid_at("asm_body", (x, y, zm))
        assert not solid_at("asm_body", (x, face + 0.2, z1 + 0.4))


def test_usb_receptacle_clears_the_bottom_opening():
    p = params()
    opening = p["inner_r_profile"][0]
    radii = np.linalg.norm(mesh("asm_devkit").vertices[:, :2], axis=1)
    assert opening - radii.max() >= p["usb_open_clear"] - 1e-4
    assert p["dk_y1"] + p["dk_usb_out"] == pytest.approx(p["usb_tip_y"], abs=1e-9)
    assert p["usb_tip_y"] < p["inner_r_profile"][int(p["usb_z"])]


def test_tray_has_no_usb_notch_or_tab():
    p = params()
    for z in (0.3, 1.0, 2.0, 2.6):
        r = max(np.linalg.norm(q, axis=1).max() for q in _loops("asm_tray", z))
        assert r <= p["inner_r_profile"][0] - p["tray_gap_min"] + 1e-6
    y = p["inner_r_profile"][0] + p["wall_t"] / 2
    for sx in (-1, 1):
        for z in (0.3, 1.0, 1.7):
            assert solid_at("asm_body", (sx * 7.2, y, z))
            assert solid_at("asm_body", (0, y, z))


from conftest import inside_xy


def _ring_samples(r0, r1, n_az=96, n_r=7):
    az = np.radians(np.arange(n_az) * 360.0 / n_az + 0.37)
    rs = np.linspace(r0, r1, n_r)
    return np.array([[r * np.cos(a), r * np.sin(a)] for r in rs for a in az])


def _components(m):
    parent = list(range(len(m.vertices)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for a, b, c in m.faces:
        for u, v in ((a, b), (b, c)):
            ru, rv = find(u), find(v)
            if ru != rv:
                parent[ru] = rv
    return len({find(i) for i in set(m.faces.flatten())})


def _lip_contact_r(p):
    ang = np.radians(p["lip_phi"])
    return p["lip_hinge_r"] + p["lip_vx"] * np.cos(ang) - p["lip_hc"] * np.sin(ang)


def test_tray_gasket_part_is_a_printed_wiper_lip():
    p = params()
    m = mesh("tray_gasket")
    assert m.is_watertight
    assert m.bounds[0][2] == pytest.approx(0.0, abs=1e-6)
    assert m.extents[2] == pytest.approx(p["lip_free_h"], abs=0.01)
    assert 1.2 <= m.extents[2] <= 1.5
    assert m.extents[0] < p["bed"]
    assert 0.6 <= p["lip_t"] <= 0.8
    r = np.linalg.norm(m.vertices[:, :2], axis=1)
    assert r.max() == pytest.approx(p["tray_flange_r"], abs=0.01)
    assert r.min() == pytest.approx(p["tray_neck_r"] + p["tray_gasket_neck_clr"], abs=0.01)
    s = _solid("tray_gasket")
    assert len(s.slice(p["lip_base_t"] / 2).to_polygons()) == 2
    assert len(s.slice(p["lip_base_t"] + 0.4).to_polygons()) == 2
    assert float(s.slice(p["lip_base_t"] + 0.4).area()) < 0.45 * float(s.slice(p["lip_base_t"] / 2).area())
    assert overhang_area(m) < 5.0
    assert _components(m) == 1


def test_wiper_lip_leans_and_wall_has_the_wanted_thickness():
    p = params()
    s = _solid("tray_gasket")
    z = p["lip_base_t"] + 0.3
    z2 = p["lip_base_t"] + 0.7
    ring = []
    for zz in (z, z2):
        loops = [np.asarray(q) for q in s.slice(zz).to_polygons()]
        rr = sorted(np.linalg.norm(q, axis=1).mean() for q in loops)
        ring.append(rr)
    inner_a, outer_a = ring[0]
    inner_b, outer_b = ring[1]
    assert outer_a - inner_a == pytest.approx(p["lip_w_h"], abs=0.02)
    assert (inner_a + outer_a) / 2 - (inner_b + outer_b) / 2 == pytest.approx(0.4 * np.tan(np.radians(p["lip_lean"])), abs=0.03)
    assert p["lip_lean"] <= 45


def test_wiper_lip_is_a_slender_beam_that_bends_not_compresses():
    p = params()
    assert p["lip_vlen"] / p["lip_t"] >= 1.5
    assert p["lip_free_h"] - p["tray_gasket_t"] == pytest.approx(p["tray_gasket_squeeze"], abs=1e-6)
    assert 0 < p["lip_phi"] < 25
    assert p["lip_hc_set"] - (p["tray_gasket_squeeze"] + 0.2) >= 0
    assert p["lip_phi_stop"] > p["lip_phi"]
    assert p["lip_phi_stop"] < 45
    assert p["lip_w_h"] * np.sin(np.radians(p["lip_phi"])) < p["lip_base_t"] - 0.05


def test_tray_gasket_sits_between_body_face_and_flange():
    p = params()
    g = mesh("asm_tray_gasket").bounds
    assert g[1][2] == pytest.approx(0.0, abs=1e-3)
    assert g[1][2] <= 1e-6
    assert g[0][2] == pytest.approx(-p["tray_gasket_t"], abs=1e-6)
    assert mesh("asm_body").bounds[0][2] == pytest.approx(0.0, abs=1e-6)
    assert mesh("asm_tray").bounds[0][2] == pytest.approx(p["tray_bot_z"], abs=1e-6)
    assert overlap("asm_tray_gasket", "asm_body") < TOL
    assert overlap("asm_tray_gasket", "asm_tray") < TOL
    assert overlap("asm_tray_gasket", "asm_tray_screws") < TOL
    assert overlap("asm_tray_gasket", "asm_devkit") < TOL


def test_deflected_lip_touches_the_body_face_all_the_way_round():
    p = params()
    rc = _lip_contact_r(p)
    assert p["inner_r_profile"][0] + 0.3 < rc - p["lip_nose_r"]
    assert rc + p["lip_nose_r"] < p["tray_flange_r"]
    eps = 0.02
    pts = _ring_samples(rc - 0.03, rc + 0.03, n_az=96, n_r=2)
    assert inside_xy("asm_body", eps, pts).all()
    assert inside_xy("asm_tray_gasket", -eps, pts).all()
    assert not inside_xy("asm_tray_gasket", eps, pts).any()
    chord = 2 * np.sqrt(2 * p["lip_nose_r"] * eps - eps * eps)
    s = _solid("asm_tray_gasket").slice(-eps)
    loops = [np.asarray(q) for q in s.to_polygons()]
    assert len(loops) == 2
    radii = sorted(np.linalg.norm(q, axis=1).mean() for q in loops)
    assert radii[1] - radii[0] == pytest.approx(chord, abs=0.03)
    assert (radii[0] + radii[1]) / 2 == pytest.approx(rc, abs=0.02)
    deeper = _solid("asm_tray_gasket").slice(-0.1)
    assert float(deeper.area()) > float(s.area())


def test_lip_base_rests_on_the_flange_all_the_way_round():
    p = params()
    r0 = p["tray_neck_r"] + p["tray_gasket_neck_clr"] + 0.05
    r1 = p["tray_flange_r"] - 0.05
    pts = _ring_samples(r0, r1, n_az=96, n_r=5)
    gt = p["tray_gasket_t"]
    eps = 0.02
    assert inside_xy("asm_tray", -gt - eps, pts).all()
    assert inside_xy("asm_tray_gasket", -gt + eps, pts).all()
    assert not inside_xy("asm_tray", -gt + eps, pts).any()
    assert not inside_xy("asm_body", -eps, pts).any()


def test_no_air_path_across_the_seam():
    p = params()
    gt = p["tray_gasket_t"]
    lo = p["inner_r_profile"][0] + 0.02
    hi = p["tray_flange_r"] - 0.02
    zs = np.round(np.arange(-gt + 0.02, -0.02, 0.05), 3)
    az = np.radians(np.arange(24) * 15.0 + 1.0)
    rs = np.linspace(lo, hi, 60)
    pts = np.array([[r * np.cos(a), r * np.sin(a)] for a in az for r in rs])
    for z in zs:
        hit = inside_xy("asm_tray_gasket", float(z), pts).reshape(len(az), len(rs))
        assert hit.any(axis=1).all(), z
    rc = _lip_contact_r(p)
    assert rc > lo and rc < hi


def test_gasket_squeeze_leaves_the_pads_short_of_the_bosses():
    p = params()
    squeeze = p["tray_gasket_squeeze"]
    assert 0.2 <= squeeze <= 0.4
    movers = ["asm_tray", "asm_devkit", "asm_amp", "asm_cap", "asm_tray_screws", "asm_devkit_screws"]
    body = _solid("asm_body")
    for name in movers:
        up = _solid(name).translate([0, 0, squeeze + 0.2 - 1e-3])
        assert float((up ^ body).volume()) < TOL, name
    pad_hit = _solid("asm_tray").translate([0, 0, p["tray_pad_gap"] + 0.1]) ^ body
    assert float(pad_hit.volume()) > 0.5
    tip_hole_end = p["tray_pad_t"] + p["tray_boss_hole_depth"]
    assert p["tray_screw_tip_z"] + squeeze + p["insert_tip_gap"] <= tip_hole_end


def test_gasket_is_the_only_thing_closing_the_seam_outside_the_opening():
    p = params()
    gt = p["tray_gasket_t"]
    r = p["tray_flange_r"] - 0.05
    pts = _ring_samples(r, r, n_az=72, n_r=1)
    assert inside_xy("asm_tray", -gt - 0.02, pts).all()
    assert not inside_xy("asm_tray", -gt + 0.1, pts).any()


def test_four_feet_keep_material_to_the_counterbores():
    p = params()
    assert len(p["foot_angles"]) == 4
    assert sorted(p["foot_angles"]) == [45, 135, 225, 315]
    zb = p["tray_bot_z"] + p["foot_niche_h"] / 2
    floor = _solid("asm_tray").slice(zb)
    niches = manifold3d.CrossSection.circle(p["tray_flange_r"], 360) - floor
    for a in p["tray_screw_angles"]:
        x, y = _xy(p["tray_screw_r"], a)
        keep = manifold3d.CrossSection.circle(p["csk_d"] / 2 + 1.5, 96).translate([x, y])
        hole = manifold3d.CrossSection.circle(p["csk_d"] / 2 + 0.05, 96).translate([x, y])
        assert float(((niches ^ keep) - hole).area()) == 0.0, a


def _hull2d(points):
    pts = sorted({(round(float(x), 6), round(float(y), 6)) for x, y in points})

    def half(seq):
        out = []
        for q in seq:
            while len(out) >= 2 and (out[-1][0] - out[-2][0]) * (q[1] - out[-2][1]) - (out[-1][1] - out[-2][1]) * (q[0] - out[-2][0]) <= 0:
                out.pop()
            out.append(q)
        return out

    lower, upper = half(pts), half(pts[::-1])
    return np.array(lower[:-1] + upper[:-1])


def _feet_support():
    v = mesh("asm_feet").vertices
    return _hull2d(v[v[:, 2] < v[:, 2].min() + 1e-6][:, :2])


def _disc_support(foot_r, foot_angles, rc):
    ts = np.linspace(0, 2 * np.pi, 64, endpoint=False)
    return _hull2d([np.array(_xy(foot_r, a)) + rc * np.array([np.cos(t), np.sin(t)]) for a in foot_angles for t in ts])


def _tip_ratio(support):
    lo = np.minimum(mesh("asm_body").bounds[0], mesh("asm_tray").bounds[0])
    hi = np.maximum(mesh("asm_body").bounds[1], mesh("asm_tray").bounds[1])
    com = ((lo + hi) / 2)[:2]
    cap = mesh("asm_button_cap").bounds
    press = ((cap[0] + cap[1]) / 2)[:2]
    best = np.inf
    for a, b in zip(support, np.roll(support, -1, axis=0)):
        edge = b - a
        n = np.array([edge[1], -edge[0]]) / np.linalg.norm(edge)
        inside = -np.dot(com - a, n)
        outside = np.dot(press - a, n)
        if outside > 1e-6:
            best = min(best, inside / outside)
    return best


def test_device_does_not_tip_on_button_press():
    p = params()
    support = _feet_support()
    assert len(support) >= 4
    best = _tip_ratio(support)
    assert np.isfinite(best)
    assert best >= 1.4


def test_tipping_check_rejects_the_old_three_foot_layout():
    b = params()["button_az"]
    assert _tip_ratio(_disc_support(40.0, [b + 180, b + 300, b + 60], 6.0 - 0.6)) < 1.4


def _scan(point_fn, lo, hi, step=0.05, size=0.04):
    vals = np.arange(lo, hi, step)
    return vals, np.array([solid_at("asm_tray", point_fn(v), size) for v in vals])


def _latch_cases():
    p = params()
    zt_board = p["dk_z1"]
    amp_top = p["amp_z0"] + p["amp_t"]
    y0, y1 = p["dk_y0"], p["dk_y1"]
    hy = p["amp_l"] / 2
    dk = (p["clr"], p["latch_relief"])
    amp = (p["amp_clr"], p["amp_latch_relief"])
    return [
        ("front", p["dkf_latch_t"], p["dkf_latch_ov"], zt_board, p["dkf_latch_strain"],
         lambda xl, z: (0.0, y0 + xl, z), *dk),
        ("back", p["dkb_latch_t"], p["dkb_latch_ov"], zt_board, p["dkb_latch_strain"],
         lambda xl, z: (7.4, y1 - xl, z), *dk),
        ("amp", p["amp_latch_t"], p["amp_latch_ov"], amp_top, p["amp_latch_strain"],
         lambda xl, z: (p["amp_x"] + p["amp_latch_dx"], p["amp_y"] + hy - xl, z), *amp),
        ("amp_lo", p["amp_latch_t"], p["amp_latch_ov"], amp_top, p["amp_latch_strain"],
         lambda xl, z: (p["amp_x"] - p["amp_latch_dx"], p["amp_y"] - hy + xl, z), *amp),
    ]


@pytest.mark.parametrize("case", _latch_cases(), ids=lambda c: c[0])
def test_latch_root_strain_is_derated_and_matches_the_mesh(case):
    p = params()
    name, t, ov, top, strain, g, clr, relief = case
    assert strain <= 0.016
    zr = p["tray_floor_t"] - relief
    zt = top + p["latch_vg"] - p["latch_k"] * clr + p["latch_k"] * (ov + clr)
    xs, hit = _scan(lambda v: g(v, zr + 4.0), -3.0, 1.5)
    arm = xs[hit]
    assert arm.min() == pytest.approx(-clr - t, abs=0.06)
    assert arm.max() == pytest.approx(-clr, abs=0.06)
    xs, hit = _scan(lambda v: g(v, zt - 0.02), -3.0, 2.0)
    assert xs[hit].max() == pytest.approx(ov, abs=0.08)
    zs, col = _scan(lambda v: g(-clr - t - 0.1, v), zr - 1.0, p["tray_floor_t"] + 1.0)
    root = zs[col].max()
    assert root == pytest.approx(zr, abs=0.06)
    assert not solid_at("asm_tray", g(-clr - t - 0.1, zr + 0.5), 0.04)
    assert not solid_at("asm_tray", g(-clr + p["latch_root_fillet"] + 0.1, zr + 0.5), 0.04)
    measured = 1.5 * t * (ov + clr) / (zt - root) ** 2
    assert measured <= 0.016
    assert measured == pytest.approx(strain, rel=0.08)
    assert solid_at("asm_tray", g(-clr + 0.1, zr + 0.15), 0.04)
    assert not solid_at("asm_tray", g(-clr + p["latch_root_fillet"] - 0.05, zr + 0.6), 0.04)


def test_latch_reliefs_stay_inside_the_tray_edge():
    p = params()
    edge = p["tray_neck_r"]
    for name, t, ov, top, strain, g, clr, relief in _latch_cases():
        lat = {"front": 5.0, "back": 1.8, "amp": 2.0, "amp_lo": 2.0}[name] + p["latch_relief_clr"]
        pts = [
            g(xl, p["tray_floor_t"] - 0.01)
            for xl in (-clr - t - p["latch_relief_clr"], -clr + p["latch_root_fillet"] + p["latch_relief_clr"])
        ]
        for q in pts:
            for dl in (-lat, lat):
                qq = (q[0] + dl, q[1])
                assert np.hypot(*qq) <= edge - 0.8


def test_amp_dummy_matches_the_measured_module():
    p = params()
    assert p["amp_w"] == pytest.approx(18.7)
    assert p["amp_l"] == pytest.approx(17.7)
    assert p["amp_total_h"] == pytest.approx(10.4)
    b = mesh("asm_amp").bounds
    assert b[1][0] - b[0][0] == pytest.approx(p["amp_w"], abs=1e-4)
    assert b[1][1] - b[0][1] == pytest.approx(max(p["amp_l"], p["amp_hdr_n"] * p["amp_hdr_pitch"]), abs=1e-4)
    assert p["amp_hdr_n"] * p["amp_hdr_pitch"] - p["amp_l"] < 0.2
    assert b[1][2] - b[0][2] == pytest.approx(p["amp_total_h"] + p["amp_hdr_tail"], abs=1e-4)
    assert b[0][2] == pytest.approx(p["amp_z0"] - p["amp_hdr_tail"], abs=1e-4)


def test_amp_cradle_wraps_the_measured_board():
    p = params()
    x0 = p["amp_x"] - p["amp_w"] / 2
    x1 = p["amp_x"] + p["amp_w"] / 2
    ya0 = p["amp_y"] - p["amp_l"] / 2
    ya1 = p["amp_y"] + p["amp_l"] / 2
    zt = p["amp_z0"] + p["amp_t"]
    c = p["amp_clr"]
    assert not solid_at("asm_tray", (x0 - 0.1, p["amp_y"], zt - 0.2))
    assert not solid_at("asm_tray", (x1 + 0.1, p["amp_y"] + 8, zt - 0.2))
    assert solid_at("asm_tray", (x1 + c + 0.2, p["amp_y"] + 8, zt - 0.2), 0.05)
    assert solid_at("asm_tray", (x0 + 1.0, ya1 + c + 0.2, zt - 0.2), 0.05)
    assert solid_at("asm_tray", (x0 + 1.0, ya0 - c - 0.2, zt - 0.2), 0.05)
    assert overlap("asm_tray", "asm_amp") < TOL


def test_amp_pocket_has_0_45_clearance_per_side_on_both_axes():
    p = params()
    assert p["amp_clr"] == pytest.approx(0.45)
    assert p["clr"] == pytest.approx(0.2)
    c = p["amp_clr"]
    ax, ay = p["amp_x"], p["amp_y"]
    hx, hy = p["amp_w"] / 2, p["amp_l"] / 2
    z = p["amp_z0"] + p["amp_t"] - 0.2
    y_stop = hy - p["amp_stop_len"] / 2
    for sx in (-1, 1):
        for sy in (-1, 1):
            assert not solid_at("asm_tray", (ax + sx * (hx + c - 0.05), ay + sy * y_stop, z), 0.04)
            assert solid_at("asm_tray", (ax + sx * (hx + c + 0.05), ay + sy * y_stop, z), 0.04)
            x_seg = hx - p["amp_seg_len"] / 2
            assert not solid_at("asm_tray", (ax + sx * x_seg, ay + sy * (hy + c - 0.05), z), 0.04)
            assert solid_at("asm_tray", (ax + sx * x_seg, ay + sy * (hy + c + 0.05), z), 0.04)
            x_latch = sx * p["amp_latch_dx"]
            z_arm = p["tray_floor_t"] + 1.0
            assert not solid_at("asm_tray", (ax + x_latch, ay + sy * (hy + c - 0.05), z_arm), 0.04)
            assert solid_at("asm_tray", (ax + x_latch, ay + sy * (hy + c + 0.05), z_arm), 0.04)
    dk = p["dk_w"] / 2 + p["clr"]
    assert not solid_at("asm_tray", (dk - 0.05, p["dk_y1"] - 4, p["dk_z1"] - 0.2), 0.04)
    assert solid_at("asm_tray", (dk + 0.05, p["dk_y1"] - 4, p["dk_z1"] - 0.2), 0.04)


def test_amp_latches_keep_their_grip_over_the_board_edge():
    p = params()
    c = p["amp_clr"]
    ov = p["amp_latch_ov"]
    assert ov == pytest.approx(0.5)
    hy = p["amp_l"] / 2
    zt = p["amp_z0"] + p["amp_t"] + p["latch_vg"] + p["latch_k"] * ov
    for sx in (-1, 1):
        for sy in (-1, 1):
            x = p["amp_x"] + sx * p["amp_latch_dx"]
            edge = p["amp_y"] + sy * hy
            assert solid_at("asm_tray", (x, edge - sy * (ov - 0.08), zt), 0.04)
            assert not solid_at("asm_tray", (x, edge - sy * (ov + 0.08), zt), 0.04)
            assert solid_at("asm_tray", (x, edge + sy * (c + 0.05), zt), 0.04)
    assert p["amp_latch_strain"] <= 0.016
    assert p["amp_latch_strain"] == pytest.approx(
        1.5 * p["amp_latch_t"] * (ov + c) / (zt - (p["tray_floor_t"] - p["amp_latch_relief"])) ** 2, rel=1e-4)


@pytest.mark.parametrize("sy", [-1, 1])
@pytest.mark.parametrize("sx", [-1, 1])
def test_amp_latch_reliefs_leave_the_corner_pads_on_solid_floor(sx, sy):
    p = params()
    zr = p["tray_floor_t"] - p["amp_latch_relief"]
    x_pad = p["amp_x"] + sx * (p["amp_w"] / 2 - 1.5)
    y_edge = p["amp_y"] + sy * p["amp_l"] / 2
    assert solid_at("asm_tray", (x_pad, y_edge - sy * 0.4, zr + 1.0), 0.05)
    x_arm = p["amp_x"] + sx * p["amp_latch_dx"]
    gap = p["amp_clr"] + p["amp_latch_t"] + p["latch_relief_clr"] / 2
    assert not solid_at("asm_tray", (x_arm, y_edge + sy * gap, zr + 1.0), 0.03)
    assert solid_at("asm_tray", (x_arm, y_edge + sy * gap, zr - 0.3), 0.03)
    assert p["amp_w"] / 2 - p["amp_pad"] > p["amp_latch_dx"] + p["amp_latch_w"] / 2 + p["latch_relief_clr"]
    assert p["amp_w"] / 2 - p["amp_seg_len"] > p["amp_latch_dx"] + p["amp_latch_w"] / 2 + p["latch_relief_clr"]


def _foot_at(a):
    p = params()
    c = np.array(_xy(p["foot_r"], a))
    box = manifold3d.Manifold.cube([p["foot_l"] + 4, p["foot_w"] + 4, 20], True).rotate([0, 0, a]).translate(
        [c[0], c[1], p["tray_bot_z"]])
    return _solid("asm_feet") ^ box


def test_feet_in_niches():
    p = params()
    assert 1.5 <= p["foot_out"] <= 2.5
    assert mesh("asm_tray").bounds[0][2] - mesh("asm_feet").bounds[0][2] == pytest.approx(p["foot_out"], abs=1e-4)
    tray = _solid("asm_tray")
    tm = mesh("asm_tray")
    fm = mesh("asm_feet")
    assert len(p["foot_angles"]) == 4
    for a in p["foot_angles"]:
        rad = np.array([np.cos(np.radians(a)), np.sin(np.radians(a)), 0.0])
        tan = np.array([-rad[1], rad[0], 0.0])
        z = p["tray_bot_z"] + p["foot_niche_h"] / 2
        for off in (-p["foot_w"] / 2 + 1.0, 0.0, p["foot_w"] / 2 - 1.0):
            origin = (p["plinth_r"] + 5) * rad + off * tan + np.array([0, 0, z])
            hf = first_hit(fm, origin, -rad)
            ht = first_hit(tm, origin, -rad)
            assert hf is not None and (ht is None or ht > hf), (a, off)
            assert p["plinth_r"] + 5 - hf <= p["plinth_r"] - 0.3
        c = p["foot_r"] * rad
        roof = p["tray_bot_z"] + p["foot_niche_h"]
        assert solid_at("asm_tray", tuple(c + np.array([0, 0, roof + 0.1])), 0.05)
        assert not solid_at("asm_tray", tuple(c + np.array([0, 0, roof - 0.1])), 0.05)
        assert roof <= -p["tray_gasket_t"] - 0.8 + 1e-6
        foot = _foot_at(a)
        assert float(foot.volume()) > 0.9 * p["foot_w"] * p["foot_l"] * p["foot_h"] * 0.9
        assert float((foot ^ tray).volume()) < TOL
        for d in (tan, -tan, -rad):
            assert float((foot.translate(list(0.19 * d)) ^ tray).volume()) < TOL, (a, d)
            assert float((foot.translate(list(0.51 * d)) ^ tray).volume()) > TOL, (a, d)
        assert float((foot.translate(list(3.0 * rad)) ^ tray).volume()) < TOL


def _signed(loop):
    x, y = loop[:, 0], loop[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))


def _dk_ant_holes(p):
    return [np.array([sx * p["dk_hole_x"], p["dk_y0"] + p["dk_hole_in"]]) for sx in (-1, 1)]


def _hole_near(part, z, c, r=2.5):
    found = [q for q in _loops(part, z) if _signed(q) < 0 and np.linalg.norm(q.mean(axis=0) - c) < r]
    assert len(found) == 1, (part, z, c)
    return found[0]


def test_devkit_dummy_has_the_measured_mounting_holes():
    p = params()
    assert p["dk_hole_d"] == pytest.approx(3.0)
    assert p["dk_hole_pitch"] == pytest.approx(23.2)
    assert p["dk_hole_in"] == pytest.approx(2.25)
    assert p["dk_w"] / 2 - p["dk_hole_x"] == pytest.approx(2.775)
    z = (p["dk_z0"] + p["dk_z1"]) / 2
    for y in (p["dk_y0"] + p["dk_hole_in"], p["dk_y1"] - p["dk_hole_in"]):
        for sx in (-1, 1):
            x = sx * p["dk_hole_x"]
            assert not solid_at("asm_devkit", (x, y, z), 0.05)
            for ang in np.radians(np.arange(0, 360, 45)):
                q = (x + 1.65 * np.cos(ang), y + 1.65 * np.sin(ang), z)
                assert solid_at("asm_devkit", q, 0.05), (x, y, ang)
    assert p["dk_pin_l"] == pytest.approx(15 * 2.54)


def test_devkit_standoff_inserts_are_coaxial_with_the_antenna_end_holes():
    p = params()
    depth = p["dk_insert_hole_depth"]
    for c in _dk_ant_holes(p):
        board = _hole_near("asm_devkit", p["dk_z0"] + 0.8, c)
        insert = _hole_near("asm_tray", p["dk_z0"] - 1.0, c)
        assert np.allclose(board.mean(axis=0), c, atol=0.02)
        assert np.allclose(insert.mean(axis=0), c, atol=0.02)
        assert -_signed(insert) == pytest.approx(np.pi * (p["insert_d"] / 2) ** 2, rel=0.02)
        for z in np.arange(p["dk_z0"] - depth + 0.1, p["dk_z0"], 0.5):
            assert not solid_at("asm_tray", (c[0], c[1], z), 0.05), z
        assert solid_at("asm_tray", (c[0], c[1], p["dk_z0"] - depth - 0.15), 0.05)
        out = np.sign(c[0])
        for d in ((out, 0.0), (0.0, 1.0), (0.0, -1.0)):
            q = c + (p["insert_d"] / 2 + 0.6) * np.array(d)
            assert solid_at("asm_tray", (q[0], q[1], p["dk_z0"] - 0.1), 0.05), d
            assert not solid_at("asm_tray", (q[0], q[1], p["dk_z0"] + 0.1), 0.05), d
    assert depth >= p["insert_len"] + 1
    assert depth == pytest.approx(5.4)


def test_devkit_usb_end_holes_stay_free():
    p = params()
    for sx in (-1, 1):
        x, y = sx * p["dk_hole_x"], p["dk_y1"] - p["dk_hole_in"]
        for z in (p["tray_floor_t"] + 0.5, p["dk_z0"] - 0.5, p["dk_z0"] + 0.5, p["dk_z1"] + 0.5):
            assert not solid_at("asm_tray", (x, y, z), 0.05), z


def test_devkit_standoffs_clear_the_module_by_0_3():
    p = params()
    y0 = p["dk_y0"]
    module = manifold3d.Manifold.cube([p["dk_module_w"], p["dk_module_l"], p["dk_parts_h"]]).translate(
        [-p["dk_module_w"] / 2, y0, p["dk_z0"] - p["dk_parts_h"]])
    for sx in (-1, 1):
        zone = manifold3d.Manifold.cube([10.0, 12.0, p["dk_z0"]]).translate([7.0 if sx > 0 else -17.0, y0 - 3.0, 0.0])
        standoff = _solid("asm_tray") ^ zone
        assert float(standoff.volume()) > 50.0
        gap = standoff.min_gap(module, 5.0)
        assert gap >= p["dk_mod_clr"] - 1e-3
        assert gap <= p["dk_mod_clr"] + 0.05
        ant = _solid("asm_antenna")
        assert standoff.min_gap(ant, 5.0) >= p["dk_mod_clr"] - 1e-3
    assert p["dk_mod_clr"] == pytest.approx(0.3)
    assert p["dk_standoff_wall"] == pytest.approx(
        p["dk_hole_x"] - p["insert_d"] / 2 - p["dk_module_w"] / 2 - p["dk_mod_clr"])
    assert p["dk_standoff_wall"] >= 0.6


def test_devkit_screws_sit_on_the_board_and_engage_the_inserts():
    p = params()
    s = mesh("asm_devkit_screws")
    assert s.bounds[1][2] == pytest.approx(p["dk_screw_head_z"], abs=1e-4)
    assert s.bounds[0][2] == pytest.approx(p["dk_screw_tip_z"], abs=1e-4)
    stand = p["screw_rim_h"] + (p["screw_head_d"] - p["dk_hole_d"]) / 2
    assert p["dk_screw_head_z"] - p["dk_z1"] == pytest.approx(stand, abs=1e-9)
    assert stand == pytest.approx(1.25)
    engage = p["dk_z0"] - p["dk_screw_tip_z"]
    assert engage == pytest.approx(p["dk_screw_engage"], abs=1e-9)
    assert engage >= p["insert_len"]
    assert p["dk_screw_thread"] >= p["min_thread"]
    bottom = p["dk_z0"] - p["dk_insert_hole_depth"]
    assert p["dk_screw_tip_z"] - bottom >= p["insert_tip_gap"] - 1e-6
    assert bottom > p["tray_bot_z"] + 1.5
    for c in _dk_ant_holes(p):
        assert solid_at("asm_tray", (c[0], c[1], bottom - 0.1), 0.05)
        for z in np.arange(p["tray_bot_z"] + 0.1, bottom - 0.05, 0.4):
            assert solid_at("asm_tray", (c[0], c[1], z), 0.05), z
    screws = _solid("asm_devkit_screws")
    devkit = _solid("asm_devkit")
    for name in ("asm_tray", "asm_devkit", "asm_body", "asm_wire_keepout"):
        assert float((screws ^ _solid(name)).volume()) < TOL, name
    assert float((screws.translate([0, 0, -0.2]) ^ devkit).volume()) > 0.1
    above = manifold3d.Manifold.cube([200, 200, 50]).translate([-100, -100, p["dk_z1"] + 0.01])
    assert screws.min_gap(devkit ^ above, 5.0) >= 0.5
    assert p["dk_pin_gap"] == pytest.approx((p["dk_l"] - p["dk_pin_l"]) / 2 - p["dk_hole_in"] - p["screw_head_d"] / 2)
    assert p["dk_pin_gap"] >= 0.5
    assert screws.min_gap(_solid("asm_tray"), 5.0) >= 0.1


def test_no_strap_or_printed_washer_parts_left():
    from conftest import OpenScadError, scad
    for part in ("devkit_strap", "asm_devkit_strap", "speaker_washer", "asm_speaker_washers", "asm_driver",
                 "asm_top_screw_access"):
        with pytest.raises(OpenScadError):
            scad(part)
    text = (Path(__file__).resolve().parent.parent / "build.sh").read_text()
    for name in ("devkit_strap", "speaker_washer", "speaker_clamp"):
        assert name not in text
    p = params()
    assert not any(k.startswith("dk_strap") or k.startswith("spk_washer_") and k != "spk_washer_bot_z" for k in p)
