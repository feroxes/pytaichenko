import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent

CLEAR_OF = [
    "body",
    "speaker",
    "button",
    "button_cap",
    "top_screws",
    "speaker_screws",
    "speaker_gasket",
    "filament",
    "devkit",
    "usb_plug",
    "amp",
    "cap",
    "mic",
    "tray_screws",
    "metal_washers",
    "devkit_screws",
    "feet",
    "tray",
    "mic_boot",
]

MIC_BOOT_CLEAR_OF = [
    "mic",
    "tray",
    "devkit",
    "usb_plug",
    "amp",
    "cap",
    "speaker",
    "metal_washers",
    "top_plugs",
    "filament",
    "ring",
    "top",
    "tray_screws",
    "top_screws",
    "speaker_screws",
    "feet",
    "mic_port_probe",
    "filament_channel_probe",
]

TOP_OVERHANG_MAX = 300
GASKET_OVERHANG_MAX = 5
MIC_BOOT_OVERHANG_MAX = 220


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def solid_at(part, point, s=0.05):
    cube = manifold3d.Manifold.cube([s, s, s], True).translate(list(point))
    return float((_solid(part) ^ cube).volume()) / s ** 3 > 0.5


def _xy(r, a):
    return r * np.cos(np.radians(a)), r * np.sin(np.radians(a))


def _button_xy():
    p = params()
    return _xy(p["button_r"], p["button_az"])


def _area_at(part, z):
    return float(_solid(part).slice(z).area())


def _disc(r):
    return manifold3d.CrossSection.circle(r, 360)


def _annulus(r0, r1):
    return _disc(r1) - _disc(r0)


def _w(x, y):
    a = np.radians(params()["spk_rot"])
    return x * np.cos(a) - y * np.sin(a), x * np.sin(a) + y * np.cos(a)


def solid_at_spk(part, point, s=0.05):
    return solid_at(part, (*_w(point[0], point[1]), point[2]), s)


def _spk_solid(part):
    return _solid(part).rotate([0, 0, -params()["spk_rot"]])


def _spk_bounds(part):
    x0, y0, z0, x1, y1, z1 = _spk_solid(part).bounding_box()
    return np.array([[x0, y0, z0], [x1, y1, z1]])


def _outline(inset=0.0):
    return _spk_outline(inset).rotate(params()["spk_rot"])


def _spk_outline(inset=0.0):
    p = params()
    big = p["spk_side_arc_r"]
    off = big - p["spk_flange_half"]
    rho = p["spk_flange_fillet_r"]
    shape = None
    for dx, dy in ((-off, 0.0), (off, 0.0), (0.0, -off), (0.0, off)):
        disc = manifold3d.CrossSection.circle(big, 1440).translate([dx, dy])
        shape = disc if shape is None else shape ^ disc
    shape = shape.offset(-rho, manifold3d.JoinType.Round, 2.0, 96)
    shape = shape.offset(rho, manifold3d.JoinType.Round, 2.0, 96)
    if inset:
        shape = shape.offset(-inset, manifold3d.JoinType.Round, 2.0, 96)
    return shape


def _relief_rects():
    p = params()
    bx, by = _button_xy()
    out = None
    for sy in (-1, 1):
        y0, y1 = sorted((sy * p["btn_relief_y0"], sy * p["btn_relief_y1"]))
        r = manifold3d.CrossSection.square([2 * p["btn_relief_x"], y1 - y0]).translate([-p["btn_relief_x"], y0])
        r = r.rotate(p["button_az"]).translate([bx, by])
        out = r if out is None else out + r
    return out


def _seat_outline():
    return _outline(-params()["spk_seat_flange_clr"])


def _gasket_band():
    return _outline() - _disc(params()["gasket_inner_r"])


def _seat_band():
    return _seat_outline() - _disc(params()["gasket_inner_r"])


def _seat_slice(z):
    p = params()
    near = _grow(_outline(), 2.0)
    sl = _solid("asm_top").slice(z)
    assert float((sl ^ (_grow(_outline(), 2.5) - _grow(_outline(), 0.85))).area()) == 0.0
    return sl ^ near


def test_top_watertight_fits_bed():
    p = params()
    m = mesh("top")
    assert m.is_watertight
    assert m.volume > 0
    assert m.extents[0] < p["bed"] and m.extents[1] < p["bed"]
    assert abs(m.bounds[0][2]) < 1e-6


def test_print_top_is_the_flipped_assembly():
    p = params()
    a = mesh("asm_top")
    b = mesh("top")
    assert b.volume == pytest.approx(a.volume, rel=1e-6)
    holder_low = min(p["btn_hook_z0"], p["gasket_z1"], p["top_z0"], p["top_boss_z0"])
    assert holder_low == pytest.approx(p["gasket_z1"], abs=1e-9)
    assert b.extents[2] == pytest.approx(p["H"] - holder_low, abs=1e-4)
    assert a.bounds[0][2] == pytest.approx(holder_low, abs=1e-4)
    assert a.bounds[1][2] == pytest.approx(p["H"], abs=1e-4)
    assert np.allclose(a.bounds[:, :2], b.bounds[:, :2], atol=1e-6)


def test_top_prints_face_down_on_a_flat_face():
    p = params()
    flat = _area_at("top", 0.05)
    open_r = p["dimple_d"] / 2 * (p["dimple_depth"] - 0.05) / p["dimple_depth"]
    expect = np.pi * (p["top_face_r"] ** 2 - p["hole_count"] * (p["grille_hole_d"] / 2) ** 2
                      - p["dimple_count"] * open_r ** 2 - (p["button_hole_d"] / 2) ** 2
                      - len(p["top_screw_angles"]) * (p["top_plug_d"] / 2) ** 2)
    assert flat == pytest.approx(expect, rel=0.03)
    assert flat > 0.6 * np.pi * p["top_face_r"] ** 2


@pytest.mark.parametrize("name", CLEAR_OF)
def test_top_clear(name):
    assert overlap("asm_top", f"asm_{name}") < TOL


def test_top_flush():
    p = params()
    m = mesh("asm_top")
    full = 2 * p["ring_outer_r"]
    assert m.extents[0] <= full + 0.1
    assert m.extents[1] <= full + 0.1
    assert m.extents[0] >= full - 0.1
    assert m.extents[1] >= full - 0.1
    for z in (p["top_z0"] + 0.05, p["top_z0"] + 1.5, p["top_edge_z0"] - 0.05):
        loops = _solid("asm_top").slice(z).to_polygons()
        r = max(np.linalg.norm(np.asarray(q), axis=1).max() for q in loops)
        assert r == pytest.approx(p["ring_outer_r"], abs=0.02)
    r_all = np.linalg.norm(m.vertices[:, :2], axis=1)
    assert r_all.max() <= p["ring_outer_r"] + 0.01


def _button_pocket():
    p = params()
    bx, by = _button_xy()
    side = 2 * p["btn_in"]
    return manifold3d.CrossSection.square([side, side], True).rotate(p["button_az"]).translate([bx, by])


def test_top_seats_on_the_body_rim():
    p = params()
    below = _solid("asm_top").slice(p["top_z0"] - 0.05)
    inside = _disc(p["ring_in_r"] - p["ring_in_clr"])
    assert float((below - inside).area()) == 0.0
    rim = float(_seat_band().area()) - len(p["spk_hole_angles"]) * np.pi * (p["insert_d"] / 2) ** 2
    assert float(below.area()) == pytest.approx(rim, rel=0.002)
    assert p["gasket_z1"] < p["top_z0"] < p["top_edge_z0"] and p["btn_hook_z0"] > p["top_z0"]
    skirt = _solid("asm_top").slice(p["top_z0"] + 0.05)
    seat = _annulus(p["top_skirt_in_r"], p["ring_outer_r"])
    expect = seat - _button_pocket()
    assert float((skirt ^ seat).area()) == pytest.approx(float(expect.area()), rel=1e-4)
    assert float((expect - skirt).area()) < 0.05
    assert float((seat ^ _button_pocket()).area()) > 10.0
    inner = float(((skirt ^ _disc(p["top_skirt_in_r"] - 0.01)) - _grow(_seat_band(), 0.05)).area())
    assert inner < 1e-3
    assert p["top_skirt_in_r"] >= p["arc_top_in_r"] + 0.3
    top_ring = mesh("asm_ring")
    at_top = top_ring.vertices[np.abs(top_ring.vertices[:, 2] - p["ring_z0"] - p["ring_arc_h"]) < 0.01]
    assert np.linalg.norm(at_top[:, :2], axis=1).min() == pytest.approx(p["arc_top_in_r"], abs=0.01)


def test_grille():
    p = params()
    assert p["grille_open_ratio"] >= 0.30
    assert p["grille_open_ratio"] == pytest.approx(0.354, abs=0.001)
    assert 3 <= p["cone_gap"] <= 5
    plate_under = p["top_underside_z"]
    assert plate_under - p["spk_surround_top_z"] == pytest.approx(p["cone_gap"], abs=1e-9)
    cone = _disc(p["spk_surround_d"] / 2)
    open_actual = 1 - float((_solid("asm_top").slice(plate_under + 1.0) ^ cone).area()) / float(cone.area())
    assert open_actual >= 0.30


def _loops(part, z):
    return [np.asarray(q) for q in _solid(part).slice(z).to_polygons()]


def _signed_area(q):
    x, y = q[:, 0], q[:, 1]
    return 0.5 * float(np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y))


def test_through_holes_only_inside_gasket_circle():
    p = params()
    bx, by = _button_xy()
    holes = [q for q in _loops("asm_top", p["top_underside_z"] + 1.0) if _signed_area(q) < 0]
    screws = [_xy(p["top_screw_r"], a) for a in p["top_screw_angles"]]
    at_screws = [q for q in holes if min(np.linalg.norm(q.mean(axis=0) - c) for c in screws) < 1.0]
    assert len(at_screws) == len(screws)
    mids = [_button_local(p, 0.0, sy * (p["btn_relief_y0"] + p["btn_relief_y1"]) / 2) for sy in (-1, 1)]
    reliefs = [q for q in holes if min(np.linalg.norm(q.mean(axis=0) - m) for m in mids) < 1.5]
    assert len(reliefs) == 2
    others = [q for q in holes if np.linalg.norm(q.mean(axis=0) - [bx, by]) > 1.0
              and not any(q is h for h in at_screws + reliefs)]
    assert len(holes) == len(others) + 1 + len(at_screws) + len(reliefs)
    assert len(others) == int(p["hole_count"])
    outside = [q for q in others if np.linalg.norm(q.mean(axis=0)) > p["gasket_inner_r"]]
    assert len(outside) == 0
    assert max(np.linalg.norm(q, axis=1).max() for q in others) < p["gasket_inner_r"]


def test_dimples_are_blind():
    p = params()
    zone = _annulus(p["gasket_inner_r"] + 1, p["dimple_r"] - 1.0)
    for a in p["top_screw_angles"]:
        zone = zone - manifold3d.CrossSection.circle(p["top_plug_d"] / 2 + 0.1, 128).translate(list(_xy(p["top_screw_r"], a)))
    zone = zone - _relief_rects().offset(0.1, manifold3d.JoinType.Miter)
    full = float((zone - manifold3d.CrossSection.circle(p["button_hole_d"] / 2, 128).translate(list(_button_xy()))).area())
    z_floor = p["H"] - p["dimple_depth"] - 0.05
    assert float((_solid("asm_top").slice(z_floor) ^ zone).area()) == pytest.approx(full, rel=1e-3)
    z_dim = p["H"] - 0.5
    shallow = float((_solid("asm_top").slice(z_dim) ^ zone).area())
    assert shallow < 0.97 * full
    assert shallow > 0.6 * full


def test_dimples_depth_leaves_a_skin():
    p = params()
    assert p["top_plate_t"] - p["dimple_depth"] >= 1.5


def test_top_sealed_outside_gasket():
    p = params()
    m = mesh("asm_top")
    bx, by = _button_xy()
    z0 = p["top_underside_z"] - 0.5
    r0 = p["gasket_inner_r"] + 0.3
    r1 = p["ring_outer_r"] - p["wall_t"] - 1
    n = 200
    golden = np.pi * (3 - np.sqrt(5))
    count = 0
    for i in range(n):
        r = np.sqrt(r0 ** 2 + (r1 ** 2 - r0 ** 2) * (i + 0.5) / n)
        th = i * golden
        x, y = r * np.cos(th), r * np.sin(th)
        if np.hypot(x - bx, y - by) < p["button_hole_d"] / 2 + 0.5:
            continue
        if min(np.hypot(x - sx, y - sy) for sx, sy in (_xy(p["top_screw_r"], a) for a in p["top_screw_angles"])) < p["screw_thru_d"] / 2 + 0.5:
            continue
        count += 1
        assert first_hit(m, (x, y, z0), (0, 0, 1)) is not None, (x, y)
    assert count >= 188


def test_grille_holes_are_4_mm_at_6_4_pitch_with_2_4_ribs():
    p = params()
    assert p["grille_hole_d"] == pytest.approx(4.0)
    assert p["grille_pitch"] == pytest.approx(6.4)
    assert p["grille_pitch"] - p["grille_hole_d"] == pytest.approx(2.4)
    assert p["dimple_d"] == pytest.approx(p["grille_hole_d"])
    assert p["dimple_depth"] == pytest.approx(1.4)
    pitch = p["grille_pitch"]
    row = pitch * np.sqrt(3) / 2
    r_hole = p["grille_hole_d"] / 2
    z = p["top_underside_z"] + p["top_plate_t"] / 2
    for cx, cy in ((0.0, 0.0), (pitch, 0.0), (pitch / 2, row), (-pitch, 2 * row)):
        for ang in np.radians(np.arange(0, 360, 30)):
            d = np.array([np.cos(ang), np.sin(ang)])
            assert not solid_at("asm_top", (cx + (r_hole - 0.1) * d[0], cy + (r_hole - 0.1) * d[1], z), 0.04)
            assert solid_at("asm_top", (cx + (r_hole + 0.1) * d[0], cy + (r_hole + 0.1) * d[1], z), 0.04)
    for k in np.linspace(r_hole + 0.1, pitch - r_hole - 0.1, 9):
        assert solid_at("asm_top", (k, 0.0, z), 0.04)
    loops = [q for q in _loops("asm_top", z) if _signed_area(q) < 0 and np.linalg.norm(q.mean(axis=0)) < p["grille_r"]]
    areas = np.array([-_signed_area(q) for q in loops])
    assert len(loops) == int(p["hole_count"])
    assert areas.mean() == pytest.approx(np.pi * r_hole ** 2, rel=0.02)
    bx, by = _button_xy()
    plugs = [_xy(p["top_screw_r"], a) for a in p["top_screw_angles"]]
    dimples = 0
    for q in _loops("asm_top", p["H"] - 0.02):
        c = q.mean(axis=0)
        if min(np.hypot(c[0] - px, c[1] - py) for px, py in plugs) < 1.0:
            continue
        if _signed_area(q) < 0 and np.hypot(c[0] - bx, c[1] - by) > 1.0 and np.linalg.norm(c) > p["grille_r"] + 0.1:
            dimples += 1
            assert np.linalg.norm(q - c, axis=1).max() == pytest.approx(p["dimple_d"] / 2, abs=0.05)
    assert dimples == int(p["dimple_count"])


def test_through_holes_exist_inside():
    p = params()
    m = mesh("asm_top")
    z0 = p["top_underside_z"] - 0.5
    pitch = p["grille_pitch"]
    row = pitch * np.sqrt(3) / 2
    for x, y in [(0, 0), (pitch, 0), (pitch / 2, row), (-pitch / 2, row), (2 * pitch, 0), (0, 2 * row), (3 * pitch, 0)]:
        assert first_hit(m, (x, y, z0), (0, 0, 1)) is None, (x, y)


def test_speaker_bosses_and_inserts():
    p = params()
    for a in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], a)
        z_open = p["gasket_z1"]
        for dz in (0.3, 2.0, p["spk_insert_hole_depth"] - 0.4):
            assert not solid_at("asm_top", (x, y, z_open + dz))
        assert solid_at("asm_top", (x, y, p["spk_insert_top_z"] + 0.2))
        assert solid_at("asm_top", (x, y, p["spk_insert_top_z"] + p["boss_skin"] - 0.3))
        reach = p["insert_d"] / 2 + 1.55
        for ang in (0, 90, 180, 270):
            qx = x + reach * np.cos(np.radians(ang))
            qy = y + reach * np.sin(np.radians(ang))
            for dz in (0.3, 2.0, 5.0):
                assert solid_at("asm_top", (qx, qy, z_open + dz))
        assert p["boss_wall"] >= 1.6
        assert p["H"] - p["spk_insert_top_z"] >= 1.6


@pytest.mark.parametrize("b", [20, 200])
def test_top_screw_hole_seat_and_plug_pocket_go_through_from_the_face(b):
    p = params()
    assert b in p["top_screw_angles"]
    x, y = _xy(p["top_screw_r"], b)
    H = p["H"]
    seat = p["top_seat_z"]
    assert seat == pytest.approx(H - p["top_plug_depth"], abs=1e-9)
    assert p["top_plug_depth"] == pytest.approx(1.2)
    assert first_hit(mesh("asm_top"), (x, y, p["top_boss_z0"] - 1.0), (0, 0, 1)) is None
    plug_r, csk_r, thru_r = p["top_plug_d"] / 2, p["csk_d"] / 2, p["screw_thru_d"] / 2
    for ang in range(0, 360, 30):
        d = np.array([np.cos(np.radians(ang)), np.sin(np.radians(ang))])

        def at(r, z):
            return (x + r * d[0], y + r * d[1], z)

        for z in (seat + 0.1, H - 0.1):
            assert not solid_at("asm_top", at(plug_r - 0.1, z)), (ang, z)
            assert solid_at("asm_top", at(plug_r + 0.1, z)), (ang, z)
        assert solid_at("asm_top", at(csk_r + 0.2, seat - 0.1)), ang
        assert not solid_at("asm_top", at(csk_r - 0.1, seat - 0.2)), ang
        zc = seat - p["csk_bore_h"] - 0.5
        assert not solid_at("asm_top", at(csk_r - 0.6, zc)), ang
        assert solid_at("asm_top", at(csk_r - 0.4, zc)), ang
        for z in (seat - p["csk_h"] - 0.2, p["top_underside_z"] - 0.5, p["top_boss_z0"] + 0.2):
            assert not solid_at("asm_top", at(thru_r - 0.1, z)), (ang, z)
            assert solid_at("asm_top", at(thru_r + 0.1, z)), (ang, z)
        for z in (p["top_boss_z0"] + 0.2, p["top_underside_z"] - 0.2):
            assert solid_at("asm_top", at(p["boss_od"] / 2 - 0.15, z)), (ang, z)
            assert not solid_at("asm_top", at(p["boss_od"] / 2 + 0.15, z)), (ang, z)
        assert not solid_at("asm_top", at(1.0, p["top_boss_z0"] - 0.1)), ang
    assert p["top_seat_floor_t"] == pytest.approx(seat - p["csk_h"] - p["top_boss_z0"], abs=1e-9)
    assert p["top_seat_floor_t"] >= 1.6
    assert p["top_screw_r"] + p["top_plug_d"] / 2 + 1.0 <= p["top_face_r"]
    face = _solid("asm_top").slice(H - 0.05)
    ring = _annulus(plug_r + 0.05, plug_r + p["top_plug_rim"]).translate([x, y])
    assert float((face ^ ring).area()) == pytest.approx(float(ring.area()), rel=0.01)


def test_top_plugs_sit_flush_in_their_pockets_with_a_light_press_fit():
    p = params()
    plugs = mesh("asm_top_plugs")
    assert _components(plugs) == len(p["top_screw_angles"])
    assert plugs.bounds[1][2] == pytest.approx(p["H"], abs=1e-4)
    assert plugs.bounds[0][2] == pytest.approx(p["top_seat_z"], abs=1e-4)
    d = p["top_plug_d"] + p["top_plug_fit"]
    straight = p["top_plug_depth"] - p["top_plug_chamfer"]
    press = float((_solid("asm_top_plugs") ^ _solid("asm_top")).volume())
    expect = len(p["top_screw_angles"]) * np.pi / 4 * (d * d - p["top_plug_d"] ** 2) * straight
    assert press == pytest.approx(expect, rel=0.15)
    assert float((_solid("asm_top_plugs").translate([0, 0, -0.05]) ^ _solid("asm_top")).volume()) > press + 0.5
    face = _solid("asm_top").slice(p["H"] - 0.02) + _solid("asm_top_plugs").slice(p["H"] - 0.02)
    for a in p["top_screw_angles"]:
        x, y = _xy(p["top_screw_r"], a)
        disc = manifold3d.CrossSection.circle(p["top_plug_d"] / 2 + 0.5, 96).translate([x, y])
        assert float((face ^ disc).area()) == pytest.approx(float(disc.area()), rel=1e-3)
    screws = _solid("asm_top_screws")
    gap = _solid("asm_top_plugs").min_gap(screws, 1.0)
    assert gap == pytest.approx(p["csk_head_z"], abs=0.01)


def _hole_profile(x, y, z_lo, z_hi, side=2.6):
    zs = np.round(np.arange(z_lo + 0.05, z_hi, 0.1), 3)
    boss = [z for z in zs if solid_at("asm_top", (x + side, y, z))]
    bottom = min(boss)
    top = min(z for z in zs if z > bottom + 0.5 and solid_at("asm_top", (x, y, z)))
    return bottom, top


def _screw_tip(part):
    return float(mesh(part).bounds[1][2])


def test_speaker_screws_engage():
    p = params()
    tip = _screw_tip("asm_speaker_screws")
    assert tip == pytest.approx(p["spk_screw_tip_z"], abs=1e-4)
    for a in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], a)
        bottom, top = _hole_profile(x, y, p["gasket_z0"], p["H"] - 1.0)
        assert min(p["insert_len"], tip - bottom) >= p["min_thread"]
        assert top - tip >= p["insert_tip_gap"] - 1e-6
        assert top - bottom >= p["insert_len"] + 1 - 1e-6
    screws = mesh("asm_top_screws")
    assert screws.bounds[1][2] == pytest.approx(p["top_screw_head_z"], abs=1e-4)
    tip = float(screws.bounds[0][2])
    assert tip == pytest.approx(p["top_screw_tip_z"], abs=1e-4)
    assert p["top_seat_z"] - p["top_screw_head_z"] == pytest.approx(p["csk_head_z"], abs=1e-9)
    for a in p["top_screw_angles"]:
        x, y = _xy(p["top_screw_r"], a)
        zs = np.round(np.arange(p["top_ledge_z0"] + 0.05, p["top_col_z1"] + 0.5, 0.1), 3)
        col_top = max(z for z in zs if solid_at("asm_body", (x + 2.6, y, z)))
        hole_bot = min(z for z in zs if z > p["top_ledge_z0"] + 1.0 and not solid_at("asm_body", (x, y, z)))
        assert col_top == pytest.approx(p["top_col_z1"], abs=0.11)
        assert hole_bot == pytest.approx(p["top_insert_bot_z"], abs=0.11)
        assert col_top - tip == pytest.approx(p["top_screw_engage"], abs=0.15)
        assert col_top - tip >= p["insert_len"]
        assert min(p["insert_len"], col_top - tip) >= p["min_thread"]
        assert tip - hole_bot >= p["insert_tip_gap"] - 0.1 - 1e-6
        assert col_top - hole_bot >= p["insert_len"] + 1 - 0.1
    s = _solid("asm_top_screws")
    for name in ("asm_top", "asm_body", "asm_top_plugs"):
        assert float((s ^ _solid(name)).volume()) < TOL, name
    assert float((s.translate([0, 0, -0.3]) ^ _solid("asm_top")).volume()) > 1.0


def _area_of(part, z):
    return float(_solid(part).slice(z).area())


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


def test_metal_washers_are_din125_m2_5_under_each_flange_corner():
    p = params()
    assert p["washer_id"] == pytest.approx(2.7)
    assert p["washer_od"] == pytest.approx(6.0)
    assert p["washer_t"] == pytest.approx(0.5)
    w = mesh("asm_metal_washers")
    assert w.is_watertight
    assert _components(w) == 4
    assert w.bounds[0][2] == pytest.approx(p["spk_washer_bot_z"], abs=1e-4)
    assert w.bounds[1][2] == pytest.approx(p["spk_flange_bot_z"], abs=1e-4)
    zm = p["spk_washer_bot_z"] + p["washer_t"] / 2
    for a in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], a)
        assert not solid_at("asm_metal_washers", (x, y, zm))
        for ang in range(0, 360, 45):
            c, s_ = np.cos(np.radians(ang)), np.sin(np.radians(ang))
            assert solid_at("asm_metal_washers", (x + 2.0 * c, y + 2.0 * s_, zm)), (a, ang)
            assert not solid_at("asm_metal_washers", (x + 3.1 * c, y + 3.1 * s_, zm)), (a, ang)
    washers = _solid("asm_metal_washers")
    speaker = _solid("asm_speaker")
    assert float((washers ^ speaker).volume()) < TOL
    assert float((washers.translate([0, 0, 0.1]) ^ speaker).volume()) > 2.0
    assert p["washer_od"] >= p["spk_hole_l"] + 0.5
    assert p["spk_hole_r"] - p["washer_od"] / 2 >= p["spk_basket_top_d"] / 2 + 1.0
    assert p["spk_hole_edge_dist"] - p["washer_od"] / 2 >= 1.0
    outline = _outline()
    for a in p["spk_hole_angles"]:
        disc = manifold3d.CrossSection.circle(p["washer_od"] / 2, 96).translate(list(_xy(p["spk_hole_r"], a)))
        assert float((disc - outline).area()) == 0.0


@pytest.mark.parametrize(
    "name",
    [
        "speaker_gasket",
        "body",
        "top",
        "top_screws",
        "top_plugs",
        "tray",
        "tray_screws",
        "devkit",
        "amp",
        "cap",
        "mic",
        "button",
        "button_cap",
        "ring",
        "filament",
        "mic_boot",
        "wire_keepout",
    ],
)
def test_speaker_screw_heads_and_washers_clear_of(name):
    assert overlap("asm_speaker_screws", f"asm_{name}") < TOL
    assert overlap("asm_metal_washers", f"asm_{name}") < TOL


def test_speaker_screws_cannot_pull_through_the_washers_and_the_flange_stack_is_continuous():
    p = params()
    screws = _solid("asm_speaker_screws")
    washers = _solid("asm_metal_washers")
    assert float((screws ^ washers).volume()) < TOL
    assert float((screws.translate([0, 0, 0.3]) ^ washers).volume()) > 0.5
    assert mesh("asm_speaker_screws").bounds[0][2] == pytest.approx(p["spk_screw_head_z"], abs=1e-4)
    stand = p["screw_rim_h"] + (p["screw_head_d"] - p["washer_id"]) / 2
    assert p["spk_washer_bot_z"] - p["spk_screw_head_z"] == pytest.approx(stand, abs=1e-9)
    assert stand == pytest.approx(1.4)
    assert p["spk_washer_bot_z"] + p["washer_t"] == pytest.approx(p["spk_flange_bot_z"])
    assert p["spk_flange_bot_z"] + p["spk_flange_t"] == pytest.approx(p["gasket_z0"])
    assert p["gasket_z0"] + p["gasket_t"] == pytest.approx(p["gasket_z1"])
    assert mesh("asm_speaker_gasket").bounds[1][2] == pytest.approx(p["gasket_z1"], abs=1e-4)
    assert p["washer_id"] < p["screw_head_d"] - 1.0
    assert p["spk_hole_l"] > p["screw_head_d"] - 0.5


def test_speaker_screw_stack_engagement_and_skin():
    p = params()
    tip = _screw_tip("asm_speaker_screws")
    assert tip == pytest.approx(p["spk_screw_tip_z"], abs=1e-4)
    stack = p["washer_t"] + p["spk_flange_t"] + p["gasket_t"]
    assert p["gasket_z1"] - p["spk_washer_bot_z"] == pytest.approx(stack)
    assert p["spk_screw_engage"] == pytest.approx(tip - p["gasket_z1"], abs=1e-4)
    assert p["spk_screw_engage"] >= p["insert_len"]
    assert min(p["insert_len"], p["spk_screw_engage"]) >= p["min_thread"]
    assert p["spk_insert_hole_depth"] >= p["insert_len"] + 1
    assert tip + p["insert_tip_gap"] <= p["spk_insert_top_z"] + 1e-9
    assert p["H"] - p["spk_insert_top_z"] >= 1.6
    assert p["boss_skin"] >= 1.6
    assert p["spk_insert_top_z"] <= p["top_underside_z"]
    for a in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], a)
        bottom, top = _hole_profile(x, y, p["gasket_z0"], p["H"] - 1.0)
        assert tip - bottom >= p["min_thread"]
        assert top - tip >= p["insert_tip_gap"] - 1e-6
        assert top - bottom >= p["insert_len"] + 1 - 1e-6


def test_speaker_screw_passes_the_oval_hole_and_the_gasket_hole():
    p = params()
    for a in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], a)
        for z in (p["spk_flange_bot_z"] + 0.35, p["gasket_z0"] + 0.6):
            assert not solid_at("asm_speaker", (x, y, p["spk_flange_bot_z"] + 0.35))
            assert not solid_at("asm_speaker_gasket", (x, y, p["gasket_z0"] + 0.6))
    screws = _solid("asm_speaker_screws")
    assert float((screws ^ _solid("asm_speaker")).volume()) < TOL
    assert float((screws ^ _solid("asm_speaker_gasket")).volume()) < TOL
    assert float((screws ^ _solid("asm_top")).volume()) < TOL


def test_no_dimples_over_insert_holes_or_next_to_the_plug_pockets():
    p = params()
    centres = [_xy(p["spk_hole_r"], a) for a in p["spk_hole_angles"]]
    z = p["H"] - 0.5
    for x, y in centres:
        for dx in np.linspace(-2.8, 2.8, 7):
            for dy in np.linspace(-2.8, 2.8, 7):
                if np.hypot(dx, dy) <= 2.8:
                    assert solid_at("asm_top", (x + dx, y + dy, z), 0.05)
    assert p["top_plug_keep"] == pytest.approx(p["top_plug_d"] / 2 + p["dimple_d"] / 2 + p["top_plug_rim"])
    for a in p["top_screw_angles"]:
        x, y = _xy(p["top_screw_r"], a)
        ring = _annulus(p["top_plug_d"] / 2 + 0.05, p["top_plug_d"] / 2 + p["top_plug_rim"]).translate([x, y])
        for zz in (p["H"] - 0.05, p["H"] - 0.5, p["H"] - p["dimple_depth"] + 0.05):
            sl = _solid("asm_top").slice(zz)
            assert float((sl ^ ring).area()) == pytest.approx(float(ring.area()), rel=0.01), (a, zz)


def test_button_hole():
    p = params()
    bx, by = _button_xy()
    z = p["top_underside_z"] + 1.0
    sl = _solid("asm_top").slice(z)
    rim = p["btn_relief_y0"] - 0.05
    assert rim - p["button_hole_d"] / 2 >= 0.75
    assert p["button_r"] + rim < p["ring_outer_r"] - p["top_edge_r"] + np.sqrt(p["top_edge_r"] ** 2 - (z - p["top_edge_z0"]) ** 2)
    hole = float((sl ^ manifold3d.CrossSection.circle(rim, 128).translate([bx, by])).area())
    ring = np.pi * (rim ** 2 - (p["button_hole_d"] / 2) ** 2)
    assert hole == pytest.approx(ring, rel=5e-3)
    for zz in (p["top_underside_z"] - 0.2, p["top_underside_z"] + 0.1):
        assert not solid_at("asm_top", (bx, by, zz))
    for ang in (0, 90, 180, 270):
        qx = bx + (p["button_hole_d"] / 2 + 0.3) * np.cos(np.radians(ang))
        qy = by + (p["button_hole_d"] / 2 + 0.3) * np.sin(np.radians(ang))
        assert solid_at("asm_top", (qx, qy, p["H"] - 0.5))


def test_button_hole_underside_edge_is_sharp():
    p = params()
    bx, by = _button_xy()
    r_hole = p["button_hole_d"] / 2
    z = p["top_underside_z"]
    for dz in (0.05, 0.5, 1.5):
        loops = [np.asarray(q) for q in _solid("asm_top").slice(z + dz).to_polygons()]
        near = [q for q in loops if np.linalg.norm(q.mean(axis=0) - [bx, by]) < 1.0]
        assert len(near) == 1
        radii = np.linalg.norm(near[0] - [bx, by], axis=1)
        assert radii.min() > r_hole - 0.05 and radii.max() < r_hole + 0.05


def test_cap_zone_is_clear():
    p = params()
    bx, by = _button_xy()
    cap_low = float(mesh("asm_button_cap").bounds[0][2])
    z0 = cap_low - 0.5
    zone = manifold3d.Manifold.cylinder(p["top_underside_z"] - z0, 5.2, 5.2, 96).translate([bx, by, z0])
    assert float((zone ^ _solid("asm_top")).volume()) < TOL


def test_button_holder_keeps_the_switch():
    p = params()
    top = _solid("asm_top")
    btn = _solid("asm_button")
    ang = np.radians(p["button_az"])
    rad = np.array([np.cos(ang), np.sin(ang), 0.0])
    tan = np.array([-np.sin(ang), np.cos(ang), 0.0])
    assert float((btn ^ top).volume()) < TOL
    assert float((btn.translate([0, 0, 0.3]) ^ top).volume()) > 0.1
    assert float((btn.translate([0, 0, -0.3]) ^ top).volume()) > 0.1
    for s in (-1, 1):
        assert float((btn.translate(list(s * 0.3 * tan)) ^ top).volume()) > 0.05
        assert float((btn.translate(list(s * 0.3 * rad)) ^ top).volume()) > 0.05
    assert mesh("asm_button").bounds[1][2] == pytest.approx(p["btn_tip_z"], abs=1e-6)


def _button_local(p, lx, ly):
    a = np.radians(p["button_az"])
    bx, by = _button_xy()
    return (
        bx + lx * np.cos(a) - ly * np.sin(a),
        by + lx * np.sin(a) + ly * np.cos(a),
    )


def test_button_holder_ceiling_gap():
    p = params()
    qx, qy = _button_local(p, -5.8, 3.5)
    top_of_body = p["btn_z0"] + p["btn_body_h"]
    gap = None
    for z in np.arange(top_of_body, p["top_underside_z"], 0.05):
        if solid_at("asm_top", (qx, qy, z)):
            gap = z - top_of_body
            break
    assert gap is not None
    assert 0.1 <= gap <= 0.4


def test_button_snap_hooks_sit_under_the_switch():
    p = params()
    y = p["btn_w"] / 2 - p["btn_hook_ov"] + 0.15
    for sy in (-1, 1):
        qx, qy = _button_local(p, 0.0, sy * y)
        assert solid_at("asm_top", (qx, qy, p["btn_z0"] - 0.1))
        assert not solid_at("asm_top", (qx, qy, p["btn_z0"] + 0.1))
        assert not solid_at("asm_top", (qx, qy, p["btn_hook_z0"] + 0.1))
        qx, qy = _button_local(p, 0.0, sy * (p["btn_w"] / 2 - p["btn_hook_ov"] - 0.3))
        assert not solid_at("asm_top", (qx, qy, p["btn_z0"] - 0.1))
        qx, qy = _button_local(p, p["btn_tab_w"] / 2 + 0.3, sy * (p["btn_in"] + 0.5))
        assert not solid_at("asm_top", (qx, qy, p["btn_z0"] + 1.0))
        qx, qy = _button_local(p, p["btn_tab_w"] / 2 + p["btn_tab_slit"] + 0.5, sy * (p["btn_in"] + 0.5))
        assert solid_at("asm_top", (qx, qy, p["btn_z0"] + 1.0))
    assert p["btn_tab_strain"] <= 0.016


def test_button_hooks_grip_0_6_under_the_switch_on_a_flat_catch_face():
    p = params()
    assert 0.6 <= p["btn_hook_ov"] <= 0.8
    tip = p["btn_w"] / 2 - p["btn_hook_ov"]
    assert p["btn_hook_u"] == pytest.approx(p["btn_in"] - tip, abs=1e-9)
    z0 = p["btn_z0"]
    for sy in (-1, 1):
        for ly in np.linspace(tip + 0.05, p["btn_in"] - 0.05, 6):
            for lx in (-p["btn_tab_w"] / 2 + 0.2, 0.0, p["btn_tab_w"] / 2 - 0.2):
                qx, qy = _button_local(p, lx, sy * ly)
                assert solid_at("asm_top", (qx, qy, z0 - 0.05), 0.03), (ly, lx)
                assert not solid_at("asm_top", (qx, qy, z0 + 0.05), 0.03), (ly, lx)
        qx, qy = _button_local(p, 0.0, sy * (tip - 0.05))
        assert not solid_at("asm_top", (qx, qy, z0 - 0.05), 0.03)
    btn = _solid("asm_button")
    top = _solid("asm_top")
    ang = np.radians(p["button_az"])
    tan = np.array([-np.sin(ang), np.cos(ang), 0.0])
    for s_ in (-1, 1):
        moved = btn.translate(list(s_ * (p["btn_hook_ov"] - 0.05) * tan + np.array([0, 0, -0.2])))
        assert float((moved ^ top).volume()) > 0.05
    assert p["btn_w"] / 2 - p["btn_hook_ov"] >= p["btn_flange_d"] / 2 + 0.15


def test_button_tab_roots_sit_in_reliefs_and_are_rounded():
    p = params()
    zu = p["top_underside_z"]
    zt = p["btn_root_z"]
    assert zt == pytest.approx(zu + p["btn_tab_relief"], abs=1e-9)
    assert p["top_plate_t"] - p["btn_tab_relief"] >= 1.0
    ri, ro = p["btn_root_in"], p["btn_root_out"]
    for sy in (-1, 1):
        qx, qy = _button_local(p, 0.0, sy * (p["btn_in"] - 0.2))
        assert solid_at("asm_top", (qx, qy, zt - 0.1), 0.04)
        qx, qy = _button_local(p, 0.0, sy * (p["btn_in"] - ri + 0.05))
        assert not solid_at("asm_top", (qx, qy, zt - ri + 0.1), 0.04)
        qx, qy = _button_local(p, 0.0, sy * (p["btn_out"] + 0.3))
        assert solid_at("asm_top", (qx, qy, zt - 0.1), 0.04)
        assert not solid_at("asm_top", (qx, qy, zt - ro + 0.05), 0.04)
        qx, qy = _button_local(p, 0.0, sy * (p["btn_out"] + ro + 0.1))
        assert not solid_at("asm_top", (qx, qy, zt - 0.3), 0.04)
        assert solid_at("asm_top", (qx, qy, zt + 0.1), 0.04)
        for ly in (p["btn_relief_y0"] + 0.1, p["btn_relief_y1"] - 0.1):
            for lx in (-p["btn_relief_x"] + 0.1, p["btn_relief_x"] - 0.1):
                qx, qy = _button_local(p, lx, sy * ly)
                assert not solid_at("asm_top", (qx, qy, zu + 0.5), 0.04), (lx, ly)
                assert solid_at("asm_top", (qx, qy, zt + 0.1), 0.04), (lx, ly)
        for ly in (p["btn_relief_y0"] - 0.1, p["btn_relief_y1"] + 0.1):
            qx, qy = _button_local(p, 0.0, sy * ly)
            assert solid_at("asm_top", (qx, qy, zu + 0.5), 0.04), ly
        qx, qy = _button_local(p, p["btn_relief_x"] + 0.1, sy * (p["btn_in"] + 0.5))
        assert solid_at("asm_top", (qx, qy, zu + 0.5), 0.04)
        edge = p["btn_tab_w"] / 2
        yc = sy * (p["btn_in"] + p["btn_wall"] / 2)
        for sx in (-1, 1):
            qx, qy = _button_local(p, sx * (edge - 0.05), yc)
            assert solid_at("asm_top", (qx, qy, zt - 1.0), 0.02)
            qx, qy = _button_local(p, sx * (edge + p["btn_tab_slit"] / 2), yc)
            for z in (zu - 1.0, zu - 0.05, zu + 0.5, zt - 0.1):
                assert not solid_at("asm_top", (qx, qy, z), 0.02), z
    face = _solid("asm_top").slice(p["H"] - 0.02)
    grown = _relief_rects().offset(p["btn_rim_min"] - 0.05, manifold3d.JoinType.Round, 2.0, 32)
    bx, by = _button_xy()
    grown = grown - manifold3d.CrossSection.circle(p["button_hole_d"] / 2 + 0.05, 96).translate([bx, by])
    grown = grown ^ _disc(p["top_face_r"] - 0.1)
    assert float((face ^ grown).area()) == pytest.approx(float(grown.area()), rel=1e-3)


def _path_overlap(shape, other, dzs):
    return max(float((shape.translate([0, 0, float(dz)]) ^ other).volume()) for dz in dzs)


def test_cap_goes_in_from_below():
    p = params()
    cap = _solid("asm_button_cap")
    top = _solid("asm_top")
    start = mesh("asm_top").bounds[0][2] - mesh("asm_button_cap").bounds[1][2] - 0.25
    dzs = np.arange(start, 0.001, 0.25)
    assert mesh("asm_button_cap").bounds[1][2] + dzs[0] < mesh("asm_top").bounds[0][2]
    assert _path_overlap(cap, top, dzs) < TOL
    assert _path_overlap(cap, top, np.arange(0.0, 0.6, 0.1)) < TOL
    from_above = _path_overlap(cap, top, np.arange(12.0, 0.001, -0.25))
    assert from_above > 1.0
    flange = np.linalg.norm(mesh("asm_button_cap").vertices[:, :2] - np.array(_button_xy()), axis=1).max()
    assert flange == pytest.approx(p["btn_flange_d"] / 2, abs=0.02)


def test_switch_snaps_in_from_below():
    p = params()
    btn = _solid("asm_button")
    cap = _solid("asm_button_cap")
    top = _solid("asm_top")
    flexed = _solid("asm_top_flexed")
    start = mesh("asm_top").bounds[0][2] - mesh("asm_button").bounds[1][2] - 0.25
    dzs = np.arange(start, 0.001, 0.25)
    assert mesh("asm_button").bounds[1][2] + dzs[0] < mesh("asm_top").bounds[0][2]
    assert _path_overlap(btn, flexed, dzs) < TOL
    assert _path_overlap(btn, cap, dzs) < TOL
    assert _path_overlap(btn, flexed, [0.19]) < TOL
    lifted = cap.translate([0, 0, 0.19])
    assert float((btn.translate([0, 0, 0.19]) ^ lifted).volume()) < TOL
    assert float((lifted ^ top).volume()) < TOL
    assert _path_overlap(btn, top, dzs) > 0.05


def test_switch_cannot_drop_out_without_flexing():
    p = params()
    btn = _solid("asm_button")
    top = _solid("asm_top")
    flexed = _solid("asm_top_flexed")
    for dz in (-0.3, -0.6, -1.0):
        assert float((btn.translate([0, 0, dz]) ^ top).volume()) > 0.1
        assert float((btn.translate([0, 0, dz]) ^ flexed).volume()) < TOL
    assert mesh("asm_top_flexed").bounds[0][2] <= mesh("asm_top").bounds[0][2] + 0.3
    assert p["btn_tab_flex"] >= p["btn_hook_ov"] + 0.1


def test_top_overhangs():
    p = params()
    m = mesh("top")
    steep = m.face_normals[:, 2] < -np.cos(np.radians(45.0))
    on_bed = np.all(m.triangles[:, :, 2] <= m.bounds[0][2] + 1e-6, axis=1)
    in_dimples = np.all(m.triangles[:, :, 2] <= p["dimple_depth"] + 1e-3, axis=1)
    cx = m.triangles[:, :, :2].mean(axis=1)
    near_plug = np.zeros(len(m.faces), dtype=bool)
    for a in p["top_screw_angles"]:
        x, y = _xy(p["top_screw_r"], a)
        near_plug |= np.hypot(cx[:, 0] - x, -cx[:, 1] - y) < p["top_plug_d"] / 2 + 0.05
    in_dimples &= ~near_plug
    cones = float(m.area_faces[steep & in_dimples & ~on_bed].sum())
    lateral = np.pi * p["dimple_d"] / 2 * np.hypot(p["dimple_d"] / 2, p["dimple_depth"])
    assert cones <= p["dimple_count"] * lateral * 1.01
    assert np.degrees(np.arctan2(p["dimple_depth"], p["dimple_d"] / 2)) >= 30.0
    plug = float(m.area_faces[steep & near_plug & ~on_bed].sum())
    shelf = np.pi * ((p["top_plug_d"] / 2) ** 2 - (p["csk_d"] / 2) ** 2)
    csk = np.pi * (p["csk_d"] + p["screw_thru_d"]) / 2 * np.sqrt(2) * (p["csk_d"] - p["screw_thru_d"]) / 2
    assert plug <= len(p["top_screw_angles"]) * (shelf + csk) * 1.02
    assert p["top_plug_d"] / 2 - p["csk_d"] / 2 <= 1.0
    assert overhang_area(m) - cones - plug < TOP_OVERHANG_MAX


def test_top_edge_is_rounded():
    p = params()
    prev = None
    for z in np.linspace(p["top_edge_z0"] + 0.2, p["H"] - 0.05, 12):
        loops = _solid("asm_top").slice(z).to_polygons()
        r = max(np.linalg.norm(np.asarray(q), axis=1).max() for q in loops)
        if prev is not None:
            assert r <= prev + 1e-6
        prev = r
    assert prev < p["ring_outer_r"] - 2.0
    assert p["top_face_r"] > p["dimple_r"] + p["grille_hole_d"] / 2 + 1.0


def _strip_extent(part, z, horizontal, at):
    p = params()
    size = [300.0, 0.02] if horizontal else [0.02, 300.0]
    centre = [0.0, at] if horizontal else [at, 0.0]
    strip = manifold3d.CrossSection.square(size, True).translate(centre)
    box = _spk_solid(part).slice(z) ^ strip
    x0, y0, x1, y1 = box.bounds()
    return (x0, x1) if horizontal else (y0, y1)


def test_flange_outline_matches_measurements():
    p = params()
    assert p["spk_flange_mid_w"] == pytest.approx(77.9)
    assert p["spk_flange_corner_w"] == pytest.approx(66.8)
    assert p["spk_flange_diag"] == pytest.approx(92.7)
    part = "asm_speaker_gasket"
    zc = (p["gasket_z0"] + p["gasket_z1"]) / 2
    for horizontal in (True, False):
        lo, hi = _strip_extent(part, zc, horizontal, 0.0)
        assert hi == pytest.approx(38.95, abs=0.05)
        assert lo == pytest.approx(-38.95, abs=0.05)
    rot = np.radians(-p["spk_rot"])
    v = mesh(part).vertices[:, :2] @ np.array([[np.cos(rot), np.sin(rot)], [-np.sin(rot), np.cos(rot)]])
    r = np.linalg.norm(v, axis=1)
    assert r.max() == pytest.approx(46.35, abs=0.05)
    assert r.max() <= 46.35 + 0.01
    far = v[r > 46.3]
    assert len(far) >= 4
    for x, y in far:
        assert abs(abs(x) - abs(y)) < 1.0
    assert {(np.sign(x), np.sign(y)) for x, y in far} == {(1, 1), (1, -1), (-1, 1), (-1, -1)}
    y_end = p["spk_flange_arc_y"]
    assert 28.0 < y_end < 33.4
    for sy in (-1, 1):
        lo, hi = _strip_extent(part, zc, True, sy * y_end)
        assert hi - lo == pytest.approx(66.8, abs=0.3)
    for sx in (-1, 1):
        lo, hi = _strip_extent(part, zc, False, sx * y_end)
        assert hi - lo == pytest.approx(66.8, abs=0.3)
    lo, hi = _strip_extent(part, zc, True, 20.0)
    assert 36.5 < hi < 38.95
    assert (hi - lo) < 77.9 - 2.0


def test_flange_fillet_and_side_arcs_are_solved_from_the_three_measurements():
    p = params()
    big = p["spk_side_arc_r"]
    rho = p["spk_flange_fillet_r"]
    assert 90.0 < big < 100.0
    assert 0.0 <= rho < 4.0
    assert p["spk_flange_max_r"] == pytest.approx(46.35, abs=1e-9)
    assert p["spk_flange_fillet_c"] * np.sqrt(2) + rho == pytest.approx(46.35, abs=1e-4)
    cx = p["spk_flange_half"] - big
    assert p["spk_side_arc_cx"] == pytest.approx(cx, abs=1e-4)
    tangent = np.hypot(p["spk_flange_fillet_c"] - cx, p["spk_flange_fillet_c"])
    assert tangent == pytest.approx(big - rho, abs=1e-3)
    assert cx + np.sqrt(big ** 2 - p["spk_flange_arc_y"] ** 2) == pytest.approx(33.4, abs=1e-3)
    assert p["spk_seat_max_r"] == pytest.approx(46.35 + 0.8)


def test_seat_clears_flange_by_0_8():
    p = params()
    assert p["spk_seat_flange_clr"] == pytest.approx(0.8)
    z = p["gasket_z1"] + 0.3
    seat = _seat_slice(z)
    inserts = None
    for a in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], a)
        d = manifold3d.CrossSection.circle(p["insert_d"] / 2, 96).translate([x, y])
        inserts = d if inserts is None else inserts + d
    inner = _disc(p["gasket_inner_r"])
    for margin in (0.5, 0.75):
        need = _grow(_outline(), margin) - inner - inserts
        assert float((need - seat).area()) < 1e-3, margin
    assert float((_grow(_outline(), 0.75) ^ inner).area()) == pytest.approx(float(inner.area()), rel=1e-9)
    assert float((seat - _grow(_outline(), 0.85)).area()) < 1e-3
    assert float((seat - _seat_outline()).area()) < 0.1
    tip = p["spk_seat_max_r"] / np.sqrt(2)
    assert solid_at_spk("asm_top", (tip - 0.05, tip - 0.05, z))
    assert not solid_at_spk("asm_top", (tip + 0.05, tip + 0.05, z))
    for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge = p["spk_flange_half"] + p["spk_seat_flange_clr"]
        assert solid_at_spk("asm_top", (sx * (edge - 0.05), sy * (edge - 0.05), z))
        assert not solid_at_spk("asm_top", (sx * (edge + 0.05), sy * (edge + 0.05), z))
    gasket = _solid("asm_speaker_gasket").slice(p["gasket_z0"] + 0.1)
    assert float((gasket - _outline()).area()) < 0.1
    assert float((_grow(_outline(), 0.75) - _seat_outline()).area()) < 0.1


def test_gasket_watertight_fits_bed():
    p = params()
    m = mesh("speaker_gasket")
    assert m.is_watertight
    assert m.volume > 0
    assert m.extents[0] < p["bed"] and m.extents[1] < p["bed"]
    assert abs(m.bounds[0][2]) < 1e-6


def test_gasket_shape():
    p = params()
    a = mesh("asm_speaker_gasket")
    assert a.extents[2] == pytest.approx(p["gasket_t"], abs=1e-4)
    assert a.bounds[0][2] == pytest.approx(p["gasket_z0"], abs=1e-4)
    assert a.bounds[1][2] == pytest.approx(p["gasket_z1"], abs=1e-4)
    half = p["spk_flange_half"]
    lb = _spk_bounds("asm_speaker_gasket")
    assert lb[1][0] == pytest.approx(half, abs=0.01)
    assert lb[1][1] == pytest.approx(half, abs=0.01)
    assert lb[0][0] == pytest.approx(-half, abs=0.01)
    r = np.linalg.norm(a.vertices[:, :2], axis=1)
    assert r.min() == pytest.approx(p["gasket_inner_r"], abs=0.02)
    zc = (p["gasket_z0"] + p["gasket_z1"]) / 2
    s = _solid("asm_speaker_gasket").slice(zc)
    holes = len(p["spk_hole_angles"]) * np.pi * (p["gasket_hole_d"] / 2) ** 2
    assert float(s.area()) == pytest.approx(float(_gasket_band().area()) - holes, rel=0.005)
    assert float((s ^ _outline()).area()) == pytest.approx(float(s.area()), rel=1e-4)
    assert float((s ^ _disc(p["gasket_inner_r"] - 0.01)).area()) == 0.0
    for ang in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], ang)
        assert not solid_at("asm_speaker_gasket", (x, y, zc))
        for dx in (-2.2, 2.2):
            assert solid_at("asm_speaker_gasket", (x + dx, y, zc))
        assert solid_at("asm_speaker_gasket", (x, y + 2.2, zc))
    b = mesh("speaker_gasket")
    assert b.volume == pytest.approx(a.volume, rel=1e-5)


def test_gasket_follows_the_flange_outline_with_the_wanted_side_band():
    p = params()
    assert p["spk_seat_side_w"] == pytest.approx(1.2, abs=1e-6)
    assert p["spk_seat_side_w"] == pytest.approx(p["spk_flange_mid_w"] / 2 - p["gasket_inner_r"], abs=1e-9)
    assert p["gasket_inner_r"] == pytest.approx(p["spk_surround_d"] / 2 + 0.5)
    assert p["gasket_inner_r"] >= 37.75 - 1e-9
    zc = (p["gasket_z0"] + p["gasket_z1"]) / 2
    for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        r_in = p["gasket_inner_r"]
        half = p["spk_flange_half"]
        assert solid_at_spk("asm_speaker_gasket", (sx * (r_in + 0.6) + sy * 0.0, sy * (r_in + 0.6), zc), 0.05)
        assert not solid_at_spk("asm_speaker_gasket", (sx * (half + 0.2), sy * (half + 0.2), zc), 0.05)
    gasket = _spk_solid("asm_speaker_gasket")
    corner = manifold3d.CrossSection.square([30, 30], True).translate([38, 38])
    wide = float((gasket.slice(zc) ^ corner).area())
    hole = np.pi * (p["gasket_hole_d"] / 2) ** 2
    assert wide == pytest.approx(float(((_spk_outline() - _disc(p["gasket_inner_r"])) ^ corner).area()) - hole, rel=0.005)
    assert wide > 90.0
    assert p["spk_flange_max_r"] - p["gasket_inner_r"] >= 8.0


def test_gasket_clear():
    assert overlap("asm_speaker_gasket", "asm_speaker") < TOL
    assert overlap("asm_speaker_gasket", "asm_speaker_screws") < TOL
    assert overlap("asm_speaker_gasket", "asm_top") < TOL


def test_gasket_sits_between_flange_and_top():
    gasket = _solid("asm_speaker_gasket")
    top = _solid("asm_top")
    speaker = _solid("asm_speaker")
    up = gasket.translate([0, 0, 0.1])
    down = gasket.translate([0, 0, -0.1])
    assert float((up ^ top).volume()) > 0.5
    assert float((down ^ speaker).volume()) > 0.5


def test_rim_presses_on_the_gasket():
    p = params()
    z = p["gasket_z1"] + 0.3
    sl = _seat_slice(z)
    band = _seat_band()
    holes = len(p["spk_hole_angles"]) * np.pi * (p["insert_d"] / 2) ** 2
    assert float(sl.area()) == pytest.approx(float(band.area()) - holes, rel=0.002)
    assert float((sl ^ band).area()) == pytest.approx(float(sl.area()), rel=1e-3)
    assert float((sl ^ _disc(p["gasket_inner_r"] - 0.01)).area()) == 0.0


def test_seat_never_touches_the_moving_surround():
    p = params()
    assert overlap("asm_top", "asm_speaker") < TOL
    r_surround = p["spk_surround_d"] / 2
    for z in (p["spk_flange_top_z"] + 1.0, p["spk_surround_top_z"] - 0.1, p["gasket_z1"] + 0.5):
        top = _solid("asm_top").slice(z)
        near = float((top ^ _disc(r_surround + 0.45)).area())
        assert near == 0.0, z
    assert p["gasket_inner_r"] - r_surround >= 0.5 - 1e-9
    assert p["spk_hole_r"] - p["boss_od"] / 2 > p["gasket_inner_r"]


@pytest.mark.parametrize("shift", [(0.5, 0.0), (-0.5, 0.0), (0.0, 0.5), (0.0, -0.5)])
def test_seat_still_clears_the_surround_with_the_speaker_off_centre_by_the_instructed_tolerance(shift):
    p = params()
    spk = _solid("asm_speaker").translate([shift[0], shift[1], 0])
    assert float((spk ^ _solid("asm_top")).volume()) < 0.05
    assert float((spk ^ _solid("asm_speaker_gasket")).volume()) < 0.05
    assert p["gasket_inner_r"] - p["spk_surround_d"] / 2 - 0.5 >= -1e-9
    zs = p["gasket_z1"] + 1.0
    surround = manifold3d.CrossSection.circle(p["spk_surround_d"] / 2, 360).translate(list(shift))
    seat_slice = _solid("asm_top").slice(zs)
    assert float((seat_slice ^ surround).area()) < 0.05


@pytest.mark.parametrize("shift", [(0.8, 0.0), (0.0, -0.8)])
def test_a_bigger_off_centre_shift_would_touch_the_seat(shift):
    spk = _solid("asm_speaker").translate([shift[0], shift[1], 0])
    assert float((spk ^ _solid("asm_top")).volume()) > 5.0


def test_grille_through_holes_stay_inside_the_inner_circle():
    p = params()
    assert p["grille_r"] + p["grille_hole_d"] / 2 < p["gasket_inner_r"]
    z = p["top_underside_z"] + 1.0
    slice_ = _solid("asm_top").slice(z)
    outside = _annulus(p["gasket_inner_r"], p["dimple_r"] - 1.0)
    bx, by = _button_xy()
    hole = manifold3d.CrossSection.circle(p["button_hole_d"] / 2, 128).translate([bx, by])
    seat_d = p["screw_thru_d"] + 2 * max(0.0, z - (p["top_seat_z"] - p["csk_h"]))
    for a in p["top_screw_angles"]:
        hole = hole + manifold3d.CrossSection.circle(seat_d / 2, 128).translate(list(_xy(p["top_screw_r"], a)))
    outside = outside - _relief_rects()
    assert float((slice_ ^ outside).area()) == pytest.approx(float((outside - hole).area()), rel=1e-3)


def test_gasket_overhangs():
    assert overhang_area(mesh("speaker_gasket")) < GASKET_OVERHANG_MAX


def _mic_dirs(p):
    ang = np.radians(p["mic_az"])
    rad = np.array([np.cos(ang), np.sin(ang), 0.0])
    tan = np.array([-np.sin(ang), np.cos(ang), 0.0])
    return rad, tan


def test_mic_boot_watertight_fits_bed():
    p = params()
    m = mesh("mic_boot")
    assert m.is_watertight
    assert m.volume > 0
    assert m.extents[0] < p["bed"] and m.extents[1] < p["bed"]
    assert abs(m.bounds[0][2]) < 0.01
    assert m.volume == pytest.approx(mesh("asm_mic_boot").volume, rel=1e-5)


@pytest.mark.parametrize("name", MIC_BOOT_CLEAR_OF)
def test_mic_boot_clear(name):
    assert overlap("asm_mic_boot", f"asm_{name}") < TOL


def test_mic_boot():
    p = params()
    assert overlap("asm_mic_boot", "asm_mic") < TOL
    squeeze = overlap("asm_mic_boot", "asm_body")
    assert 1.0 < squeeze < 15.0
    assert p["mb_d"] - p["mic_seat_d"] == pytest.approx(-2 * p["mb_clr"], abs=1e-9)
    assert p["mb_clr"] < 0


def test_mic_boot_wire_opening():
    p = params()
    rad, _ = _mic_dirs(p)
    m = mesh("asm_mic_boot")
    centre = rad * (p["mic_face_r"] - p["mic_t"]) + np.array([0.0, 0.0, p["mic_z"]])
    assert first_hit(m, centre, (0.0, 1.0, 0.0)) is None
    assert first_hit(m, centre, -rad) is None


def test_mic_boot_back_edge_has_a_lead_in():
    p = params()
    rad, tan = _mic_dirs(p)
    back = p["inner_r_profile"][int(p["mic_z"])] + p["mic_seat_depth"] - p["mb_back_gap"]
    y = p["mb_d"] / 2 - 0.04

    def at(depth):
        x = np.sqrt((back - depth) ** 2 - y ** 2)
        return tuple(rad * x + tan * y + np.array([0.0, 0.0, p["mic_z"]]))

    assert not solid_at("asm_mic_boot", at(0.05), 0.03)
    assert solid_at("asm_mic_boot", at(p["mb_lead"] + 0.25), 0.03)
    assert p["mb_lead"] >= -2 * p["mb_clr"]


def test_mic_boot_keeps_the_sound_path_open():
    p = params()
    assert overlap("asm_mic_boot", "asm_mic_port_probe") < TOL
    rad, tan = _mic_dirs(p)
    m = mesh("asm_mic_boot")
    face = p["mic_face_r"]
    z = p["mic_z"]
    assert first_hit(m, rad * (face - 0.2) + [0, 0, z], rad) is None
    assert solid_at("asm_mic_boot", tuple(rad * (face + 0.3) + tan * 2.2 + [0, 0, z]))
    assert solid_at("asm_mic_boot", tuple(rad * (face + 0.3) - tan * 2.2 + [0, 0, z]))
    assert solid_at("asm_mic_boot", tuple(rad * (face + 0.3) + [0, 0, z + 2.2]))
    assert solid_at("asm_mic_boot", tuple(rad * (face + 0.3) + [0, 0, z - 2.2]))


def test_mic_boot_sits_in_the_seat():
    p = params()
    rad, tan = _mic_dirs(p)
    boot = _solid("asm_mic_boot")
    body = _solid("asm_body")
    seated = float((boot ^ body).volume())
    assert float((boot.translate(list(0.3 * rad)) ^ body).volume()) > seated
    inward = float((boot.translate(list(-0.3 * rad)) ^ body).volume())
    assert 0.3 * seated < inward < seated
    assert float((boot.translate(list(0.6 * tan)) ^ body).volume()) > seated


def test_mic_boot_presses_the_board_to_the_port():
    p = params()
    rad, tan = _mic_dirs(p)
    boot = _solid("asm_mic_boot")
    mic = _solid("asm_mic")
    assert float((mic.translate(list(0.15 * rad)) ^ boot).volume()) > 1.0
    assert float((mic.translate(list(-0.15 * rad)) ^ boot).volume()) > 0.1
    for s in (-1, 1):
        assert float((mic.translate(list(s * 0.4 * tan)) ^ boot).volume()) > 0.05
    for dz in (-0.4, 0.4):
        assert float((mic.translate([0, 0, dz]) ^ boot).volume()) > 0.05


def _floor_thickness(p, tan_off, z_off):
    rad, tan = _mic_dirs(p)
    base = rad * (p["mic_face_r"] - 0.1) + tan * tan_off + np.array([0.0, 0.0, p["mic_z"] + z_off])
    n = 0
    for k in range(120):
        if solid_at("asm_mic_boot", tuple(base + rad * 0.05 * k), 0.03):
            n += 1
    return n * 0.05


def test_mic_boot_floor_is_thick_enough():
    p = params()
    spots = [(2.2, 0), (-2.2, 0), (0, 2.2), (0, -2.2), (0, 4.5), (0, -4.5)]
    for sy in (-1, 1):
        for k in (-1, 0, 1):
            spots.append((sy * p["mic_pin_row"], k * p["mic_pin_pitch"]))
        spots += [(sy * (p["mic_pin_row"] + 0.6), -p["mic_pin_half"] - 0.6)]
    for tan_off, z_off in spots:
        assert _floor_thickness(p, tan_off, z_off) >= 0.5, (tan_off, z_off)
    assert p["mb_relief_floor_t"] >= p["mb_min_t"]
    low = mesh("asm_mic_boot").bounds[0][2]
    assert low >= p["mic_z"] - p["mb_d"] / 2 - 0.01
    assert p["mb_z_clip"] <= low


def test_mic_boot_overhangs():
    assert overhang_area(mesh("mic_boot")) < MIC_BOOT_OVERHANG_MAX


def build_parts():
    text = (CASE_DIR / "build.sh").read_text()
    line = next(l for l in text.splitlines() if l.startswith("PARTS="))
    return dict(e.split(":") for e in line.split("(")[1].rstrip(")").split())


def test_build_lists_the_new_parts():
    for name in ("top", "speaker_gasket", "top_plug"):
        assert name in build_parts()
    assert "mic_boot" not in build_parts()


def test_build_names_every_stl_with_its_plastic_and_count():
    tpu = {"foot", "speaker_gasket", "tray_gasket", "top_plug"}
    counts = {"ring_arc": 2, "foot": 4, "top_plug": 2}
    files = build_parts()
    assert len(files) == 9
    for part, name in files.items():
        plastic = "TPU_" if part in tpu else ("PETG-white_" if part == "ring_arc" else "PETG-black_")
        suffix = f"_x{counts[part]}" if part in counts else ""
        assert name == f"{plastic}{part}{suffix}"


def test_speaker_terminals_face_the_turned_flange_side_and_stay_clear():
    p = params()
    assert list(p["spk_term_az"]) == [p["spk_rot"]]
    for a in list(p["top_screw_angles"]) + [p["button_az"]]:
        assert abs((a - p["spk_rot"] + 180) % 360 - 180) >= 45
    r_out = p["spk_flange_half"]
    zt = p["spk_flange_bot_z"] - p["spk_term_h"] / 2
    mid = r_out + (p["spk_term_out"] - p["spk_term_in"]) / 2
    assert p["spk_term_h"] == pytest.approx(10.0)
    assert r_out + p["spk_term_out"] <= 39.0 + 1e-9
    assert solid_at_spk("asm_speaker", (mid, 0, zt))
    assert not solid_at_spk("asm_speaker", (-(r_out - 0.5), 0, zt))
    for sy in (-1, 1):
        assert not solid_at_spk("asm_speaker", (0, sy * (r_out - 0.5), zt))
    for name in (
        "body",
        "top",
        "top_screws",
        "top_plugs",
        "button",
        "button_cap",
        "ring",
        "speaker_gasket",
        "speaker_screws",
        "filament",
        "devkit",
        "amp",
        "cap",
        "metal_washers",
    ):
        assert overlap("asm_speaker", f"asm_{name}") < TOL, name


@pytest.mark.parametrize("turn", [90, 180, 270])
def test_speaker_turned_with_its_washers_also_clears_body_and_top(turn):
    spk = _solid("asm_speaker").rotate([0, 0, turn])
    washers = _solid("asm_metal_washers").rotate([0, 0, turn])
    gasket = _solid("asm_speaker_gasket")
    for solid in (spk, washers):
        hit = float((solid ^ _solid("asm_body")).volume()) + float((solid ^ _solid("asm_top")).volume())
        assert hit < TOL
    assert float((spk ^ washers).volume()) < TOL
    assert float((spk ^ gasket).volume()) < TOL


def test_speaker_dummy_matches_the_measured_speaker():
    p = params()
    m = mesh("asm_speaker")
    assert p["spk_flange_mid_w"] == pytest.approx(77.9)
    assert p["spk_flange_corner_w"] == pytest.approx(66.8)
    assert p["spk_flange_diag"] == pytest.approx(92.7)
    assert p["spk_flange_t"] == pytest.approx(0.7)
    assert p["spk_surround_d"] == pytest.approx(74.5)
    assert p["spk_basket_top_d"] == pytest.approx(74.8)
    assert p["spk_surround_h"] == pytest.approx(4.0)
    assert p["spk_depth"] == pytest.approx(36.0)
    assert p["spk_magnet_d"] == pytest.approx(60.0) and p["spk_magnet_h"] == pytest.approx(10.0)
    assert p["spk_hole_pitch"] == pytest.approx(59.0)
    assert p["spk_hole_r"] == pytest.approx(59.0 * np.sqrt(2) / 2, abs=1e-4)
    assert m.bounds[1][2] - p["spk_surround_top_z"] == pytest.approx(0.0, abs=1e-4)
    assert m.bounds[0][2] == pytest.approx(p["spk_flange_top_z"] - 36.0, abs=1e-4)
    assert p["spk_surround_top_z"] - m.bounds[0][2] == pytest.approx(40.0, abs=1e-4)
    zf = p["spk_flange_bot_z"] + p["spk_flange_t"] / 2
    s = _spk_solid("asm_speaker").slice(zf)
    holes = len(p["spk_hole_angles"]) * (np.pi * (p["spk_hole_d"] / 2) ** 2 + p["spk_hole_d"] * (p["spk_hole_l"] - p["spk_hole_d"]))
    lug = 0.0
    flange = float(_outline().area()) - holes
    assert float(s.area()) >= flange - 1.0
    for ang in p["spk_hole_angles"]:
        x, y = _xy(p["spk_hole_r"], ang)
        assert not solid_at("asm_speaker", (x, y, zf))
    half = p["spk_flange_half"]
    tip = p["spk_flange_max_r"] / np.sqrt(2)
    assert solid_at_spk("asm_speaker", (half - 0.3, 0.0, zf))
    assert not solid_at_spk("asm_speaker", (half + 0.3, 0.0, zf))
    assert not solid_at_spk("asm_speaker", (half - 0.3, half - 0.3, zf))
    assert solid_at_spk("asm_speaker", (tip - 0.3, tip - 0.3, zf))
    assert not solid_at_spk("asm_speaker", (tip + 0.3, tip + 0.3, zf))
    zs = p["spk_flange_top_z"] + p["spk_surround_h"] - 0.5
    assert solid_at("asm_speaker", (p["spk_surround_d"] / 2 - 0.5, 0.0, zs))
    assert not solid_at("asm_speaker", (p["spk_surround_d"] / 2 + 0.3, 0.0, zs))
    assert not solid_at("asm_speaker", (0.0, 0.0, zs))
    zk = p["spk_flange_bot_z"] - 0.1
    assert solid_at_spk("asm_speaker", (-(p["spk_basket_top_d"] / 2 - 0.2), 0.0, zk))
    assert not solid_at_spk("asm_speaker", (-(p["spk_basket_top_d"] / 2 + 0.2), 0.0, zk))


def _legs():
    p = params()
    below = manifold3d.Manifold.cube([200, 200, 50], True).translate([0, 0, p["btn_z0"] - 25])
    return _solid("asm_button") ^ below


def test_switch_legs_are_modelled_and_clear_of_the_hooks():
    p = params()
    legs = _legs()
    expect = 2 * p["btn_leg_w"] * p["btn_leg_t"] * (p["btn_leg_l"] + p["btn_stub_l"])
    assert float(legs.volume()) == pytest.approx(expect, rel=0.01)
    assert mesh("asm_button").bounds[0][2] == pytest.approx(p["btn_z0"] - p["btn_leg_l"], abs=1e-3)
    v = mesh("asm_button").vertices
    outer = v[np.hypot(v[:, 0], v[:, 1]) > p["ring_in_r"] - p["ring_in_clr"]]
    assert outer[:, 2].min() == pytest.approx(p["btn_z0"] - p["btn_stub_l"], abs=1e-3)
    assert outer[:, 2].min() - p["ring_z1"] >= p["btn_leg_gap"] - 1e-6
    top = _solid("asm_top")
    flexed = _solid("asm_top_flexed")
    assert float((legs ^ top).volume()) < TOL
    assert float((legs ^ flexed).volume()) < TOL
    assert p["btn_leg_y"] + p["btn_leg_w"] / 2 <= p["btn_w"] / 2 - p["btn_hook_ov"] - 2.0
    dzs = np.arange(-6.0, 0.001, 0.25)
    assert _path_overlap(legs, flexed, dzs) < TOL
    assert _path_overlap(legs, _solid("asm_button_cap"), dzs) < TOL
    assert overlap("asm_button", "asm_ring") < TOL


def test_switch_legs_on_the_hook_sides_would_collide():
    p = params()
    bx, by = _button_xy()
    turned = _legs().translate([-bx, -by, 0]).rotate([0, 0, 90]).translate([bx, by, 0])
    assert float((turned ^ _solid("asm_top")).volume()) > 0.005
    assert p["btn_leg_x"] + p["btn_leg_t"] / 2 > p["btn_w"] / 2 - p["btn_hook_ov"]


def test_cap_socket_accepts_a_round_or_a_square_stem():
    p = params()
    cap = _solid("asm_button_cap")
    assert p["btn_stem_square"] in (0.0, False)
    for name in ("asm_button_round", "asm_button_square"):
        stem = _solid(name)
        assert float((stem ^ cap).volume()) < TOL
        assert float((stem ^ _solid("asm_top")).volume()) < TOL
        assert mesh(name).bounds[1][2] == pytest.approx(p["btn_tip_z"], abs=1e-6)
        rad = np.radians(p["button_az"])
        for ang in (0, 90, 180, 270):
            d = 0.15 * np.array([np.cos(np.radians(ang) + rad), np.sin(np.radians(ang) + rad), 0.0])
            assert float((stem.translate(list(d)) ^ cap).volume()) < TOL, (name, ang)
    assert mesh("asm_button").volume == pytest.approx(mesh("asm_button_round").volume, rel=1e-6)
    assert mesh("asm_button_square").volume != pytest.approx(mesh("asm_button_round").volume, rel=1e-3)


def _mic_face_basis(p):
    rad, tan = _mic_dirs(p)
    return rad, tan, np.array([0.0, 0.0, 1.0])


def test_mic_pins_have_a_relief_and_the_port_ring_still_seals():
    p = params()
    rad, tan, up = _mic_face_basis(p)
    face = p["mic_face_r"]
    z0 = p["mic_z"]
    m = mesh("asm_mic")
    reach = (m.vertices @ rad).max()
    assert reach == pytest.approx(face + p["mic_pin_tail"], abs=1e-3)
    assert overlap("asm_mic", "asm_mic_boot") < TOL
    assert overlap("asm_mic", "asm_body") < TOL
    assert p["mic_pin_n"] == 3 and p["mic_pin_pitch"] == pytest.approx(2.54)
    assert 2 * p["mic_pin_row"] == pytest.approx(7.6)
    for sy in (-1, 1):
        for k in (-1, 0, 1):
            y = sy * p["mic_pin_row"]
            base = rad * face + tan * y + up * (z0 + k * p["mic_pin_pitch"])
            assert not solid_at("asm_mic_boot", tuple(base + rad * 0.5), 0.04)
            assert not solid_at("asm_mic_boot", tuple(base + rad * (p["mb_relief_depth"] - 0.05)), 0.04)
            assert solid_at("asm_mic_boot", tuple(base + rad * (p["mb_relief_depth"] + 0.08)), 0.04)
            thickness = 0.0
            for j in range(100):
                if solid_at("asm_mic_boot", tuple(base + rad * (p["mb_relief_depth"] + 0.02 + 0.05 * j)), 0.03):
                    thickness += 0.05
            assert thickness >= 0.5
    ch = p["mb_channel_d"] / 2
    ring_out = p["mic_pin_row"] - p["mic_pin_d"] / 2 - p["mb_relief_clr"]
    assert ring_out - ch >= 0.8
    for phi in np.radians(np.arange(0, 360, 5.0)):
        for r in (ch + 0.1, ch + 0.5, ch + 0.78):
            q = rad * (face + 0.05) + tan * (r * np.cos(phi)) + up * (z0 + r * np.sin(phi))
            assert solid_at("asm_mic_boot", tuple(q), 0.03), (phi, r)
            q = rad * (face - 0.05) + tan * (r * np.cos(phi)) + up * (z0 + r * np.sin(phi))
            assert not solid_at("asm_mic_boot", tuple(q), 0.03), (phi, r)


def test_mic_board_matches_the_measured_inmp441():
    p = params()
    rad, tan, up = _mic_face_basis(p)
    face = p["mic_face_r"]
    assert p["mic_d"] == pytest.approx(13.8)
    assert p["mic_t"] == pytest.approx(1.0)
    assert p["mic_total_t"] == pytest.approx(2.0)
    m = mesh("asm_mic")
    z0 = p["mic_z"]
    centre = rad * (face - 0.5) + up * z0
    assert solid_at("asm_mic", tuple(centre))
    can_mid = rad * (face - p["mic_t"] - p["mic_can_h"] / 2) + up * z0
    assert solid_at("asm_mic", tuple(can_mid))
    off_can = rad * (face - p["mic_t"] - p["mic_can_h"] / 2) + tan * (p["mic_can_w"] / 2 + 0.6) + up * z0
    assert not solid_at("asm_mic", tuple(off_can))
    far = rad * (face - 0.5) + tan * (p["mic_d"] / 2 + 0.3) + up * z0
    assert not solid_at("asm_mic", tuple(far))
    edge = rad * (face - 0.5) + tan * (p["mic_d"] / 2 - 0.1) + up * z0
    assert solid_at("asm_mic", tuple(edge))
    back = (m.vertices @ rad).min()
    assert face - back >= p["mic_t"] + p["mic_can_h"]
    assert p["mic_port_d"] / 2 < p["mic_can_w"] / 2


def test_top_rests_on_the_bridges_not_on_the_arcs_in_the_model():
    p = params()
    inter = _solid("asm_ring") ^ _solid("asm_top")
    assert float(inter.volume()) < 1e-3
    assert p["top_z0"] - mesh("asm_ring").bounds[1][2] == pytest.approx(p["ring_h"] - p["ring_arc_h"], abs=1e-4)
    assert mesh("asm_top").bounds[1][2] == pytest.approx(p["H"], abs=1e-3)


def test_switch_dummy_matches_the_measured_part():
    p = params()
    assert p["btn_w"] == pytest.approx(11.9)
    assert p["btn_body_h"] == pytest.approx(3.0)
    assert p["btn_h"] == pytest.approx(5.7)
    assert p["btn_pusher_d"] == pytest.approx(6.5)
    assert p["btn_stem_square"] in (0.0, False)
    m = mesh("asm_button_round")
    ang = np.radians(p["button_az"])
    rad = np.array([np.cos(ang), np.sin(ang), 0.0])
    tan = np.array([-np.sin(ang), np.cos(ang), 0.0])
    centre = np.array(_button_xy() + (0.0,))
    v = m.vertices - centre
    body = v[(v[:, 2] > p["btn_z0"] - 1e-6) & (v[:, 2] < p["btn_z0"] + p["btn_body_h"] + 1e-6)]
    assert np.abs(body @ rad).max() == pytest.approx(p["btn_w"] / 2, abs=1e-4)
    assert np.abs(body @ tan).max() == pytest.approx(p["btn_w"] / 2, abs=1e-4)
    stem = v[v[:, 2] > p["btn_z0"] + p["btn_body_h"] + 0.01]
    assert np.hypot(stem @ rad, stem @ tan).max() == pytest.approx(p["btn_pusher_d"] / 2, abs=0.02)
    assert m.bounds[1][2] - p["btn_z0"] == pytest.approx(p["btn_h"], abs=1e-4)
    assert m.bounds[1][2] == pytest.approx(p["btn_tip_z"], abs=1e-4)


def test_switch_holder_hooks_stay_inside_the_strain_limit_for_the_measured_switch():
    p = params()
    assert p["btn_tab_strain"] <= 0.016
    assert p["btn_tab_len"] == pytest.approx(p["top_underside_z"] + p["btn_tab_relief"] - p["btn_z0"])
    assert p["btn_tab_strain"] == pytest.approx(
        1.5 * p["btn_wall"] * p["btn_tab_flex"] / p["btn_tab_len"] ** 2, rel=1e-4
    )
    assert 0.6 <= p["btn_hook_ov"] <= 0.8
    assert p["btn_tab_flex"] >= p["btn_hook_ov"] + 0.1 - 1e-9
    assert p["btn_in"] == pytest.approx(p["btn_w"] / 2 + p["btn_pocket_clr"])
    assert p["btn_hook_u"] == pytest.approx(p["btn_in"] - (p["btn_w"] / 2 - p["btn_hook_ov"]))


def test_cap_still_travels_with_the_shorter_stem():
    p = params()
    cap_low = float(mesh("asm_button_cap").bounds[0][2])
    body_top = p["btn_z0"] + p["btn_body_h"]
    assert cap_low - body_top >= 0.5
    assert p["btn_tip_z"] - p["btn_tip_gap"] > p["btn_ceil_z"]
    stem_free = p["btn_h"] - p["btn_body_h"]
    assert stem_free == pytest.approx(2.7)
    assert p["btn_tip_z"] - cap_low <= stem_free


def _grow(shape, d):
    return shape.offset(d, manifold3d.JoinType.Round, 2.0, 64)


def test_switch_sits_against_the_seat_rim_and_its_legs_clear_the_flange():
    p = params()
    sw = _solid("asm_button").slice(p["btn_z0"] + 1.0)
    band = _seat_band()
    assert float((_grow(band, p["btn_pocket_clr"] - 0.01) ^ sw).area()) == 0.0
    assert float((_grow(band, p["btn_pocket_clr"] + 0.05) ^ sw).area()) > 0.0
    assert (p["button_az"] - p["spk_rot"]) % 90 == pytest.approx(0.0)
    legs = _legs()
    for z in np.linspace(p["btn_z0"] - p["btn_leg_l"] + 0.05, p["btn_z0"] - 0.05, 5):
        assert float((_grow(_outline(), 1.0) ^ legs.slice(float(z))).area()) == 0.0
    bx, by = _button_xy()
    reach = np.hypot(max(p["btn_out"], p["btn_skirt_x"]), p["btn_out"])
    for a in p["spk_hole_angles"]:
        corner = np.array(_xy(p["spk_hole_r"], a))
        assert np.hypot(bx - corner[0], by - corner[1]) - p["boss_od"] / 2 - reach > 1.0
    assert overlap("asm_speaker", "asm_button") < TOL
    assert overlap("asm_metal_washers", "asm_button") < TOL
    assert overlap("asm_speaker_gasket", "asm_button_cap") < TOL


@pytest.mark.parametrize("b", [20, 200])
def test_speaker_flange_and_washers_keep_clear_of_the_top_screw_columns(b):
    p = params()
    assert b in p["top_screw_angles"]
    assert (b - p["spk_rot"]) % 90 == pytest.approx(0.0)
    x, y = _xy(p["top_screw_r"], b)
    boss = manifold3d.CrossSection.circle(p["boss_od"] / 2, 96).translate([x, y])
    outline = _outline()
    assert float((_grow(outline, 2.5) ^ boss).area()) == 0.0
    assert float((_grow(_seat_outline(), 0.5) ^ boss).area()) == 0.0
    for name in ("asm_speaker", "asm_metal_washers", "asm_speaker_gasket", "asm_speaker_screws"):
        assert overlap(name, "asm_top_screws") < TOL
        assert overlap(name, "asm_body") < TOL
    assert p["top_screw_r"] - p["boss_od"] / 2 - p["spk_flange_half"] >= 2.5


def test_speaker_swap_leaves_antenna_mic_and_height_alone():
    p = params()
    assert p["antenna_metal_gap"] >= 10
    assert p["antenna_metal_gap"] == pytest.approx(mesh("asm_speaker").bounds[0][2] - p["antenna_top_z"], abs=1e-4)
    assert p["cone_gap"] == pytest.approx(4.5)
    for name in ("mic", "mic_boot", "mic_port_probe", "tray", "devkit", "amp"):
        assert overlap("asm_speaker", f"asm_{name}") < TOL, name
        assert overlap("asm_metal_washers", f"asm_{name}") < TOL, name
    assert mesh("asm_top").bounds[1][2] == pytest.approx(p["H"], abs=1e-4)
    assert mesh("asm_body").bounds[1][2] == pytest.approx(p["top_col_z1"], abs=1e-4)
    low = mesh("asm_feet").bounds[0][2]
    assert mesh("asm_top").bounds[1][2] - low == pytest.approx(p["H"] - p["tray_bot_z"] + p["foot_out"], abs=1e-3)


def test_button_front_right_near_edge():
    p = params()
    cap = mesh("asm_button_cap")
    c = (cap.bounds[0][:2] + cap.bounds[1][:2]) / 2
    assert np.degrees(np.arctan2(c[1], c[0])) == pytest.approx(-70.0, abs=5.0)
    r = float(np.hypot(*c))
    stem = (p["button_hole_d"] - 2 * p["clr"]) / 2
    assert r - p["button_hole_d"] / 2 > p["grille_r"] + p["grille_hole_d"] / 2
    assert r + p["button_hole_d"] / 2 < p["top_face_r"]
    v = cap.vertices
    assert np.hypot(v[:, 0], v[:, 1]).max() < p["ring_outer_r"]
    top_face = _solid("asm_top").slice(p["H"] - 0.05)
    h = p["button_hole_d"] / 2
    rim = manifold3d.CrossSection.circle(h + 0.15, 96).translate(list(c)) - manifold3d.CrossSection.circle(h + 0.05, 96).translate(list(c))
    assert float((rim - _disc(p["top_face_r"])).area()) == 0.0
    assert float((top_face ^ rim).area()) == pytest.approx(float(rim.area()), rel=0.02)
    assert float((top_face ^ manifold3d.CrossSection.circle(h - 0.05, 96).translate(list(c))).area()) == 0.0
    assert stem < h


def _loop_distance(points, loop):
    a = loop[None, :, :]
    ab = np.roll(loop, -1, axis=0)[None, :, :] - a
    ap = points[:, None, :] - a
    t = np.clip((ap * ab).sum(axis=2) / (ab * ab).sum(axis=2), 0.0, 1.0)
    nearest = a + t[:, :, None] * ab
    return np.linalg.norm(points[:, None, :] - nearest, axis=2).min(axis=1)


def test_button_hole_rim_to_the_outer_surface_is_at_least_0_8_all_the_way_down():
    p = params()
    bx, by = _button_xy()
    assert p["btn_rim_min"] >= 0.8
    angles = np.radians(np.arange(0, 360, 4.0))
    hole = np.stack([bx + p["button_hole_d"] / 2 * np.cos(angles), by + p["button_hole_d"] / 2 * np.sin(angles)], axis=1)
    for depth in (0.02, 0.1, 0.2, 0.3, 0.45, 0.6, 0.8, 1.0, 1.3, 1.7, 2.2, 3.0):
        loops = _loops("asm_top", p["H"] - depth)
        outer = max(loops, key=lambda q: abs(_signed_area(q)))
        rim = float(_loop_distance(hole, outer).min())
        assert rim >= p["btn_rim_min"], (depth, rim)
    for depth, lobe in ((0.3, True), (p["btn_collar_h"] + 0.05, False), (2.0, False)):
        outer = max(_loops("asm_top", p["H"] - depth), key=lambda q: abs(_signed_area(q)))
        r = np.linalg.norm(outer, axis=1)
        assert (r.max() - r.min() > 0.2) == lobe, depth
