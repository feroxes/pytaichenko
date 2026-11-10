import itertools

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, mesh, overlap, params

FASTENERS = ["tray_screws", "top_screws", "speaker_screws", "metal_washers", "devkit_screws", "feet"]

DUMMIES = [
    "devkit",
    "usb_plug",
    "speaker",
    "amp",
    "cap",
    "mic",
    "button",
    "filament",
]


def test_ring_geometry():
    p = params()
    wall_at_lip = p["bowl_c"][0] + np.sqrt(p["bowl_rad"] ** 2 - p["ring_lip_h"] ** 2)
    assert p["bowl_c"][1] == pytest.approx(p["ring_z0"], abs=1e-4)
    assert p["ring_outer_r"] - p["bowl_c"][0] == pytest.approx(p["bowl_rad"], abs=1e-3)
    assert p["ring_in_r"] == pytest.approx(
        wall_at_lip - p["wall_t"] - p["clr"] - p["ring_lip_w"], abs=1e-3)
    assert p["ring_in_r"] >= p["spk_flange_max_r"] + p["ring_in_clr"]
    assert p["ring_in_r"] >= p["spk_seat_max_r"] + p["ring_in_clr"]
    assert p["ring_lip_in_r"] == pytest.approx(p["ring_in_r"], abs=0.01)
    assert p["filament_r"] == pytest.approx(p["ring_outer_r"] - p["ring_front_t"] - p["ring_pocket_r"], abs=1e-4)
    reach = np.hypot(p["button_r"] + p["btn_in"], p["btn_in"])
    assert p["ring_outer_r"] == pytest.approx(reach + p["btn_wall"], abs=1e-3)
    assert p["ring_outer_r"] * 2 <= 108
    assert p["ring_arc_len"] == pytest.approx(np.pi * p["filament_r"] - p["bridge_w"], abs=1e-3)
    assert p["ring_arc_len"] < p["fil_lit"]


METAL = ["speaker", "speaker_screws", "metal_washers", "top_screws", "tray_screws", "amp", "cap"]


def test_antenna_clear_of_metal():
    p = params()
    assert p["antenna_metal_gap"] >= 10
    ant = _manifold(mesh("asm_antenna"))
    zone = ant.minkowski_sum(manifold3d.Manifold.sphere(10.0, 48))
    for name in METAL:
        assert float((zone ^ _manifold(mesh(f"asm_{name}"))).volume()) < 1e-3, name
    b = mesh("asm_antenna").bounds
    assert b[1][2] == pytest.approx(p["antenna_top_z"], abs=1e-6)
    assert b[0][1] == pytest.approx(p["dk_y0"], abs=1e-3)
    assert b[1][1] - b[0][1] == pytest.approx(p["ant_len"], abs=1e-6)
    assert mesh("asm_speaker").bounds[0][2] - p["antenna_top_z"] == pytest.approx(p["antenna_metal_gap"], abs=1e-4)


def test_wiring_keepout_under_speaker():
    p = params()
    assert p["wire_clear_h"] == pytest.approx(5.0)
    keep = mesh("asm_wire_keepout")
    assert keep.is_watertight
    assert keep.bounds[1][2] == pytest.approx(p["dk_pin_top_z"] + p["wire_clear_h"], abs=1e-6)
    assert keep.bounds[0][2] == pytest.approx(min(p["dk_pin_top_z"], p["amp_pin_top_z"]), abs=1e-6)
    assert p["dk_pin_top_z"] == pytest.approx(mesh("asm_devkit").bounds[1][2], abs=1e-6)
    assert p["amp_pin_top_z"] == pytest.approx(p["amp_z0"] + p["amp_t"] + p["amp_hdr_h"] + p["amp_hdr_pin"], abs=1e-6)
    under = keep.vertices[np.hypot(keep.vertices[:, 0], keep.vertices[:, 1]) < p["spk_magnet_d"] / 2]
    assert len(under) > 0
    for name in ("speaker", "metal_washers", "speaker_screws", "body", "top", "ring"):
        assert overlap("asm_wire_keepout", f"asm_{name}") < 1e-3, name


def test_total_height_minimal():
    p = params()
    assert p["total_h"] <= 78
    assert p["total_h"] == pytest.approx(mesh("asm_top").bounds[1][2] - mesh("asm_feet").bounds[0][2], abs=1e-4)
    assert p["spk_low_z"] == pytest.approx(p["dk_pin_top_z"] + p["wire_clear_h"], abs=1e-6)
    keep = _manifold(mesh("asm_wire_keepout"))
    speaker = _manifold(mesh("asm_speaker"))
    assert float((keep ^ speaker).volume()) < 1e-3
    assert float((keep.translate([0, 0, 0.5]) ^ speaker).volume()) > 1.0


def test_cap_fit():
    p = params()
    c = mesh("asm_cap")
    assert c.extents[2] == pytest.approx(p["cap_d"], abs=0.01)
    assert c.bounds[0][2] == pytest.approx(p["tray_floor_t"], abs=1e-6)
    axis = np.array([-np.sin(np.radians(p["cap_az"])), np.cos(np.radians(p["cap_az"]))])
    along = c.vertices[:, :2] @ axis
    assert along.max() - along.min() == pytest.approx(p["cap_l"], abs=0.01)
    r = np.hypot(c.vertices[:, 0], c.vertices[:, 1])
    assert r.min() >= p["spk_magnet_d"] / 2 + 1.0
    opening = p["inner_r_profile"][0] - p["tray_gap_min"]
    v = mesh("asm_cap").vertices
    assert np.hypot(v[:, 0], v[:, 1]).max() < opening
    centre = np.array([p["cap_r"] * np.cos(np.radians(p["cap_az"])), p["cap_r"] * np.sin(np.radians(p["cap_az"]))])
    ends = [centre + s * p["cap_l"] / 2 * axis for s in (-1, 1)]
    pin_x = p["amp_x"] - p["amp_w"] / 2 + p["amp_hdr_in"] + p["amp_hdr_w"] / 2
    half = (p["amp_hdr_n"] - 1) / 2 * p["amp_hdr_pitch"]
    pins = [np.array([pin_x, p["amp_y"] + s * half]) for s in (-1, 1)]
    assert min(np.linalg.norm(e - q) for e in ends for q in pins) <= 20.0
    for name in ("tray", "body", "speaker", "devkit", "amp", "mic", "tray_screws", "filament", "wire_keepout", "feet"):
        assert overlap("asm_cap", f"asm_{name}") < 1e-3, name


def test_insert_bosses():
    p = params()
    assert p["boss_wall"] >= 1.6
    assert p["boss_skin"] >= 1.6


@pytest.mark.parametrize("a,b", list(itertools.combinations(DUMMIES, 2)))
def test_dummies_do_not_collide(a, b):
    assert overlap(f"asm_{a}", f"asm_{b}") < 1e-3


@pytest.mark.parametrize("name", DUMMIES)
def test_dummies_watertight(name):
    m = mesh(f"asm_{name}")
    assert m.is_watertight
    assert m.volume > 0


@pytest.mark.parametrize(
    "name",
    [
        "tray_screws",
        "top_screws",
        "speaker_screws",
        "metal_washers",
        "devkit_screws",
        "top_plugs",
        "feet",
        "mic_port_probe",
        "filament_channel_probe",
        "antenna",
        "wire_keepout",
    ],
)
def test_fasteners_and_probes_watertight(name):
    m = mesh(f"asm_{name}")
    assert m.is_watertight
    assert m.volume > 0


@pytest.mark.parametrize("a,b", list(itertools.combinations(DUMMIES + FASTENERS, 2)))
def test_dummies_and_fasteners_do_not_collide(a, b):
    assert overlap(f"asm_{a}", f"asm_{b}") < 1e-3


def test_insert_depths_and_hard_stop():
    p = params()
    assert p["tray_pad_t"] == pytest.approx(p["tray_floor_t"] + p["tray_pad_gap"])
    assert p["tray_screw_tip_z"] + p["tray_pad_gap"] + p["insert_tip_gap"] <= (
        p["tray_pad_t"] + p["tray_boss_hole_depth"] + 1e-6
    )
    assert p["spk_screw_tip_z"] + p["insert_tip_gap"] <= p["spk_insert_top_z"] + 1e-6
    assert p["top_screw_tip_z"] - p["insert_tip_gap"] >= p["top_insert_bot_z"] - 1e-6
    assert p["dk_screw_tip_z"] - p["insert_tip_gap"] >= p["dk_z0"] - p["dk_insert_hole_depth"] - 1e-6
    for hole in ("tray_boss_hole_depth", "top_insert_hole_depth", "spk_insert_hole_depth", "dk_insert_hole_depth"):
        assert p[hole] >= p["insert_len"] + 1 - 1e-9


def test_screws_are_m25_countersunk_with_the_kit_inserts():
    p = params()
    assert p["screw_len"] == 8 and p["screw_d"] == 2.5
    assert p["screw_head_d"] == 4.7 and p["screw_head_h"] == 1.5
    assert p["screw_rim_h"] == pytest.approx(0.4)
    assert p["insert_len"] == 4 and p["insert_od"] == 3.5
    assert 3.2 <= p["insert_d"] <= 3.4
    assert p["screw_thru_d"] >= p["screw_d"] + 0.3
    assert p["csk_d"] == pytest.approx(p["screw_head_d"] + 2 * p["csk_clr"])
    assert p["csk_h"] == pytest.approx(p["csk_bore_h"] + (p["csk_d"] - p["screw_thru_d"]) / 2)
    assert 0 <= p["csk_head_z"] <= p["csk_clr"]


def test_thread_engagement_in_every_insert():
    p = params()
    for key in ("tray_screw_thread", "top_screw_thread", "spk_screw_thread", "dk_screw_thread"):
        assert p[key] >= p["min_thread"]
        assert p[key] <= p["insert_len"] + 1e-9
    assert p["tray_screw_engage"] >= p["min_thread"]
    assert p["boss_wall"] >= 1.6
    assert p["boss_skin"] >= 1.6
    assert p["top_seat_floor_t"] >= 1.6
    for key in ("top_screw_engage", "spk_screw_engage", "dk_screw_engage"):
        assert p[key] >= p["insert_len"], key


def test_countersunk_head_stands_on_a_plain_hole_by_rim_plus_cone():
    p = params()
    for d, key in ((p["washer_id"], "csk_stand_washer"), (p["dk_hole_d"], "csk_stand_dk_hole")):
        assert p[key] == pytest.approx(p["screw_rim_h"] + (p["screw_head_d"] - d) / 2, abs=1e-9)
        assert p["screw_rim_h"] < p[key] < p["screw_head_h"]
    assert p["csk_stand_washer"] == pytest.approx(1.4)
    assert p["csk_stand_dk_hole"] == pytest.approx(1.25)
