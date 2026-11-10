import functools
import importlib
import math
import subprocess
import sys

import manifold3d
import pytest

from conftest import CASE_DIR, _manifold, mesh, params

sys.path.insert(0, str(CASE_DIR / "tools"))
wiring = importlib.import_module("wiring")

FIRMWARE = {
    "MIC_BCLK": 5,
    "MIC_WS": 6,
    "MIC_DIN": 7,
    "SPK_BCLK": 15,
    "SPK_LRC": 16,
    "SPK_DOUT": 17,
    "RING_GPIO": 18,
    "BUTTON_TOP_GPIO": 8,
}

GPIO_ENDS = {
    "GPIO5": {"mic L SCK", "mic R SCK"},
    "GPIO6": {"mic L WS", "mic R WS"},
    "GPIO7": {"mic L SD", "mic R SD"},
    "GPIO15": {"amp BCLK"},
    "GPIO16": {"amp LRC"},
    "GPIO17": {"amp DIN"},
    "GPIO18": {"transistor G (47 Ω soldered on it)"},
    "GPIO8": {"button leg A"},
}

OBSTACLES = ["asm_body", "asm_tray", "asm_top", "asm_speaker", "asm_inserts", "asm_cap", "asm_amp", "asm_devkit",
             "asm_transistor", "asm_mic_l", "asm_mic_r", "asm_button", "asm_ring_arc", "asm_filament_tails",
             "asm_tray_screws", "asm_top_screws", "asm_speaker_screws"]

TRIM = 3.0
TUBE_R = 0.5


@functools.lru_cache(maxsize=None)
def result():
    return wiring.compute(params(), wiring.load_firmware())


def devkit_label(end):
    return end.label.split(" ")[1]


def test_firmware_pins_are_the_ones_wired():
    gpio = wiring.load_firmware()
    for name, num in FIRMWARE.items():
        assert gpio[name] == num, name


def test_every_firmware_gpio_lands_where_expected():
    seen = {}
    for r in result().routed:
        for end, other in ((r.a, r.b), (r.b, r.a)):
            if end.part == "asm_devkit" and devkit_label(end).startswith("GPIO"):
                seen.setdefault(devkit_label(end), set()).add(other.label)
    assert seen == GPIO_ENDS
    p = params()
    for label in GPIO_ENDS:
        row, index = wiring.find_pin(label)
        assert row == 2
        assert wiring.ROW2.index(label) == index
        x, y, z = wiring.devkit_pin(p, row, index)
        assert y == pytest.approx(p["dk_pin_y0"] + (p["dk_pins"] - 1 - index) * p["dk_pin_pitch"])


def test_devkit_rows_match_the_measurements():
    p = params()
    assert len(wiring.ROW1) == len(wiring.ROW2) == p["dk_pins"]
    assert math.copysign(1, wiring.row_x(p, 1)) == math.copysign(1, p["dk_com_x"])
    assert wiring.row_x(p, 2) == pytest.approx(-wiring.row_x(p, 1))
    assert abs(wiring.row_x(p, 1)) == pytest.approx(p["dk_row_x"])
    assert p["usb_face_y"] < p["dk_y0"] < p["dk_y1"] < p["dk_ant_y1"]
    assert wiring.pin_y(p, 0) == pytest.approx(p["dk_pin_y1"])
    assert wiring.pin_y(p, p["dk_pins"] - 1) == pytest.approx(p["dk_pin_y0"])
    for label in ("3V3", "5Vin", "RST", "GPIO4", "GPIO14"):
        assert label in wiring.ROW2
    for label in ("TX", "RX", "GPIO0", "GPIO1"):
        assert label in wiring.ROW1
    assert wiring.ROW2[:3] == ["3V3", "3V3", "RST"]
    assert wiring.ROW2[-2:] == ["5Vin", "GND"]
    used_row1 = {index for row, index in wiring.POWER_PINS.values() if row == 1}
    assert used_row1 == {0, 20}
    assert all(wiring.ROW1[i] == "GND" for i in used_row1)


def test_power_pins_are_what_their_names_say():
    plan = wiring.POWER_PINS
    labels = {k: wiring.ROWS[row][index] for k, (row, index) in plan.items()}
    assert labels == {"3v3_a": "3V3", "3v3_b": "3V3", "5vin": "5Vin", "gnd_ring": "GND", "gnd_mic": "GND",
                      "gnd_amp": "GND"}


def test_every_wire_has_two_ends_and_a_sane_length():
    keys = [r.wire.key for r in result().routed]
    assert len(keys) == len(set(keys)) == len(wiring.WIRES)
    for r in result().routed:
        assert r.a.key != r.b.key
        assert 30 <= r.length_mm <= 200, r.wire.key
        assert r.length_mm % wiring.ROUND_STEP == 0
        assert r.length_mm >= math.dist(r.a.pos, r.b.pos) - (wiring.RES_SPAN if r.wire.res else 0)
        assert r.open >= r.closed - 1e-9
        need = wiring.ROUTE_FACTOR * r.open + 2 * wiring.END_ALLOW - (wiring.RES_SPAN if r.wire.res else 0)
        assert need + r.wire.trim <= r.length_mm < need + r.wire.trim + wiring.ROUND_STEP
        assert r.length_mm >= r.closed + 2 * wiring.END_ALLOW - (wiring.RES_SPAN if r.wire.res else 0), r.wire.key


def test_wires_inside_the_tray_need_no_service_length():
    for r in result().routed:
        if r.join == "tray":
            assert r.open == pytest.approx(r.closed)
        else:
            assert r.open > r.closed


def test_every_end_sits_on_its_part():
    cube = manifold3d.Manifold.cube((0.1, 0.1, 0.1), True)
    parts = {}
    for r in result().routed:
        for end in (r.a, r.b):
            m = parts.setdefault(end.part, _manifold(mesh(end.part)))
            probe = tuple(c + 0.3 * d for c, d in zip(end.pos, end.inward))
            assert (cube.translate(probe) ^ m).volume() > 0.9e-3, (r.wire.key, end.label)


def _trim(pts, d):
    out = [tuple(q) for q in pts]
    left = d
    while left > 0 and len(out) > 1:
        seg = math.dist(out[0], out[1])
        if seg <= left:
            left -= seg
            out.pop(0)
            continue
        f = left / seg
        out[0] = tuple(a + (b - a) * f for a, b in zip(out[0], out[1]))
        left = 0
    return out


def _tube(pts):
    ball = manifold3d.Manifold.sphere(TUBE_R, 12)
    segs = [manifold3d.Manifold.batch_hull([ball.translate(a), ball.translate(b)]) for a, b in zip(pts, pts[1:])]
    return manifold3d.Manifold.batch_boolean(segs, manifold3d.OpType.Add)


def test_closed_routes_clear_the_case_and_the_parts():
    solids = {name: _manifold(mesh(name)) for name in OBSTACLES}
    for r in result().routed:
        path = r.a.chain + r.b.chain[::-1]
        path = _trim(_trim(path, TRIM)[::-1], TRIM)[::-1]
        tube = _tube(path)
        for name, solid in solids.items():
            assert (tube ^ solid).volume() < 1e-3, (r.wire.key, name)


def test_wire_level_runs_between_the_pins_and_the_magnet():
    p = params()
    z = result().z_bus
    assert z > max(p["dk_pin_top_z"], p["amp_top_z"]) + 1
    assert z < p["spk_mag_z"] - 2


def test_amp_header_order_puts_vin_by_the_capacitor_leads():
    p = params()
    xs = [q[0] for q in p["amp_hdr_xy"]]
    vin = wiring.AMP_PINS.index("Vin")
    gnd = wiring.AMP_PINS.index("GND")
    lead_x = p["cap_xy"][0] + p["cap_l"] / 2
    assert xs[vin] == max(xs)
    assert abs(xs[vin] - lead_x) < 2 and abs(xs[gnd] - lead_x) < 4
    wired = {e.label for r in result().routed for e in (r.a, r.b)}
    assert "amp GAIN" not in wired and "amp SD" not in wired


def test_transistor_gate_is_the_lead_nearest_the_devkit():
    ends = {e.key: e for r in result().routed for e in (r.a, r.b)}
    xs = {k: abs(ends["tr_" + k].pos[0]) for k in ("g", "d", "s")}
    assert xs["g"] < xs["d"] < xs["s"]


def test_left_mic_is_on_the_row_2_side():
    p = params()
    assert math.cos(math.radians(p["mic_az_l"])) < 0
    assert math.copysign(1, wiring.row_x(p, 2)) < 0


def test_tool_output_is_the_table_in_the_doc():
    res = result()
    table = wiring.render_table(res)
    proc = subprocess.run([sys.executable, str(CASE_DIR / "tools" / "wiring.py")], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    assert table in proc.stdout
    doc = (CASE_DIR / "WIRING.md").read_text()
    assert table in doc
    assert f"{res.hinge_deg}°" in doc
    assert wiring.hinge_words(res.hinge_deg) in doc
    assert f"{res.body_support_h / 10:.0f} cm" in doc
    side = "left" if res.plus_side == "l" else "right"
    assert f"its “+” end on the {side}" in doc
    assert "WIRING.md" in (CASE_DIR / "README.md").read_text()
