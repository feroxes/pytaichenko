import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent
TOP_OVERHANG_MAX = 80

CLEAR_OF = [
    "speaker",
    "button",
    "speaker_screws",
    "metal_washers",
    "inserts",
    "top_screws",
    "filament",
    "mic_l",
    "mic_r",
    "devkit",
    "antenna",
    "usb_plug",
    "amp",
    "cap",
    "transistor",
    "top_gasket",
    "filament_tails",
]


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def solid_at(part, point, s=0.05):
    cube = manifold3d.Manifold.cube([s, s, s], True).translate(list(point))
    return float((_solid(part) ^ cube).volume()) / s ** 3 > 0.5


def _disc(r, c=(0.0, 0.0)):
    return manifold3d.CrossSection.circle(r, 360).translate(list(c))


def _annulus(r0, r1, c=(0.0, 0.0)):
    return _disc(r1, c) - _disc(r0, c)


def _loops(part, z):
    return [np.asarray(q) for q in _solid(part).slice(z).to_polygons()]


def _signed_area(q):
    x, y = q[:, 0], q[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(np.roll(x, -1), y))


def _centroid(q):
    x, y = q[:, 0], q[:, 1]
    x1, y1 = np.roll(x, -1), np.roll(y, -1)
    cr = x * y1 - x1 * y
    a = cr.sum() / 2
    return np.array([((x + x1) * cr).sum() / (6 * a), ((y + y1) * cr).sum() / (6 * a)])


def _hole_near(part, z, c, r_max):
    holes = [q for q in _loops(part, z) if _signed_area(q) < 0 and np.linalg.norm(_centroid(q) - c) < r_max]
    assert len(holes) == 1, (z, c, len(holes))
    return holes[0]


def _button_xy():
    p = params()
    a = np.radians(p["button_az"])
    return p["button_r"] * np.cos(a), p["button_r"] * np.sin(a)


def _button_local(p, lx, ly):
    a = np.radians(p["button_az"])
    bx, by = _button_xy()
    return bx + lx * np.cos(a) - ly * np.sin(a), by + lx * np.sin(a) + ly * np.cos(a)


def _cyl(d, z0, z1, c=(0.0, 0.0), fn=96):
    return manifold3d.Manifold.cylinder(z1 - z0, d / 2, d / 2, fn).translate([c[0], c[1], z0])


def test_top_watertight_one_body_fits_bed():
    p = params()
    m = mesh("top")
    assert m.is_watertight
    assert len(_manifold(m).decompose()) == 1
    assert m.volume > 0
    assert m.extents[0] < p["bed"] and m.extents[1] < p["bed"]
    assert abs(m.bounds[0][2]) < 1e-6


def test_print_top_is_the_flipped_assembly_face_down():
    p = params()
    a = mesh("asm_top")
    b = mesh("top")
    assert b.volume == pytest.approx(a.volume, rel=1e-6)
    assert a.bounds[1][2] == pytest.approx(p["H"], abs=1e-4)
    assert a.bounds[0][2] == pytest.approx(p["top_z0"], abs=1e-4)
    assert b.extents[2] == pytest.approx(p["H"] - p["top_z0"], abs=1e-4)
    assert np.allclose(a.bounds[:, 0], b.bounds[:, 0], atol=1e-4)
    flat = float(_solid("top").slice(0.05).area())
    dots = sum((d / 2) ** 2 for _x, _y, d, _t in p["sf_pts"])
    expect = np.pi * (p["top_face_r"] ** 2 - dots
                      - (p["button_hole_d"] / 2) ** 2 - len(p["top_screw_angles"]) * (p["top_plug_d"] / 2) ** 2)
    assert flat == pytest.approx(expect, rel=0.02)


@pytest.mark.parametrize("name", CLEAR_OF)
def test_top_clear(name):
    assert overlap("asm_top", f"asm_{name}") < TOL


def test_top_outer_size_and_rounded_edge():
    p = params()
    m = mesh("asm_top")
    r_all = np.linalg.norm(m.vertices[:, :2], axis=1)
    assert r_all.max() <= p["case_r"] + 0.01
    for z in (p["top_z0"] + 0.05, p["top_edge_z0"] - 0.05):
        r = max(np.linalg.norm(q, axis=1).max() for q in _loops("asm_top", z))
        assert r == pytest.approx(p["case_r"], abs=0.02)
    prev = None
    for z in np.linspace(p["top_edge_z0"] + 0.2, p["H"] - 0.05, 12):
        r = max(np.linalg.norm(q, axis=1).max() for q in _loops("asm_top", z))
        if prev is not None:
            assert r <= prev + 1e-6
        prev = r
    assert prev < p["case_r"] - 2.0


def test_skirt_sits_on_the_body_rim():
    p = params()
    sl = _solid("asm_top").slice(p["top_z0"] + 0.05)
    seat = _annulus(p["top_skirt_in_r"], p["top_groove_in_r"]) + _annulus(p["top_groove_out_r"], p["case_r"])
    assert float((seat - sl).area()) < 0.05
    groove = _annulus(p["top_groove_in_r"] + 0.02, p["top_groove_out_r"] - 0.02)
    assert float((groove ^ sl).area()) < 1e-3
    inside = sl ^ _disc(p["top_skirt_in_r"] - 0.01)
    bx, by = _button_xy()
    near_button = _disc(p["btn_out"] * 1.5 + 1.0, (bx, by))
    assert float((inside - near_button).area()) < 1e-3
    assert p["top_skirt_in_r"] == pytest.approx(p["case_r"] - p["wall_t"] + 0.3)


def test_grille_clearance_over_the_cone():
    p = params()
    zone = _cyl(p["spk_open_d"], p["spk_rim_top_z"], p["spk_rim_top_z"] + p["spk_grille_gap"])
    assert float((zone ^ _solid("asm_top")).volume()) < TOL
    assert p["grille_bot_z"] == pytest.approx(p["spk_rim_top_z"] + p["spk_grille_gap"])
    grille = _solid("asm_top").slice(p["grille_bot_z"] + 0.1) ^ _disc(p["spk_open_d"] / 2)
    assert float(grille.area()) > 0.5 * np.pi * (p["spk_open_d"] / 2) ** 2


def test_every_through_hole_is_open_through():
    p = params()
    m = mesh("asm_top")
    for x0, y0, d, t in p["sf_pts"]:
        if t != 1:
            continue
        for ang in [None] + list(range(0, 360, 45)):
            x, y = (x0, y0) if ang is None else (x0 + (d / 2 - 0.15) * np.cos(np.radians(ang)),
                                                 y0 + (d / 2 - 0.15) * np.sin(np.radians(ang)))
            assert first_hit(m, (x, y, p["top_under_z"] - 1.0), (0, 0, 1)) is None, (x0, y0, ang)


def test_gasket_seat_and_compression():
    p = params()
    assert p["spk_gasket_id"] == pytest.approx(p["spk_flange_d"] - 2 * p["spk_rim_w"])
    assert p["spk_gasket_od"] == pytest.approx(p["spk_flange_d"])
    band = _annulus(p["spk_gasket_id"] / 2, p["spk_gasket_od"] / 2)
    below = _solid("asm_top").slice(p["top_under_z"] - 0.05)
    above = _solid("asm_top").slice(p["top_under_z"] + 0.05)
    assert float((below ^ band).area()) < 1e-3
    assert float((band - above).area()) < 0.05
    lip = _annulus(p["spk_lip_in_r"], p["spk_lip_in_r"] + p["spk_lip_w"])
    at_lip = _solid("asm_top").slice(p["spk_lip_z0"] + 0.05)
    assert float((lip - at_lip).area()) < 0.05
    assert float((at_lip ^ _disc(p["spk_lip_in_r"] - 0.01)).area()) < 1e-3
    assert p["spk_lip_in_r"] - p["spk_gasket_od"] / 2 == pytest.approx(p["clr"])
    squeeze = p["spk_gasket_t"] - (p["top_under_z"] - p["spk_rim_top_z"])
    assert 0.2 <= squeeze / p["spk_gasket_t"] <= 0.3
    rim_top = mesh("asm_speaker").vertices
    rim_top = rim_top[np.linalg.norm(rim_top[:, :2], axis=1) > p["spk_gasket_id"] / 2 + 0.1][:, 2].max()
    assert rim_top == pytest.approx(p["spk_rim_top_z"], abs=1e-4)


def _lowest_top_z(x, y, z_lo, z_hi, step=0.01):
    for z in np.arange(z_lo, z_hi, step):
        if solid_at("asm_top", (x, y, z), 0.02):
            return float(z)
    return None


def test_ear_standoff_height():
    p = params()
    seat = _lowest_top_z(p["spk_flange_d"] / 2 - p["spk_rim_w"] / 2, 2.0, p["spk_rim_top_z"], p["H"])
    assert seat == pytest.approx(p["top_under_z"], abs=0.02)
    for ex, ey in p["spk_ear_xy"]:
        face = _lowest_top_z(ex + np.sign(ex) * 2.8, ey, p["spk_flange_bot_z"], p["top_under_z"])
        assert face is not None
        compressed_rim_top = seat - p["spk_gasket_c"]
        ear_plane = compressed_rim_top - p["spk_rim_h"]
        assert face == pytest.approx(ear_plane, abs=0.05)
        assert face == pytest.approx(p["spk_ear_z"], abs=0.05)
        assert p["spk_ear_standoff_h"] == pytest.approx(p["spk_rim_h"] + p["spk_gasket_c"])
        standoff = _solid("asm_top").slice(face + 0.1) ^ _disc(p["boss_od"] / 2 + 0.3, (ex, ey))
        assert float(standoff.area()) == pytest.approx(np.pi / 4 * (p["boss_od"] ** 2 - p["insert_d"] ** 2), rel=0.02)
    spk = _solid("asm_speaker")
    top = _solid("asm_top")
    assert float((spk.translate([0, 0, 0.05]) ^ top).volume()) > 0.05


def test_speaker_screws_engage_the_standoff_inserts():
    p = params()
    assert p["spk_screw_thread"] >= p["min_thread"]
    assert p["spk_screw_engage"] >= p["insert_len"]
    screws = mesh("asm_speaker_screws")
    assert screws.bounds[1][2] == pytest.approx(p["spk_screw_tip_z"], abs=1e-3)
    assert screws.bounds[0][2] == pytest.approx(p["spk_screw_head_z"], abs=1e-3)
    washers = mesh("asm_metal_washers")
    assert washers.bounds[1][2] == pytest.approx(p["spk_flange_bot_z"], abs=1e-4)
    inserts = mesh("asm_spk_inserts")
    assert inserts.bounds[0][2] == pytest.approx(p["spk_standoff_z0"], abs=1e-4)
    assert inserts.bounds[1][2] == pytest.approx(p["spk_standoff_z0"] + p["insert_len"], abs=1e-4)
    for ex, ey in p["spk_ear_xy"]:
        hole = _hole_near("asm_top", p["spk_standoff_z0"] + 1.0, (ex, ey), 1.0)
        assert np.linalg.norm(_centroid(hole) - [ex, ey]) < 0.01
        assert np.linalg.norm(hole - [ex, ey], axis=1).max() == pytest.approx(p["insert_d"] / 2, abs=0.02)
        assert not solid_at("asm_top", (ex, ey, p["spk_insert_top_z"] - 0.1))
        assert solid_at("asm_top", (ex, ey, p["spk_insert_top_z"] + 0.1))
        assert p["spk_insert_top_z"] >= p["spk_screw_tip_z"] + p["insert_tip_gap"] - 1e-9
        assert p["H"] - p["spk_insert_top_z"] >= 1.6


def test_speaker_screws_reachable_from_below_with_only_top_and_speaker():
    p = params()
    for ex, ey in p["spk_ear_xy"]:
        start = (ex, ey, p["spk_screw_head_z"] - 0.05)
        for part in ("asm_top", "asm_speaker", "asm_metal_washers"):
            assert first_hit(mesh(part), start, (0, 0, -1)) is None, part
        tool = _cyl(p["screw_head_d"] + 1.0, p["spk_screw_head_z"] - 30, p["spk_screw_head_z"] - 0.05, (ex, ey))
        for part in ("asm_top", "asm_speaker"):
            assert float((tool ^ _solid(part)).volume()) < TOL, part


def test_plug_seats_are_coaxial_with_their_countersinks():
    p = params()
    assert len(p["top_screw_xy"]) == len(p["top_screw_angles"]) == 3
    for (x, y), a in zip(p["top_screw_xy"], p["top_screw_angles"]):
        assert np.hypot(x, y) == pytest.approx(p["top_screw_r"], abs=1e-3)
        assert np.degrees(np.arctan2(y, x)) % 360 == pytest.approx(a % 360, abs=1e-2)
        plug = _hole_near("asm_top", p["H"] - 0.1, (x, y), 1.0)
        csk = _hole_near("asm_top", p["top_seat_z"] - 0.2, (x, y), 1.0)
        thru = _hole_near("asm_top", p["top_seat_z"] - p["csk_h"] - 0.3, (x, y), 1.0)
        for q, r in ((plug, p["top_plug_d"] / 2), (csk, p["csk_d"] / 2), (thru, p["screw_thru_d"] / 2)):
            assert np.linalg.norm(_centroid(q) - [x, y]) < 0.01
            assert np.linalg.norm(q - [x, y], axis=1).max() == pytest.approx(r, abs=0.02)
        assert not solid_at("asm_top", (x + p["top_plug_d"] / 2 - 0.1, y, p["top_seat_z"] + 0.1))
        assert solid_at("asm_top", (x + p["top_plug_d"] / 2 - 0.1, y, p["top_seat_z"] - 0.1))
        face = _solid("asm_top").slice(p["H"] - 0.05)
        ring = _annulus(p["top_plug_d"] / 2 + 0.05, p["top_plug_d"] / 2 + p["top_plug_rim"], (x, y))
        assert float((face ^ ring).area()) == pytest.approx(float(ring.area()), rel=0.01)
    heads = mesh("asm_top_screws")
    assert heads.bounds[1][2] <= p["H"] - p["top_plug_depth"] + 1e-6
    assert heads.bounds[1][2] == pytest.approx(p["top_screw_head_z"], abs=1e-4)
    assert heads.bounds[0][2] == pytest.approx(p["top_screw_tip_z"], abs=1e-4)
    assert p["top_seat_floor_t"] >= 1.6
    assert p["top_seat_floor_t"] == pytest.approx(p["top_seat_z"] - p["csk_h"] - p["top_boss_z0"], abs=1e-9)


def test_top_screws_reachable_from_above():
    p = params()
    for x, y in p["top_screw_xy"]:
        assert first_hit(mesh("asm_top"), (x, y, p["H"] + 5), (0, 0, -1)) is None
        for ang in range(0, 360, 45):
            px = x + 1.3 * np.cos(np.radians(ang))
            py = y + 1.3 * np.sin(np.radians(ang))
            assert first_hit(mesh("asm_top"), (px, py, p["H"] + 5), (0, 0, -1)) is None


def test_column_sockets_and_body_column_room():
    p = params()
    tol_parts = ["asm_speaker", "asm_speaker_screws", "asm_metal_washers", "asm_spk_inserts", "asm_button",
                 "asm_mic_l", "asm_mic_r", "asm_cap", "asm_amp", "asm_filament", "asm_devkit", "asm_transistor"]
    z0 = p["top_insert_bot_z"] - 3.0
    for x, y in p["top_screw_xy"]:
        assert np.hypot(x, y) + p["boss_od"] / 2 <= p["case_r"] - p["wall_t"] - 1.0
        column = _cyl(p["boss_od"], z0, p["top_col_z1"], (x, y))
        assert float((column ^ _solid("asm_top")).volume()) < TOL
        assert float((column.translate([0, 0, p["top_boss_gap"] + 0.05]) ^ _solid("asm_top")).volume()) > 0.01
        room = _cyl(p["boss_od"] + 2.0, z0, p["top_col_z1"], (x, y))
        for part in tol_parts:
            assert float((room ^ _solid(part)).volume()) < TOL, (x, y, part)
        sock = _hole_near("asm_top", p["top_under_z"] + 0.2, (x, y), 1.0)
        assert np.linalg.norm(sock - [x, y], axis=1).max() == pytest.approx(p["top_sock_d"] / 2, abs=0.02)
    assert p["top_screw_tip_z"] - p["insert_tip_gap"] >= p["top_insert_bot_z"] - 1e-9
    assert p["top_screw_engage"] >= p["insert_len"]
    assert p["top_col_z1"] > p["top_under_z"]
    for ex, ey in p["spk_ear_xy"]:
        for x, y in p["top_screw_xy"]:
            assert np.hypot(x - ex, y - ey) > p["boss_od"] / 2 + p["washer_od"] / 2 + 3.0
    ear = _solid("asm_speaker").slice(p["spk_flange_bot_z"] + 0.3)
    for x, y in p["top_screw_xy"]:
        far = _disc(p["boss_od"] / 2 + 1.0, (x, y))
        assert float((ear ^ far).area()) < 1e-6


def test_button_hole_and_bore():
    p = params()
    bx, by = _button_xy()
    hole = _hole_near("asm_top", p["H"] - 0.5, (bx, by), 1.0)
    assert np.linalg.norm(hole - [bx, by], axis=1).max() == pytest.approx(p["button_hole_d"] / 2, abs=0.02)
    bore = _disc(p["btn_bore_d"] / 2 - 0.02, (bx, by))
    assert float((_solid("asm_top").slice(p["btn_plate_z"] - 0.5) ^ bore).area()) < 1e-3
    for lx in (-1, 1):
        qx, qy = _button_local(p, lx * (p["btn_bore_d"] / 2 + 0.3), 0.0)
        assert solid_at("asm_top", (qx, qy, p["btn_plate_z"] - 0.5))
    assert not solid_at("asm_top", (bx, by, p["btn_plate_z"] - 0.2))
    for ang in (0, 90, 180, 270):
        qx = bx + (p["button_hole_d"] / 2 + 0.3) * np.cos(np.radians(ang))
        qy = by + (p["button_hole_d"] / 2 + 0.3) * np.sin(np.radians(ang))
        assert solid_at("asm_top", (qx, qy, p["H"] - 0.5))
        assert solid_at("asm_top", (qx, qy, p["btn_plate_z"] + 0.2))
    assert p["button_r"] + p["button_hole_d"] / 2 + p["btn_rim_min"] < p["top_face_r"]


def test_button_holder_keeps_the_switch():
    p = params()
    top = _solid("asm_top")
    btn = _solid("asm_button")
    ang = np.radians(p["button_az"])
    rad = np.array([np.cos(ang), np.sin(ang), 0.0])
    tan = np.array([-np.sin(ang), np.cos(ang), 0.0])
    assert float((btn ^ top).volume()) < TOL
    assert float((btn.translate([0, 0, p["btn_ceil_gap"] + 0.1]) ^ top).volume()) > 0.1
    assert float((btn.translate([0, 0, -0.3]) ^ top).volume()) > 0.1
    for s in (-1, 1):
        assert float((btn.translate(list(s * 0.3 * tan)) ^ top).volume()) > 0.05
        assert float((btn.translate(list(s * 0.3 * rad)) ^ top).volume()) > 0.05


def test_button_ceiling_gap():
    p = params()
    qx, qy = _button_local(p, -5.8, 3.5)
    top_of_body = p["btn_z0"] + p["btn_body_h"]
    gap = None
    for z in np.arange(top_of_body, p["btn_plate_z"], 0.05):
        if solid_at("asm_top", (qx, qy, z)):
            gap = z - top_of_body
            break
    assert gap is not None
    assert gap == pytest.approx(p["btn_ceil_gap"], abs=0.06)
    assert 0.4 <= p["btn_ceil_gap"] <= 0.6


def test_button_hooks_grip_under_the_switch_on_a_flat_catch_face():
    p = params()
    assert 0.6 <= p["btn_hook_ov"] <= 0.8
    tip = p["btn_w"] / 2 - p["btn_hook_ov"]
    z0 = p["btn_z0"]
    for sy in (-1, 1):
        for ly in np.linspace(tip + 0.1, p["btn_in"] - 0.05, 6):
            for lx in (-p["btn_tab_w"] / 2 + 0.2, 0.0, p["btn_tab_w"] / 2 - 0.2):
                qx, qy = _button_local(p, lx, sy * ly)
                assert solid_at("asm_top", (qx, qy, z0 - 0.05), 0.03), (ly, lx)
                assert not solid_at("asm_top", (qx, qy, z0 + 0.05), 0.03), (ly, lx)
        qx, qy = _button_local(p, 0.0, sy * (tip - 0.05))
        assert not solid_at("asm_top", (qx, qy, z0 - 0.05), 0.03)
    assert p["btn_hook_z0"] > p["top_z0"]
    assert p["btn_tab_strain"] <= 0.013
    assert p["btn_root_out"] >= 2.0 and p["btn_tab_t"] >= 0.8


def test_button_tabs_flex_freely_in_their_reliefs():
    p = params()
    for sy in (-1, 1):
        for z in (p["top_under_z"] + 0.3, p["btn_plate_z"] + 0.5, p["btn_root_z"] - 0.4):
            qx, qy = _button_local(p, 0.0, sy * (p["btn_in"] + p["btn_tab_t"] + p["btn_tab_flex"] + 0.05))
            if z < p["btn_root_z"] - p["btn_root_out"]:
                assert not solid_at("asm_top", (qx, qy, z), 0.04), z
            qx, qy = _button_local(p, 0.0, sy * (p["btn_relief_y1"] + 0.1))
            assert solid_at("asm_top", (qx, qy, z), 0.04), z
        qx, qy = _button_local(p, 0.0, sy * (p["btn_in"] + p["btn_tab_t"] / 2))
        assert solid_at("asm_top", (qx, qy, p["btn_root_z"] - 0.1), 0.04)
        assert solid_at("asm_top", (qx, qy, p["btn_root_z"] + 0.1), 0.04)
        for sx in (-1, 1):
            qx, qy = _button_local(p, sx * (p["btn_tab_w"] / 2 + p["btn_tab_slit"] / 2), sy * (p["btn_in"] + p["btn_wall"] / 2))
            for z in (p["btn_z0"] + 0.5, p["top_under_z"] + 0.5, p["btn_root_z"] - 0.1):
                assert not solid_at("asm_top", (qx, qy, z), 0.02), z
    assert p["H"] - p["btn_root_z"] >= 1.0


def test_switch_snaps_in_from_below_and_cannot_drop_out_without_flexing():
    p = params()
    btn = _solid("asm_button")
    top = _solid("asm_top")
    flexed = _solid("asm_top_flexed")
    start = mesh("asm_top").bounds[0][2] - mesh("asm_button").bounds[1][2] - 0.25
    dzs = np.arange(start, 0.001, 0.25)
    assert max(float((btn.translate([0, 0, float(dz)]) ^ flexed).volume()) for dz in dzs) < TOL
    assert max(float((btn.translate([0, 0, float(dz)]) ^ top).volume()) for dz in dzs) > 0.05
    for dz in (-0.3, -0.6, -1.0):
        assert float((btn.translate([0, 0, dz]) ^ top).volume()) > 0.1


def test_wire_paths_down_from_the_button_and_the_speaker_terminals():
    p = params()
    for sy in (-1, 1):
        lx, ly = -p["btn_leg_x"], sy * p["btn_leg_y"]
        qx, qy = _button_local(p, lx, ly)
        assert first_hit(mesh("asm_top"), (qx, qy, p["btn_z0"] - 0.05), (0, 0, -1)) is None
        channel = _cyl(3.0, p["top_z0"] - 5.0, p["btn_z0"] - 0.05, (qx, qy), 48)
        assert float((channel ^ _solid("asm_top")).volume()) < TOL
    assert mesh("asm_top").bounds[0][2] >= p["top_z0"] - 1e-6
    bx, by = _button_xy()
    holder = _disc(np.hypot(p["btn_out"], p["btn_out"]) + 0.2, (bx, by))
    under = _solid("asm_top").slice(p["spk_flange_bot_z"] - 0.1) ^ _disc(p["top_skirt_in_r"] - 0.01)
    assert float(under.area()) > 1.0
    assert float((under - holder).area()) < 1e-3


def test_build_lists_the_top():
    text = (CASE_DIR / "build.sh").read_text()
    assert '"top:PETG-black_top"' in text
