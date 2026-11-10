import itertools

import numpy as np
import pytest

from conftest import mesh, overlap, params

DUMMIES = [
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
    "transistor",
]


def test_measured_part_sizes():
    p = params()
    expect = {
        "case_d": 84, "wall_t": 3.0, "wall_thin_t": 1.5,
        "spk_flange_d": 50.3, "spk_open_d": 46, "spk_rim_w": 1.75, "spk_rim_h": 1.3,
        "spk_ear_tip": 68, "spk_ear_pitch": 60, "spk_ear_hole_d": 4, "spk_depth": 35.3,
        "spk_basket_d": 45.4, "spk_magnet_d": 44.65, "spk_cap_rise": 2, "spk_grille_gap": 4,
        "spk_term_z": 14, "spk_term_out": 4,
        "dk_len": 63.2, "dk_ant_out": 6, "dk_ant_w": 17.9, "dk_ant_t": 0.8, "dk_w": 28.2, "dk_t": 1.6,
        "dk_row_pitch": 25.4, "dk_pins": 22, "dk_pin_pitch": 2.54, "dk_pin1_from_usb": 1.5,
        "dk_hdr_h": 3, "dk_pin_short": 2.5, "dk_pin_long": 4,
        "dk_usb_w": 8.88, "dk_usb_h": 3.1, "dk_usb_pitch": 11.5, "dk_usb_out": 0.5,
        "wire_clear_h": 5, "cap_d": 10, "cap_l": 17, "mic_d": 13.8, "mic_port_d": 1.8,
        "fil_lit": 175, "fil_contact": 5, "fil_d": 1.5, "mic_angle": 55,
        "usb_plug_w": 12, "usb_plug_h": 7, "usb_plug_len": 14,
        "usb_shell_w": 8.4, "usb_shell_h": 2.6, "usb_shell_l": 6.65, "usb_plug_seat": 0.7,
        "amp_w": 18.7, "amp_l": 17.7, "amp_total_h": 10.4, "btn_w": 11.9, "btn_h": 5.7, "btn_body_h": 3.0,
    }
    for k, v in expect.items():
        assert p[k] == pytest.approx(v), k
    assert p["spk_ear_standoff_h"] == pytest.approx(p["spk_rim_h"] + p["spk_gasket_c"])
    assert p["dk_com_x"] > 0


def test_speaker_ears_fit():
    p = params()
    assert p["spk_ear_tip"] / 2 + p["clr"] <= p["case_d"] / 2 - p["wall_t"]
    b = mesh("asm_speaker").bounds
    assert b[1][0] - b[0][0] == pytest.approx(p["spk_ear_tip"], abs=0.01)
    assert b[1][1] - b[0][1] < p["spk_ear_tip"] - 10
    assert b[0][2] == pytest.approx(p["spk_mag_z"], abs=1e-4)
    assert b[1][2] == pytest.approx(p["spk_rim_top_z"] + p["spk_cap_rise"], abs=1e-4)


def test_button_pocket_fits_behind_flange():
    p = params()
    btn_out = p["btn_w"] / 2 + p["btn_pocket_clr"] + p["btn_wall"]
    assert p["btn_out"] == pytest.approx(btn_out)
    assert p["button_r"] - btn_out >= p["spk_flange_d"] / 2 + 0.5 - 1e-9
    assert np.hypot(p["button_r"] + btn_out, btn_out) <= p["case_d"] / 2 - 1.5 + 1e-6
    b = mesh("asm_button").bounds
    assert (b[0][1] + b[1][1]) / 2 == pytest.approx(-p["button_r"], abs=1e-3)
    assert (b[0][0] + b[1][0]) / 2 == pytest.approx(0, abs=1e-3)


def test_devkit_fits():
    p = params()
    limit = p["case_d"] / 2 - p["wall_t"] - p["clr"]
    y0 = p["dk_y0"]
    y1 = y0 + p["dk_len"] - p["dk_ant_out"]
    corners = [
        (p["dk_w"] / 2, y0), (-p["dk_w"] / 2, y0),
        (p["dk_w"] / 2, y1), (-p["dk_w"] / 2, y1),
        (p["dk_ant_w"] / 2, y1 + p["dk_ant_out"]), (-p["dk_ant_w"] / 2, y1 + p["dk_ant_out"]),
    ]
    for x, y in corners:
        assert np.hypot(x, y) <= limit + 1e-6, (x, y)
    assert p["usb_face_y"] == pytest.approx(y0 - p["dk_usb_out"])
    assert p["case_d"] / 2 + p["usb_face_y"] <= 5.4
    dk = mesh("asm_devkit").bounds
    assert dk[0][1] == pytest.approx(p["usb_face_y"], abs=1e-4)
    assert dk[1][0] - dk[0][0] == pytest.approx(p["dk_w"], abs=1e-4)
    ant = mesh("asm_antenna").bounds
    assert ant[1][1] == pytest.approx(y1 + p["dk_ant_out"], abs=1e-4)
    assert ant[1][0] - ant[0][0] == pytest.approx(p["dk_ant_w"], abs=1e-4)
    plug = mesh("asm_usb_plug").bounds
    assert (plug[0][0] + plug[1][0]) / 2 == pytest.approx(p["dk_com_x"], abs=1e-4)
    assert plug[1][1] == pytest.approx(p["usb_face_y"] - p["usb_plug_seat"] + p["usb_shell_l"], abs=1e-4)


def test_mic_spacing():
    p = params()
    a = np.radians(p["mic_angle"])
    holes = [np.array([s * p["case_d"] / 2 * np.sin(a), p["case_d"] / 2 * np.cos(a)]) for s in (-1, 1)]
    assert np.linalg.norm(holes[0] - holes[1]) >= 60
    centres = []
    for name in ("mic_l", "mic_r"):
        m = mesh(f"asm_{name}")
        r = np.hypot(m.vertices[:, 0], m.vertices[:, 1])
        assert r.max() <= np.hypot(p["mic_face_r"], p["mic_d"] / 2) + 1e-3, name
        assert p["case_d"] / 2 - p["mic_face_r"] >= 2.4
        centres.append(m.bounds.mean(axis=0))
    assert np.linalg.norm(centres[0][:2] - centres[1][:2]) >= 60
    rb = p["mic_board_r"]
    assert 2 * rb * np.sin(a) >= 60
    assert centres[0][0] < 0 < centres[1][0]
    assert centres[1][2] == pytest.approx(p["mic_z"], abs=0.05)


def test_height_range():
    p = params()
    stack = (p["tray_skin_t"] + p["dk_pin_short"] + p["dk_hdr_h"] + p["dk_t"] + p["dk_pin_long"]
             + p["wire_clear_h"] + p["spk_mag_gap"] + p["spk_depth"] + p["spk_grille_gap"] + p["grille_t"])
    assert p["H"] == pytest.approx(stack)
    assert 55 <= p["H"] <= 62


def test_vertical_stack():
    p = params()
    assert p["spk_mag_z"] >= p["dk_z1"] + p["dk_pin_long"] + p["wire_clear_h"] - 1e-9
    assert mesh("asm_speaker").bounds[0][2] == pytest.approx(p["spk_mag_z"], abs=1e-4)
    assert mesh("asm_devkit").bounds[1][2] == pytest.approx(p["dk_z1"] + p["dk_pin_long"], abs=1e-4)
    amp = mesh("asm_amp")
    assert amp.bounds[1][2] == pytest.approx(p["amp_top_z"], abs=1e-4)
    r = np.hypot(amp.vertices[:, 0], amp.vertices[:, 1])
    assert r.min() < p["spk_magnet_d"] / 2
    assert p["amp_top_z"] + p["wire_clear_h"] <= p["spk_mag_z"] + 1e-9
    assert p["spk_mag_z"] - (p["dk_z1"] + p["dk_ant_t"]) >= 10


def test_ring_fits():
    p = params()
    circ = 2 * np.pi * p["filament_r"]
    assert p["fil_lit"] < circ
    half = np.arctan2(p["btn_out"], p["button_r"] - p["btn_out"])
    pocket_arc = 2 * half * p["filament_r"]
    assert circ - p["fil_lit"] >= pocket_arc
    f = mesh("asm_filament")
    ang = np.degrees(np.arctan2(f.vertices[:, 1], f.vertices[:, 0]))
    gap_half = 180 * (circ - p["fil_lit"]) / circ
    assert np.all(np.abs(((ang + 90 + 180) % 360) - 180) >= gap_half - 1.0)
    assert f.bounds[1][1] == pytest.approx(p["filament_r"] + p["fil_d"] / 2, abs=0.01)
    assert (f.bounds[0][0] + f.bounds[1][0]) / 2 == pytest.approx(0, abs=0.01)


def test_transistor_zone():
    p = params()
    zx, zy = p["tr_zone"]
    cx, cy = p["transistor_xy"]
    limit = p["case_d"] / 2 - p["wall_t"] - p["clr"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            assert np.hypot(cx + sx * zx / 2, cy + sy * zy / 2) <= limit
    assert zx >= 12 and zy >= 20
    assert cx + zx / 2 <= -(p["dk_w"] / 2 + p["board_clr"])
    b = mesh("asm_transistor").bounds
    assert b[0][0] >= cx - zx / 2 - 1e-6 and b[1][0] <= cx + zx / 2 + 1e-6
    assert b[0][1] >= cy - zy / 2 - 1e-6 and b[1][1] <= cy + zy / 2 + 1e-6
    assert b[0][2] == pytest.approx(p["tray_floor_t"], abs=1e-6)


@pytest.mark.parametrize("name", DUMMIES)
def test_dummies_watertight(name):
    m = mesh(f"asm_{name}")
    assert m.is_watertight
    assert m.volume > 0


@pytest.mark.parametrize("a,b", list(itertools.combinations(DUMMIES, 2)))
def test_dummies_do_not_overlap(a, b):
    assert overlap(f"asm_{a}", f"asm_{b}") < 1e-3
