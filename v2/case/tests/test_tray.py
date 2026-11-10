import functools
from pathlib import Path

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent
TRAY_OVERHANG_MAX = 400

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
    "body",
    "tray_screws",
    "tray_gasket",
    "feet",
    "wire_probe",
    "tray_wire_probe",
    "tray_screw_probe",
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


def _hit(part, o, d):
    return first_hit(mesh(part), np.asarray(o, dtype=float), np.asarray(d, dtype=float))


def _box_volume(part, x0, x1, y0, y1, z0, z1):
    b = manifold3d.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])
    return float((_solid(part) ^ b).volume())


def _latch_ys(p):
    return [p["dks_latch_y"]]


def _strip_ys(p):
    y0 = p["dk_pin_y0"] - p["dk_pin_pitch"] / 2
    y1 = p["dk_pin_y1"] + p["dk_pin_pitch"] / 2
    ys = np.linspace(y0 + 1, y1 - 1, 15)
    w = p["dks_latch_w"] / 2 + p["latch_relief_clr"] + 0.5
    return [y for y in ys if all(abs(y - ly) > w for ly in _latch_ys(p))]


def test_tray_watertight_one_body_on_the_bed():
    p = params()
    m = mesh("tray")
    assert m.is_watertight
    assert len(_manifold(m).decompose()) == 1
    assert m.volume > 0
    assert abs(m.bounds[0][2]) < 1e-6
    assert m.bounds[1][2] == pytest.approx(p["body_bot_z"], abs=1e-4)
    a = mesh("asm_tray")
    assert a.volume == pytest.approx(m.volume, rel=1e-6)
    assert np.allclose(a.bounds, m.bounds, atol=1e-4)


@pytest.mark.parametrize("name", CLEAR_OF)
def test_tray_clear(name):
    assert overlap("asm_tray", f"asm_{name}") < TOL


def test_tray_is_a_cup_flush_with_the_body():
    p = params()
    for z in (0.5, 5.0, p["body_bot_z"] - 0.5):
        sl = _solid("asm_tray").slice(float(z))
        r = max(np.linalg.norm(np.asarray(q), axis=1).max() for q in sl.to_polygons())
        assert r == pytest.approx(p["case_r"], abs=0.02), z
    for az in (0, 150, 200):
        a = np.radians(az)
        rad = np.array([np.cos(a), np.sin(a), 0.0])
        o = np.array([0.0, 0.0, 10.0])
        t_in = _hit("asm_tray", o, rad)
        assert t_in == pytest.approx(p["inner_r"], abs=0.02), az
    rim = _solid("asm_tray").slice(float(p["body_bot_z"] - 0.05))
    body = _solid("asm_body").slice(float(p["body_bot_z"] + 0.05))
    band = _annulus(p["inner_r"] + 0.02, p["case_r"] - 0.02)
    groove = _annulus(p["tray_groove_in_r"] - 0.02, p["tray_groove_out_r"] + 0.02)
    assert float((band - groove - rim).area()) < 0.05
    holes = None
    for x, y in p["tray_screw_xy"]:
        d = _disc(p["insert_d"] / 2 + 0.02, (x, y), 96)
        holes = d if holes is None else holes + d
    assert float((band - holes - body).area()) < 0.05
    assert _hit("asm_tray", (0, 0, 0.5), (0, 0, 1)) == pytest.approx(p["tray_floor_t"] - 0.5, abs=1e-3)


def test_usb_plug_reaches_socket():
    p = params()
    assert overlap("asm_usb_plug", "asm_tray") < TOL
    assert overlap("asm_usb_plug", "asm_body") < TOL
    plug = mesh("asm_usb_plug").bounds
    assert plug[1][1] == pytest.approx(p["usb_face_y"] - p["usb_plug_seat"] + p["usb_shell_l"], abs=1e-4)
    assert plug[1][0] - plug[0][0] == pytest.approx(p["usb_plug_w"], abs=1e-4)
    assert plug[1][2] - plug[0][2] == pytest.approx(p["usb_plug_h"], abs=1e-4)
    cx, cz = p["dk_com_x"], p["usb_z"]
    hw, hh = p["tray_com_w"] / 2, p["tray_com_h"] / 2
    assert hw == pytest.approx(p["usb_plug_w"] / 2 + p["clr"])
    assert hh == pytest.approx(p["usb_plug_h"] / 2 + p["clr"])
    r_mid = (p["inner_r"] + p["case_r"]) / 2

    def y_mid(x):
        return -np.sqrt(r_mid ** 2 - x ** 2)

    for dx in (-hw + 0.1, 0, hw - 0.1):
        for dz in (-hh + 0.1, 0, hh - 0.1):
            assert not solid_at("asm_tray", (cx + dx, y_mid(cx + dx), cz + dz)), (dx, dz)
    for dx, dz in ((-hw - 0.15, 0), (hw + 0.15, 0), (0, -hh - 0.15), (0, hh + 0.15)):
        assert solid_at("asm_tray", (cx + dx, y_mid(cx + dx), cz + dz)), (dx, dz)
    assert p["tray_com_web"] >= 0.5
    assert cz + hh + p["tray_com_web"] == pytest.approx(p["body_bot_z"] - p["tray_groove_depth"])
    small = mesh("asm_com_plug").bounds
    assert small[1][0] - small[0][0] == pytest.approx(p["com_plug_w"], abs=1e-4)
    assert small[1][2] - small[0][2] == pytest.approx(p["com_plug_h"], abs=1e-4)
    assert (p["com_plug_w"], p["com_plug_h"]) == (10, 6.2)
    assert small[1][1] == pytest.approx(plug[1][1], abs=1e-4)
    for part in ("asm_tray", "asm_body", "asm_com_bushing", "asm_devkit"):
        assert overlap("asm_com_plug", part) < TOL, part
    face = p["usb_face_y"] - p["usb_plug_seat"]
    ox = (small[0][0] + small[1][0]) / 2
    assert _hit("asm_com_plug", (ox, face + 0.5, cz + 2.0), (0, -1, 0)) == pytest.approx(0.5, abs=1e-3)


def test_header_strips_sit_in_their_slots():
    p = params()
    hw = p["dk_hdr_w"] / 2
    zc = p["dk_hdr_z0"] + 1.0
    for s in (-1, 1):
        xc = s * p["dk_row_x"]
        for y in _strip_ys(p):
            for d in (-1, 1):
                t = _hit("asm_tray", (xc + d * hw, y, zc), (d, 0, 0))
                assert t == pytest.approx(p["board_clr"], abs=0.05), (s, y, d)
        y_back = p["dk_pin_y0"] - p["dk_pin_pitch"] / 2
        y_front = p["dk_pin_y1"] + p["dk_pin_pitch"] / 2
        for x in (xc - hw + 0.3, xc + hw - 0.3):
            assert _hit("asm_tray", (x, y_back, zc), (0, -1, 0)) == pytest.approx(p["board_clr"], abs=0.05)
            assert _hit("asm_tray", (x, y_front, zc), (0, 1, 0)) == pytest.approx(p["dkf_stop_clr"], abs=0.05)
            for y in _strip_ys(p)[::3]:
                assert _hit("asm_tray", (x, y, p["dk_hdr_z0"] + 0.01), (0, 0, -1)) == pytest.approx(0.01, abs=0.005)
        for k in range(0, int(p["dk_pins"]), 3):
            y = p["dk_pin_y0"] + k * p["dk_pin_pitch"]
            t = _hit("asm_tray", (xc, y, p["dk_pin_bot_z"] - 0.001), (0, 0, -1))
            assert t == pytest.approx(p["tray_pin_gap"], abs=0.01), (s, k)
            for d in (-1, 1):
                for e in (-1, 1):
                    q = (xc + d * (p["dk_pin_sq"] / 2 + p["board_clr"]), y + e * p["board_clr"], 2.0)
                    assert not solid_at("asm_tray", q), (s, k, d, e)
    assert p["tray_rail_top_z"] < p["dk_z0"]


def test_board_cannot_lift():
    p = params()
    top = p["dk_z1"] + 0.001
    edge = p["dk_w"] / 2
    for s in (-1, 1):
        for y in _latch_ys(p):
            for dy in (-p["dks_latch_w"] / 2 + 0.3, 0, p["dks_latch_w"] / 2 - 0.3):
                t = _hit("asm_tray", (s * (edge - 0.05), y + dy, top), (0, 0, 1))
                assert t is not None and t <= 0.5, (s, y, dy, t)
        for x in np.linspace(p["dkf_latch_x0"] + 0.3, p["dkf_latch_x1"] - 0.3, 4):
            t = _hit("asm_tray", (s * x, p["dk_y1"] - 0.05, top), (0, 0, 1))
            assert t is not None and t <= 0.5, (s, x, t)
    assert p["dks_latch_strain"] <= 0.016
    assert p["dkf_latch_strain"] <= 0.014
    assert p["latch_root_fillet"] >= 0.6
    assert p["dkf_latch_x0"] > p["dk_mod_w"] / 2 and p["dkf_latch_x0"] > p["dk_ant_w"] / 2
    assert p["dkf_latch_x1"] <= p["dk_w"] / 2


def test_latches_stand_free_on_their_whole_height():
    p = params()
    xm = (p["dkf_latch_x0"] + p["dkf_latch_x1"]) / 2
    face = p["dk_y1"] + p["dkf_latch_c"]
    behind = face + p["dkf_latch_t"] + 0.1
    top = p["tray_rail_top_z"] - 0.1
    for s in (-1, 1):
        for z in np.linspace(p["tray_latch_root_z"] + p["dkf_latch_fillet"] + 0.1, top, 5):
            for y in (p["tray_rail_y1"] + 0.05, face - 0.05, behind):
                assert not solid_at("asm_tray", (s * xm, y, z), 0.03), (s, y, z)
            for x in (p["dkf_latch_x0"] - p["latch_side_clr"] / 2, p["dkf_latch_x1"] + p["latch_side_clr"] / 2):
                assert not solid_at("asm_tray", (s * x, face + p["dkf_latch_t"] / 2, z), 0.03), (s, x, z)
        assert solid_at("asm_tray", (s * xm, face + p["dkf_latch_t"] / 2, top))
    assert face - p["tray_rail_y1"] >= p["latch_side_clr"] - 1e-9 and p["latch_side_clr"] >= 0.4
    hl = p["amp_l"] / 2
    for sx in (-1, 1):
        lx = p["amp_latch_lx"] + sx * (p["amp_latch_w"] / 2 + p["latch_side_clr"] / 2)
        for z in (2.0, 4.0, p["tray_amp_stop_top_z"] - 0.1):
            q = _amp_world(p, lx, hl + p["amp_latch_c"] + p["amp_latch_t"] / 2, z)
            assert not solid_at("asm_tray", q, 0.03), (sx, z)


def test_usb_push_lands_on_the_header_stops_not_on_the_end_latches():
    p = params()
    hw = p["dk_hdr_w"] / 2
    y_front = p["dk_pin_y1"] + p["dk_pin_pitch"] / 2
    zc = p["dk_hdr_z0"] + 1.0
    assert p["dkf_stop_clr"] <= 0.1
    assert p["dkf_latch_c"] >= 0.35 and p["dkf_latch_c"] >= p["dkf_stop_clr"] + 0.25
    for s in (-1, 1):
        xc = s * p["dk_row_x"]
        for x in np.linspace(xc - hw + 0.2, xc + hw - 0.2, 5):
            t = _hit("asm_tray", (x, y_front, zc), (0, 1, 0))
            assert t == pytest.approx(p["dkf_stop_clr"], abs=0.02), (s, x)
        for x in np.linspace(p["dkf_latch_x0"] + 0.3, p["dkf_latch_x1"] - 0.3, 4):
            for z in (p["dk_z0"] + 0.2, (p["dk_z0"] + p["dk_z1"]) / 2):
                t = _hit("asm_tray", (s * x, p["dk_y1"], z), (0, 1, 0))
                assert t == pytest.approx(p["dkf_latch_c"], abs=0.02), (s, x, z)
            t = _hit("asm_tray", (s * x, p["dk_y1"], p["dk_z1"] - 0.001), (0, 1, 0))
            assert t == pytest.approx(p["dkf_hook_touch"], abs=0.02), (s, x)
            assert t >= p["dkf_stop_clr"] + 0.05
        y0 = p["tray_slot_y1"] + 0.05
        y1 = p["tray_rail_y1"] - 0.05
        assert y1 - y0 >= 0.7
        for x in np.linspace(xc - hw, xc + hw, 5):
            for y in np.linspace(y0, y1, 3):
                for z in np.linspace(p["tray_floor_t"] + 0.1, p["dk_z0"] - 0.6, 4):
                    assert solid_at("asm_tray", (x, y, z), 0.03), (s, x, y, z)
        face = p["dk_y1"] + p["dkf_latch_c"]
        for x in np.linspace(xc - hw, xc + hw, 5):
            if not p["dkf_latch_x0"] - p["latch_side_clr"] < abs(x) < p["dkf_latch_x1"] + p["latch_side_clr"]:
                continue
            for z in np.linspace(p["tray_latch_root_z"] + p["dkf_latch_fillet"] + 0.1, p["tray_rail_top_z"] - 0.1, 4):
                assert not solid_at("asm_tray", (x, (p["tray_rail_y1"] + face) / 2, z), 0.03), (s, x, z)
        v = _box_volume("asm_tray", xc - hw, xc + hw, p["tray_slot_y1"], p["tray_rail_y1"], p["tray_floor_t"], p["dk_hdr_z0"])
        assert v == pytest.approx(2 * hw * p["dkf_stop_t"] * (p["dk_hdr_z0"] - p["tray_floor_t"]), rel=0.02)


def _shifted_devkit(dy):
    m = _manifold(mesh("asm_devkit")).translate([0.0, float(dy), 0.0])
    return float((m ^ _solid("asm_tray")).volume())


def test_board_travel_is_stopped_by_rigid_plastic_both_ways():
    p = params()
    stop = p["dkf_stop_clr"]
    assert _shifted_devkit(stop - 0.02) < TOL
    assert _shifted_devkit(stop + 0.05) > TOL
    back = p["dk_back_travel"]
    assert back <= p["board_clr"]
    assert _shifted_devkit(-(back - 0.02)) < TOL
    assert _shifted_devkit(-(back + 0.05)) > TOL
    assert p["dkf_latch_ov"] - back >= 0.25
    top = p["dk_z1"] + 0.001
    for s in (-1, 1):
        for x in np.linspace(p["dkf_latch_x0"] + 0.3, p["dkf_latch_x1"] - 0.3, 4):
            t = _hit("asm_tray", (s * x, p["dk_y1"] - back - 0.05, top), (0, 0, 1))
            assert t is not None and t <= 0.6, (s, x, t)


def test_end_latch_strain_is_bounded_by_the_side_latches():
    p = params()
    assert p["dkf_latch_strain"] <= p["dks_latch_strain"]
    assert p["dkf_latch_strain"] <= 0.014
    assert p["dkf_latch_t"] >= 0.65 and p["dkf_latch_fillet"] >= 1.0
    zt = p["dk_z1"] + p["dkf_latch_vg"] + p["latch_k"] * p["dkf_latch_ov"]
    span = zt - p["tray_latch_root_z"]
    want = 1.5 * p["dkf_latch_t"] * (p["dkf_latch_ov"] + p["dkf_latch_c"]) / span ** 2
    assert p["dkf_latch_strain"] == pytest.approx(want, rel=1e-4)


def _amp_world(p, lx, ly, z=None):
    a = np.radians(p["amp_rot"])
    x = p["amp_xy"][0] + lx * np.cos(a) - ly * np.sin(a)
    y = p["amp_xy"][1] + lx * np.sin(a) + ly * np.cos(a)
    return (x, y) if z is None else (x, y, z)


def _amp_dir(p, dx, dy):
    a = np.radians(p["amp_rot"])
    return (dx * np.cos(a) - dy * np.sin(a), dx * np.sin(a) + dy * np.cos(a), 0)


def test_amp_nest_clearance():
    p = params()
    hw, hl = p["amp_w"] / 2, p["amp_l"] / 2
    z = p["amp_z0"] + p["amp_t"] / 2
    for ly in np.linspace(-hl + 1, hl - 1, 5):
        for d in (-1, 1):
            t = _hit("asm_tray", _amp_world(p, d * hw, ly, z), _amp_dir(p, d, 0))
            assert t == pytest.approx(p["amp_clr"], abs=0.05), (ly, d)
    latch = (p["amp_latch_lx"] - p["amp_latch_w"] / 2 - 0.3, p["amp_latch_lx"] + p["amp_latch_w"] / 2 + 0.3)
    lip = (p["amp_lip_lx"] - p["amp_lip_w"] / 2 - 0.1, p["amp_lip_lx"] + p["amp_lip_w"] / 2 + 0.1)
    for lx in np.linspace(-hw + 1, hw - 1, 5):
        for d in (-1, 1):
            t = _hit("asm_tray", _amp_world(p, lx, d * hl, z), _amp_dir(p, 0, d))
            span = latch if d > 0 else lip
            if span[0] < lx < span[1]:
                want = p["amp_latch_c"] if d > 0 else p["amp_lip_gap"]
            elif span[0] - 0.4 < lx < span[1] + 0.4:
                continue
            else:
                want = p["amp_clr"]
            assert t == pytest.approx(want, abs=0.05), (lx, d)
    for lx in (-hw + 1, 0, p["tray_amp_pad_lx1"] - 0.3, hw - 0.3):
        for ly in (-hl + 0.5, hl - 0.5):
            t = _hit("asm_tray", _amp_world(p, lx, ly, p["amp_z0"] + 0.01), (0, 0, -1))
            assert t == pytest.approx(0.01, abs=0.005), (lx, ly)
    tails = p["amp_z0"] - p["amp_hdr_tail"]
    for x, y in p["amp_hdr_xy"]:
        t = _hit("asm_tray", (x, y, tails - 0.001), (0, 0, -1))
        assert t is not None and t >= 0.3, (x, y)
        for d in (-1, 1):
            t = _hit("asm_tray", (x, y, tails + 0.5), _amp_dir(p, d, 0))
            t = None if t is None else t - p["amp_hdr_pin_d"] / 2
            assert t is None or t >= 0.4, (x, y, d)


def test_amp_cannot_lift():
    p = params()
    hl = p["amp_l"] / 2
    top = p["amp_z0"] + p["amp_t"] + 0.001
    shift_in, shift_out = p["amp_lip_gap"], p["amp_latch_c"]
    for lx in np.linspace(p["amp_latch_lx"] - p["amp_latch_w"] / 2 + 0.3, p["amp_latch_lx"] + p["amp_latch_w"] / 2 - 0.3, 4):
        t = _hit("asm_tray", _amp_world(p, lx, hl - shift_in - 0.05, top), (0, 0, 1))
        assert t is not None and t <= 0.5, ("latch", lx, t)
    for lx in np.linspace(p["amp_lip_lx"] - p["amp_lip_w"] / 2 + 0.3, p["amp_lip_lx"] + p["amp_lip_w"] / 2 - 0.3, 4):
        t = _hit("asm_tray", _amp_world(p, lx, -hl + shift_out + 0.05, top), (0, 0, 1))
        assert t is not None and t <= 0.2, ("lip", lx, t)
    for lx in (p["amp_latch_lx"] - 1, p["amp_latch_lx"] + 1):
        q = _amp_world(p, lx, hl + p["amp_latch_c"] + p["amp_latch_t"] + 0.3, p["tray_latch_root_z"] + 1)
        assert not solid_at("asm_tray", q), lx
    assert p["amp_latch_strain"] <= 0.025
    hx = p["amp_w"] / 2 - p["amp_hdr_in"] - p["amp_hdr_w"]
    assert p["amp_latch_lx"] + p["amp_latch_w"] / 2 < hx and p["amp_lip_lx"] + p["amp_lip_w"] / 2 < hx


def test_amp_header_faces_the_capacitor_and_the_cap_lies_flat_beside_it():
    p = params()
    hx, hy = np.array(p["amp_hdr_xy"]).T
    assert np.allclose(hy, hy[0])
    assert hy[0] < p["amp_xy"][1]
    cap = mesh("asm_cap")
    assert cap.bounds[0][2] == pytest.approx(p["tray_floor_t"], abs=0.01)
    assert cap.extents[0] == pytest.approx(p["cap_l"], abs=0.05)
    assert cap.bounds[1][1] < hy[0] - p["amp_hdr_w"]
    assert cap.bounds[1][0] >= hx.max() - 1.0
    assert overlap("asm_cap", "asm_tray") < 1e-3
    assert overlap("asm_cap", "asm_amp") < 1e-3


def test_tray_screws_from_below():
    p = params()
    for part in ("asm_tray", "asm_feet", "asm_body", "asm_tray_gasket"):
        assert overlap("asm_tray_screw_probe", part) < TOL, part
    b = mesh("asm_tray_screw_probe").bounds
    assert b[0][2] == pytest.approx(-p["foot_out"], abs=1e-3)
    assert b[1][2] == pytest.approx(p["tray_screw_head_z"], abs=0.02)
    s = mesh("asm_tray_screws")
    assert len(_manifold(s).decompose()) == len(p["tray_screw_xy"])
    for x, y in p["tray_screw_xy"]:
        t = _hit("asm_tray", (x + p["screw_head_d"] / 2 - 0.05, y, p["tray_screw_head_z"] + 0.45), (0, 0, 1))
        assert t is not None and t < 0.3
        assert not solid_at("asm_tray", (x, y, p["body_bot_z"] - 0.5))
        assert _hit("asm_body", (x, y, p["body_bot_z"] - 0.5), (0, 0, 1)) == pytest.approx(
            0.5 + p["tray_insert_hole_depth"] + p["tray_tip_h"], abs=0.05)
    assert p["tray_screw_tip_z"] + p["insert_tip_gap"] <= p["tray_insert_top_z"] + 1e-9
    assert p["tray_screw_thread"] >= p["min_thread"]
    assert p["tray_screw_head_z"] >= p["body_bot_z"] - p["screw_len"] + p["min_thread"]
    assert p["tray_access_d"] >= p["csk_d"] + 1.0


def test_transistor_area_is_empty_floor():
    p = params()
    cx, cy = p["transistor_xy"]
    zx, zy = p["tr_zone"]
    e = 0.05
    v = _box_volume("asm_tray", cx - zx / 2 + e, cx + zx / 2 - e, cy - zy / 2 + e, cy + zy / 2 - e,
                    p["tray_floor_t"] + e, p["body_bot_z"] - e)
    assert v < TOL
    for x in np.linspace(cx - zx / 2 + 0.3, cx + zx / 2 - 0.3, 5):
        for y in np.linspace(cy - zy / 2 + 0.3, cy + zy / 2 - 0.3, 5):
            assert solid_at("asm_tray", (x, y, p["tray_floor_t"] - 0.1)), (x, y)
            assert _hit("asm_tray", (x, y, p["tray_floor_t"] + 0.01), (0, 0, -1)) == pytest.approx(0.01, abs=0.005)
    assert zx >= 12 and zy >= 20


def test_tray_gasket_seat():
    p = params()
    assert 0.2 <= p["tray_gasket_squeeze"] / p["tray_gasket_t"] <= 0.3
    assert p["tray_groove_depth"] == pytest.approx(p["tray_gasket_t"] - p["tray_gasket_squeeze"])
    assert -0.25 <= p["tray_gasket_fit"] <= -0.15
    assert p["tray_gasket_od"] / 2 == pytest.approx(p["tray_groove_out_r"] + p["tray_gasket_fit"])
    assert p["tray_gasket_od"] / 2 - p["tray_gasket_id"] / 2 == pytest.approx(p["tray_gasket_w"])
    room = p["tray_groove_out_r"] - p["tray_groove_in_r"]
    assert room >= p["tray_gasket_w"] * p["tray_gasket_t"] / p["tray_gasket_c"] - 1e-9
    assert p["tray_gasket_id"] / 2 > p["tray_groove_in_r"]
    assert p["tray_groove_wall"] >= 0.5
    z = p["body_bot_z"] - p["tray_groove_depth"] / 2
    groove = _annulus(p["tray_groove_in_r"] + 0.02, p["tray_groove_out_r"] - 0.02)
    assert float((groove ^ _solid("asm_tray").slice(float(z))).area()) < 1e-3
    below = _solid("asm_tray").slice(float(p["body_bot_z"] - p["tray_groove_depth"] - 0.05))
    assert float((groove - below).area()) < 0.05
    g = mesh("asm_tray_gasket")
    assert g.is_watertight
    r = np.hypot(g.vertices[:, 0], g.vertices[:, 1])
    assert r.max() == pytest.approx(p["tray_groove_out_r"], abs=0.02)
    assert r.min() == pytest.approx(p["tray_groove_out_r"] - p["tray_gasket_w"], abs=0.02)
    assert g.bounds[1][2] == pytest.approx(p["body_bot_z"], abs=1e-4)
    assert g.extents[2] == pytest.approx(p["tray_gasket_c"], abs=1e-4)
    body = _solid("asm_body").slice(float(p["body_bot_z"] + 0.05))
    under = _annulus(p["tray_groove_out_r"] - p["tray_gasket_w"], p["tray_groove_out_r"])
    assert float((under - body).area()) < 1e-2


def test_feet_sit_in_niches_and_carry_the_case():
    p = params()
    feet = mesh("asm_feet")
    assert len(_manifold(feet).decompose()) == len(p["foot_xy"]) == 4
    assert feet.bounds[0][2] == pytest.approx(-p["foot_out"], abs=1e-4)
    assert feet.bounds[1][2] == pytest.approx(p["foot_niche_h"], abs=1e-4)
    assert 1.5 <= p["foot_out"] <= 2.5
    assert p["foot_niche_h"] > 1
    for x, y in p["foot_xy"]:
        assert not solid_at("asm_tray", (x, y, p["foot_niche_h"] - 0.1))
        t = _hit("asm_tray", (x, y, p["foot_niche_h"] - 0.01), (0, 0, 1))
        assert t == pytest.approx(0.01, abs=0.005)
        assert solid_at("asm_tray", (x, y, p["foot_niche_h"] + p["foot_niche_skin"] - 0.1))
        assert np.hypot(x, y) + p["foot_l"] / 2 + p["foot_niche_clr"] <= p["case_r"] - 1.0
    pts = np.asarray(p["foot_xy"], dtype=float)
    ang = np.arctan2(pts[:, 1], pts[:, 0])
    pts = pts[np.argsort(ang)]
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        cross = a[0] * b[1] - a[1] * b[0]
        assert cross > 0
        assert cross / np.linalg.norm(b - a) >= 15


def test_wire_paths_reach_the_devkit_pins():
    p = params()
    for part in ("asm_tray", "asm_body", "asm_amp", "asm_cap", "asm_devkit", "asm_usb_plug", "asm_speaker",
                 "asm_inserts", "asm_tray_screws"):
        assert overlap("asm_tray_wire_probe", part) < TOL, part
    for q in p["tray_wire_pts"][1:]:
        assert abs(abs(q[0]) - p["dk_row_x"]) < 1e-6
        assert p["dk_pin_y0"] <= q[1] <= p["dk_pin_y1"]
        assert q[2] - p["wire_probe_d"] / 2 - p["dk_pin_top_z"] < 0.5
    start = np.asarray(p["tray_wire_pts"][0])
    end = np.asarray(p["wire_btn_pts"][-1])
    assert np.linalg.norm(start - end) < 1e-6


def test_tray_overhangs():
    assert overhang_area(mesh("tray")) < TRAY_OVERHANG_MAX


def test_build_lists_tray():
    text = (CASE_DIR / "build.sh").read_text()
    assert '"tray:PETG-black_tray"' in text


def _foot_outline(p, grow, x, y, a):
    r = p["foot_corner_r"]
    core = manifold3d.CrossSection.square([p["foot_l"] - 2 * r, p["foot_w"] - 2 * r], True)
    return core.offset(r + grow, manifold3d.JoinType.Round, 2.0, 96).rotate(a).translate([x, y])


def test_foot_niche_ceiling_bridges_from_a_solid_band():
    p = params()
    band = 2.0
    assert p["foot_pad_grow"] >= band
    for z in (p["tray_floor_t"] + 0.05, p["foot_niche_h"] - 0.05):
        cut = _solid("tray").slice(float(z))
        for (x, y), a in zip(p["foot_xy"], p["foot_rot"]):
            ring = _foot_outline(p, p["foot_niche_clr"] + band, x, y, a) - _foot_outline(p, p["foot_niche_clr"], x, y, a)
            assert float((ring - cut).area()) < 0.02, (x, y, z)
