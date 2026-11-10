import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent
BODY_OVERHANG_MAX = 60

CLEAR_OF = [
    "speaker",
    "devkit",
    "antenna",
    "usb_plug",
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
    "ring_arc",
]


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def solid_at(part, point, s=0.05):
    cube = manifold3d.Manifold.cube([s, s, s], True).translate(list(point))
    return float((_solid(part) ^ cube).volume()) / s ** 3 > 0.5


def _disc(r, c=(0.0, 0.0), fn=360):
    return manifold3d.CrossSection.circle(r, fn).translate(list(c))


def _annulus(r0, r1):
    return _disc(r1) - _disc(r0)


def _loops(part, z):
    return [np.asarray(q) for q in _solid(part).slice(float(z)).to_polygons()]


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


def _dir(az):
    a = np.radians(az)
    return np.array([np.cos(a), np.sin(a), 0.0]), np.array([-np.sin(a), np.cos(a), 0.0])


def _mic_frame(p, az):
    rad, tan = _dir(az)
    return rad, tan, np.array([0.0, 0.0, 1.0])


def _mic_point(p, az, x, ly, lz):
    rad, tan, up = _mic_frame(p, az)
    return x * rad + ly * tan + (p["mic_z"] + lz) * up


def _mics(p):
    return (("l", p["mic_az_l"]), ("r", p["mic_az_r"]))


def test_body_watertight_one_body_fits_bed():
    p = params()
    m = mesh("body")
    assert m.is_watertight
    assert len(_manifold(m).decompose()) == 1
    assert m.volume > 0
    assert m.extents[0] < p["bed"] and m.extents[1] < p["bed"]
    assert abs(m.bounds[0][2]) < 1e-6


def test_print_body_is_the_assembly_body_standing_on_its_bottom():
    p = params()
    a = mesh("asm_body")
    b = mesh("body")
    assert b.volume == pytest.approx(a.volume, rel=1e-6)
    assert a.bounds[0][2] == pytest.approx(p["body_bot_z"], abs=1e-4)
    assert a.bounds[1][2] == pytest.approx(p["top_col_z1"], abs=1e-4)
    assert b.extents[2] == pytest.approx(p["top_col_z1"] - p["body_bot_z"], abs=1e-4)
    assert np.allclose(a.bounds[:, :2], b.bounds[:, :2], atol=1e-4)
    foot = _solid("body").slice(0.05)
    ring = np.pi * (p["case_r"] ** 2 - p["inner_r"] ** 2)
    assert float(foot.area()) > ring * 0.99


@pytest.mark.parametrize("name", CLEAR_OF)
def test_body_clear(name):
    assert overlap("asm_body", f"asm_{name}") < TOL


def test_tube_is_84_with_a_3_mm_wall():
    p = params()
    for z in (p["body_bot_z"] + 0.5, 35.0, p["ring_z0"] - 0.5):
        r = max(np.linalg.norm(q, axis=1).max() for q in _loops("asm_body", z))
        assert r == pytest.approx(p["case_r"], abs=0.02), z
    for az in (0, 180, 270, 300):
        rad, _ = _dir(az)
        o = np.array([0.0, 0.0, 35.0])
        t_in = first_hit(mesh("asm_body"), o, rad)
        assert t_in == pytest.approx(p["inner_r"], abs=0.02), az
        t_out = first_hit(mesh("asm_body"), o + (t_in + 0.01) * rad, rad)
        assert t_in + 0.01 + t_out == pytest.approx(p["case_r"], abs=0.02), az


def test_bridge_fills_the_ring_gap_up_to_the_lid():
    p = params()
    z = p["top_z0"] - 0.3
    for az in np.linspace(p["bridge_a0"] + 15, p["bridge_a1"] - 15, 9):
        rad, _ = _dir(az)
        for r in (p["inner_r"] + 0.1, p["filament_r"], p["case_r"] - 0.1):
            assert solid_at("asm_body", r * rad + [0, 0, z]), (az, r)
    for a, s in ((p["bridge_a0"], 1), (p["bridge_a1"], -1)):
        inside, _ = _dir(a + s * 0.3)
        outside, _ = _dir(a - s * 0.3)
        q = p["case_r"] - 0.3
        assert solid_at("asm_body", q * inside + [0, 0, z])
        assert not solid_at("asm_body", q * outside + [0, 0, z])
    rad, _ = _dir(90)
    assert first_hit(mesh("asm_body"), rad * p["filament_r"] + [0, 0, p["ring_z0"] + 0.5], (0, 0, -1)) == pytest.approx(0.5, abs=1e-3)
    assert not solid_at("asm_body", rad * p["filament_r"] + [0, 0, p["fil_z"]])


def test_button_needs_no_body_wall_thinning():
    p = params()
    v = mesh("asm_button").vertices
    outer = v[np.hypot(v[:, 0], v[:, 1]) > p["inner_r"] - 1e-6]
    assert outer[:, 2].min() >= p["top_z0"] - 1e-6
    lid = mesh("asm_top").vertices
    reach = lid[np.hypot(lid[:, 0], lid[:, 1]) > p["inner_r"] + 0.3]
    assert reach[:, 2].min() >= p["top_z0"] - 1e-6
    assert p["btn_corner_r"] <= p["case_r"] - p["wall_thin_t"] + 1e-6


def test_rim_under_the_lid_is_closed_except_the_filament_slots():
    p = params()
    z = p["ring_top_z"] - 0.03
    rim = _solid("asm_body").slice(z) + _solid("asm_ring_arc").slice(z)
    band = _annulus(p["inner_r"], p["case_r"] - 0.01)
    open_ = band - rim
    slots = None
    for q in list(p["fil_tail_pts"]) + list(p["fil_tail_pts_m"]):
        d = _disc(p["chan_d"] / 2 + 0.1, (q[0], q[1]), 48)
        slots = d if slots is None else slots + d
    for i in range(len(p["fil_tail_pts"]) - 1):
        for pts in (p["fil_tail_pts"], p["fil_tail_pts_m"]):
            a, b = np.asarray(pts[i][:2]), np.asarray(pts[i + 1][:2])
            n = int(np.ceil(np.linalg.norm(b - a) / 0.3)) + 1
            for t in np.linspace(0, 1, n):
                slots = slots + _disc(p["chan_d"] / 2 + 0.1, tuple(a + t * (b - a)), 48)
    ends = None
    for a in (p["bridge_a0"] - p["ring_end_deg"] / 2, p["bridge_a1"] + p["ring_end_deg"] / 2):
        rad, tan = _dir(a)
        w = p["ring_end_gap"] / 2 + 0.05
        sq = manifold3d.CrossSection.square([p["case_r"] - p["inner_r"] + 0.2, 2 * w])
        sq = sq.translate([p["inner_r"] - 0.1, -w]).rotate(float(a))
        ends = sq if ends is None else ends + sq
    assert float((open_ - slots - ends).area()) < 1e-2
    assert float(open_.area()) < 25


def test_top_gasket_sits_in_the_lid_groove_over_the_rim():
    p = params()
    assert 0.2 <= p["top_gasket_squeeze"] / p["top_gasket_t"] <= 0.3
    assert p["top_groove_depth"] == pytest.approx(p["top_gasket_t"] - p["top_gasket_squeeze"])
    assert p["top_groove_out_r"] - p["top_groove_in_r"] >= p["top_gasket_w"] * p["top_gasket_t"] / p["top_gasket_c"] - 1e-9
    sl = _solid("asm_top").slice(float(p["top_z0"] + p["top_groove_depth"] / 2))
    groove = _annulus(p["top_groove_in_r"] + 0.02, p["top_groove_out_r"] - 0.02)
    assert float((groove ^ sl).area()) < 1e-3
    for r0, r1 in ((p["top_skirt_in_r"], p["top_groove_in_r"]), (p["top_groove_out_r"], p["case_r"])):
        assert float((_annulus(r0 + 0.02, r1 - 0.02) - sl).area()) < 0.05
    above = _solid("asm_top").slice(float(p["top_z0"] + p["top_groove_depth"] + 0.05))
    assert float((groove - above).area()) < 0.05
    g = mesh("asm_top_gasket")
    r = np.hypot(g.vertices[:, 0], g.vertices[:, 1])
    assert r.min() == pytest.approx(p["top_groove_out_r"] - p["top_gasket_w"], abs=0.02)
    assert r.max() == pytest.approx(p["top_groove_out_r"], abs=0.02)
    assert g.bounds[0][2] == pytest.approx(p["top_z0"], abs=1e-4)
    assert g.extents[2] == pytest.approx(p["top_gasket_c"], abs=1e-4)
    rim = _solid("asm_body").slice(float(p["top_z0"] - 0.05)) + _solid("asm_ring_arc").slice(float(p["ring_top_z"] - 0.05))
    under = _annulus(p["top_groove_out_r"] - p["top_gasket_w"], p["top_groove_out_r"])
    assert float((under - rim).area()) < 12


def test_mic_hole_axis_passes_through_the_mic_centre():
    p = params()
    port = p["mic_port_d"] / 2
    for side, az in _mics(p):
        rad, tan, up = _mic_frame(p, az)
        c = _mic_point(p, az, p["mic_face_r"] - p["mic_t"] / 2, 0, 0)
        assert solid_at(f"asm_mic_{side}", c)
        assert first_hit(mesh("asm_body"), c, rad) is None
        for d in (port + 0.2, -(port + 0.2)):
            assert first_hit(mesh("asm_body"), c + d * tan, rad) is not None
            assert first_hit(mesh("asm_body"), c + d * up, rad) is not None
        x_mid = (p["inner_r"] + p["case_r"]) / 2
        for d in (port - 0.15, -(port - 0.15)):
            assert not solid_at("asm_body", _mic_point(p, az, x_mid, d, 0))
            assert not solid_at("asm_body", _mic_point(p, az, x_mid, 0, d))
        for d in (port + 0.15, -(port + 0.15)):
            assert solid_at("asm_body", _mic_point(p, az, x_mid, d, 0))
            assert solid_at("asm_body", _mic_point(p, az, x_mid, 0, d))


def test_mic_seat_seals():
    p = params()
    for side, az in _mics(p):
        probe = mesh(f"asm_mic_seal_probe_{side}")
        assert overlap("asm_body", f"asm_mic_seal_probe_{side}") == pytest.approx(probe.volume, rel=1e-3)
        assert overlap("asm_body", f"asm_mic_port_probe_{side}") < TOL
        rad, _, _ = _mic_frame(p, az)
        for rr in np.linspace(p["mic_port_d"] / 2 + 0.2, p["mic_d"] / 2 - 0.15, 6):
            for ang in np.linspace(0, 2 * np.pi, 12, endpoint=False):
                o = _mic_point(p, az, p["mic_face_r"] - 0.01, rr * np.cos(ang), rr * np.sin(ang))
                t = first_hit(mesh("asm_body"), o, rad)
                assert t is not None and t <= 0.02, (side, rr, ang)
                assert solid_at(f"asm_mic_{side}", o - 0.3 * rad, 0.02)


def test_mic_sits_recessed_in_a_pocket():
    p = params()
    assert p["mic_face_r"] - p["mic_open_r"] == pytest.approx(p["mic_t"])
    assert p["mic_pocket_r"] == pytest.approx(p["mic_d"] / 2 + p["mic_pocket_clr"])
    assert p["mic_pocket_clr"] >= 0.35
    for side, az in _mics(p):
        for ang in np.linspace(0, 2 * np.pi, 12, endpoint=False):
            c, s_ = np.cos(ang), np.sin(ang)
            for x in (p["mic_open_r"] + 0.1, p["mic_face_r"] - 0.1):
                r_in = p["mic_pocket_r"] - 0.1
                r_out = p["mic_pocket_r"] + 0.1
                assert not solid_at("asm_body", _mic_point(p, az, x, r_in * c, r_in * s_)), (side, ang, x)
                assert solid_at("asm_body", _mic_point(p, az, x, r_out * c, r_out * s_)), (side, ang, x)
            back = _mic_point(p, az, p["mic_open_r"] - 0.3, (p["mic_d"] / 2 - 0.3) * c, (p["mic_d"] / 2 - 0.3) * s_)
            assert first_hit(mesh(f"asm_mic_{side}"), back, _mic_frame(p, az)[0]) is not None
    assert p["mic_z"] + p["mic_d"] / 2 <= p["spk_magnet_top_z"]
    assert p["mic_z"] - p["mic_fill_r"] > p["body_bot_z"] + 1


def test_mic_port_flares_open_on_the_outside():
    p = params()
    for side, az in _mics(p):
        x = p["case_r"] - 0.05
        for ang in np.linspace(0, 2 * np.pi, 8, endpoint=False):
            c, s_ = np.cos(ang), np.sin(ang)
            r_in = p["mic_port_out_d"] / 2 - 0.15
            r_out = p["mic_port_out_d"] / 2 + 0.3
            assert not solid_at("asm_body", _mic_point(p, az, x, r_in * c, r_in * s_))
            assert solid_at("asm_body", _mic_point(p, az, x - 0.2, r_out * c, r_out * s_))


def test_column_insert_holes_are_coaxial_with_the_top_screws():
    p = params()
    for x, y in p["top_screw_xy"]:
        hole = _hole_near("asm_body", p["top_col_z1"] - 2.0, (x, y), 1.0)
        assert np.linalg.norm(_centroid(hole) - [x, y]) < 0.01
        assert np.linalg.norm(hole - [x, y], axis=1).max() == pytest.approx(p["insert_d"] / 2, abs=0.02)
        assert not solid_at("asm_body", (x, y, p["top_insert_bot_z"] + 0.3))
        assert solid_at("asm_body", (x, y, p["top_insert_bot_z"] - 0.3))
        outer = [q for q in _loops("asm_body", p["top_col_z1"] - 0.5) if _signed_area(q) > 0
                 and np.linalg.norm(_centroid(q) - [x, y]) < 0.5]
        assert len(outer) == 1
        assert np.linalg.norm(outer[0] - [x, y], axis=1).max() == pytest.approx(p["boss_od"] / 2, abs=0.02)
        for z in np.arange(p["top_ledge_z0"] + 0.2, p["top_insert_bot_z"], 1.0):
            assert solid_at("asm_body", (x, y, z)), z
    assert p["top_screw_r"] + p["boss_od"] / 2 <= p["ring_in_r"] - p["ring_in_clr"] + 1e-9
    assert p["top_ledge_z1"] == pytest.approx(p["ring_z0"] - p["ring_lip_h"] - p["ring_lip_clr"])


def test_tray_boss_insert_holes_are_coaxial_with_the_tray_screws():
    p = params()
    assert len(p["tray_screw_xy"]) >= 3
    for x, y in p["tray_screw_xy"]:
        hole = _hole_near("asm_body", p["body_bot_z"] + 2.0, (x, y), 1.0)
        assert np.linalg.norm(_centroid(hole) - [x, y]) < 0.01
        assert np.linalg.norm(hole - [x, y], axis=1).max() == pytest.approx(p["insert_d"] / 2, abs=0.02)
        assert not solid_at("asm_body", (x, y, p["tray_insert_top_z"] - 0.3))
        assert solid_at("asm_body", (x, y, p["tray_boss_top_z"] - 0.3))
        t = first_hit(mesh("asm_body"), (x, y, p["body_bot_z"] - 1.0), (0, 0, 1))
        assert t == pytest.approx(1.0 + p["tray_insert_hole_depth"] + p["tray_tip_h"], abs=0.05)
        assert np.hypot(x, y) + p["insert_d"] / 2 <= p["case_r"] - 2.5
    a = sorted(p["tray_screw_angles"])
    gaps = np.diff(a + [a[0] + 360])
    assert gaps.max() <= 110


def test_wire_paths_down_to_the_tray():
    p = params()
    for part in ("asm_body", "asm_top", "asm_ring_arc", "asm_speaker", "asm_button", "asm_cap", "asm_amp",
                 "asm_devkit", "asm_usb_plug", "asm_filament_tails", "asm_inserts"):
        assert overlap("asm_wire_probe", part) < TOL, part
    b = mesh("asm_wire_probe").bounds
    assert b[0][2] <= p["wire_probe_z1"] - p["wire_probe_d"] / 2 + 0.05
    start = np.asarray(p["wire_btn_pts"][0])
    leg = np.array([0.0, -(p["button_r"] - p["btn_leg_x"]), p["btn_z0"] - p["btn_leg_l"]])
    assert np.linalg.norm(start - leg) < 3.0
    start = np.asarray(p["wire_term_pts"][0])
    assert -start[1] - (p["spk_basket_d"] / 2 + p["spk_term_out"]) < p["wire_probe_d"] / 2 + 1.0
    assert p["spk_term_z0"] <= start[2] <= p["spk_term_z1"]


def test_body_overhangs():
    assert overhang_area(mesh("body")) < BODY_OVERHANG_MAX


def test_build_lists_body_and_ring():
    text = (CASE_DIR / "build.sh").read_text()
    assert '"body:PETG-black_body"' in text
