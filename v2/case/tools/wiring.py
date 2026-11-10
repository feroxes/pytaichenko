import argparse
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

CASE_DIR = Path(__file__).resolve().parent.parent
SCAD_FILE = CASE_DIR / "pytaichenko-case.scad"
FIRMWARE_DIR = CASE_DIR.parent / "firmware" / "components"
FIRMWARE_FILES = ("pt_audio/pt_audio.c", "pt_ui/pt_ui.c")
OPENSCAD = os.environ.get("OPENSCAD", "/opt/homebrew/bin/openscad")

ROUTE_FACTOR = 1.1
END_ALLOW = 5.0
ROUND_STEP = 5.0
BUS_LIFT = 2.0
WALL_CLEAR = 1.0
LEAD_APPROACH = 3.0
TERM_PITCH = 5.0
TERM_ENTRY_H = 3.5
SPK_LUG_FRACTION = 0.25
RES_SPAN = 12.0
MIC_FIT_TRIM = -20.0
FIL_MINUS_FIT_TRIM = -20.0
FIL_PLUS_FIT_TRIM = -55.0
HINGE_GAP = 10.0
HINGE_STEP = 5
SOLDER_ANGLE = 90
SWING_STEP = 5

ROW1 = ["GND", "TX", "RX", "GPIO1", "GPIO2", "GPIO42", "GPIO41", "GPIO40", "GPIO39", "GPIO38", "GPIO37",
        "GPIO36", "GPIO35", "GPIO0", "GPIO45", "GPIO48", "GPIO47", "GPIO21", "GPIO20", "GND", "GND", "?"]
ROW2 = ["3V3", "3V3", "RST", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO15", "GPIO16", "GPIO17", "GPIO18",
        "GPIO8", "GPIO3", "GPIO46", "GPIO9", "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "5Vin", "GND"]
ROWS = {1: ROW1, 2: ROW2}

AMP_PINS = ["LRC", "BCLK", "DIN", "GAIN", "SD", "GND", "Vin"]

SIGNALS = {
    "mic_sck": "MIC_BCLK",
    "mic_ws": "MIC_WS",
    "mic_sd": "MIC_DIN",
    "amp_bclk": "SPK_BCLK",
    "amp_lrc": "SPK_LRC",
    "amp_din": "SPK_DOUT",
    "ring": "RING_GPIO",
    "button": "BUTTON_TOP_GPIO",
}

POWER_PINS = {
    "3v3_a": (2, 0),
    "3v3_b": (2, 1),
    "5vin": (2, 20),
    "gnd_ring": (2, 21),
    "gnd_mic": (1, 0),
    "gnd_amp": (1, 20),
}

TRAY, BODY, LID = "tray", "body", "lid"


@dataclass
class End:
    key: str
    label: str
    part: str
    side: str
    pos: tuple
    inward: tuple
    chain: list = field(default_factory=list)


@dataclass
class Wire:
    key: str
    a: str
    b: str
    colour: str
    res: bool = False
    trim: float = 0.0


@dataclass
class Routed:
    wire: Wire
    a: End
    b: End
    closed: float
    open: float
    length_mm: float

    @property
    def length_cm(self):
        return self.length_mm / 10

    @property
    def join(self):
        sides = {self.a.side, self.b.side}
        if sides == {TRAY}:
            return "tray"
        return "tray ↔ " + (sides - {TRAY}).pop()


@dataclass
class Result:
    routed: list
    hinge_deg: int
    plus_side: str
    body_support_h: float
    z_bus: float
    pins: dict


def _floats(v):
    if isinstance(v, list):
        return [_floats(x) for x in v]
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return float(v)
    return v


def _parse_value(text):
    text = text.strip()
    if text in ("true", "false"):
        return text == "true"
    if text.startswith("["):
        return _floats(json.loads(text))
    if text.startswith('"'):
        return text.strip('"')
    return float(text)


def load_params():
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "params.echo"
        cmd = [OPENSCAD, "--backend=manifold", "-D", 'part="params"', "-o", str(out), str(SCAD_FILE)]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0 or not out.exists():
            raise RuntimeError("openscad params echo failed\n" + proc.stderr)
        text = out.read_text()
    values = {}
    for line in text.splitlines():
        if not line.startswith("ECHO:"):
            continue
        for name, raw in re.findall(r"(\w+) = (\[(?:[^\[\]]|\[[^\[\]]*\])*\]|\"[^\"]*\"|[^,]+)", line[5:]):
            values[name] = _parse_value(raw)
    return values


def load_firmware(base=FIRMWARE_DIR):
    gpio = {}
    for rel in FIRMWARE_FILES:
        for name, num in re.findall(r"#define\s+(\w+)\s+GPIO_NUM_(\d+)", (Path(base) / rel).read_text()):
            gpio[name] = int(num)
    return gpio


def _dist(a, b):
    return math.dist(a, b)


def _poly(pts):
    return sum(_dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def _rot(xy, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (xy[0] * c - xy[1] * s, xy[0] * s + xy[1] * c)


def row_x(p, row):
    com = math.copysign(1.0, p["dk_com_x"])
    return com * p["dk_row_x"] if row == 1 else -com * p["dk_row_x"]


def pin_y(p, index):
    k = int(p["dk_pins"]) - 1 - index
    return p["dk_pin_y0"] + k * p["dk_pin_pitch"]


def devkit_pin(p, row, index):
    return (row_x(p, row), pin_y(p, index), p["dk_pin_top_z"])


def find_pin(label):
    hits = [(r, i) for r, labels in ROWS.items() for i, name in enumerate(labels) if name == label]
    if len(hits) != 1:
        raise ValueError(f"{label}: {len(hits)} pins")
    return hits[0]


def pin_plan(gpio):
    plan = {}
    for key, define in SIGNALS.items():
        plan[key] = find_pin(f"GPIO{gpio[define]}")
    plan.update(POWER_PINS)
    return plan


def pin_name(row, index):
    return f"DevKitC {ROWS[row][index]} (row {row}, pin {index + 1} from the antenna)"


def _bus(pt, z):
    return (pt[0], pt[1], z)


def build_ends(p, gpio, plus_side):
    z_bus = max(p["dk_pin_top_z"], p["amp_top_z"]) + BUS_LIFT
    r_drop = p["tray_screw_r"] - p["boss_od"] / 2 - WALL_CLEAR
    ends = {}
    plan = pin_plan(gpio)

    for key, (row, index) in plan.items():
        pos = devkit_pin(p, row, index)
        ends["dk_" + key] = End("dk_" + key, pin_name(row, index), "asm_devkit", TRAY, pos, (0, 0, -1),
                                [pos, _bus(pos, z_bus)])

    amp_z = p["amp_pin_top_z"]
    for i, name in enumerate(AMP_PINS):
        hx, hy = p["amp_hdr_xy"][i]
        pos = (hx, hy, amp_z)
        ends["amp_" + name.lower()] = End("amp_" + name.lower(), f"amp {name}", "asm_amp", TRAY, pos, (0, 0, -1),
                                          [pos, _bus(pos, z_bus)])

    ax, ay = p["amp_xy"]
    entry_z = p["amp_z0"] + p["amp_t"] + TERM_ENTRY_H
    out_dir = _rot((-1, 0), p["amp_rot"])
    for name, sign in (("plus", 1), ("minus", -1)):
        lx, ly = _rot((-p["amp_w"] / 2, sign * TERM_PITCH / 2), p["amp_rot"])
        pos = (ax + lx, ay + ly, entry_z)
        front = (pos[0] + out_dir[0] * LEAD_APPROACH, pos[1] + out_dir[1] * LEAD_APPROACH, entry_z)
        label = "amp terminal " + ("+" if sign > 0 else "−")
        ends["term_" + name] = End("term_" + name, label, "asm_amp", TRAY, pos, (-out_dir[0], -out_dir[1], 0),
                                   [pos, front, _bus(front, z_bus)])

    tx, ty = p["transistor_xy"]
    tip_y = ty + p["tr_len"] / 2
    lead_z = p["tr_z0"] + p["tr_lead_z"] + p["tr_lead_t"] / 2
    for name, k in (("g", 1), ("d", 0), ("s", -1)):
        pos = (tx + k * p["tr_lead_pitch"], tip_y, lead_z)
        out = (pos[0], pos[1] + LEAD_APPROACH, lead_z)
        label = f"transistor {name.upper()}" + (" (47 Ω soldered on it)" if name == "g" else "")
        ends["tr_" + name] = End("tr_" + name, label, "asm_transistor", TRAY, pos,
                                 (0, -1, 0), [pos, out, _bus(out, z_bus)])

    for side, az in (("l", p["mic_az_l"]), ("r", p["mic_az_r"])):
        c, s = math.cos(math.radians(az)), math.sin(math.radians(az))
        r = p["mic_face_r"] - p["mic_t"] - p["mic_can_h"]
        pos = (r * c, r * s, p["mic_z"])
        off = (r_drop * c, r_drop * s, p["mic_z"])
        for pad in ("VDD", "GND", "SD", "WS", "SCK"):
            key = f"mic{side}_{pad.lower()}"
            ends[key] = End(key, f"mic {side.upper()} {pad}", f"asm_mic_{side}", BODY, pos, (c, s, 0),
                            [pos, off, _bus(off, z_bus)])

    for pts in (p["fil_tail_pts"], p["fil_tail_pts_m"]):
        side = "r" if pts[-1][0] > 0 else "l"
        end, prev = tuple(pts[-1]), tuple(pts[-2])
        d = _dist(end, prev)
        inward = tuple((b - a) / d for a, b in zip(end, prev))
        sign = "+" if side == plus_side else "−"
        key = "fil_plus" if side == plus_side else "fil_minus"
        a = math.atan2(end[1], end[0])
        off = (r_drop * math.cos(a), r_drop * math.sin(a), end[2])
        where = "left" if side == "l" else "right"
        label = f"filament {sign} ({where} end" + (", 47 Ω soldered on it)" if key == "fil_plus" else ")")
        ends[key] = End(key, label, "asm_filament_tails",
                        BODY, end, inward, [end, off, _bus(off, z_bus)])

    route = [tuple(q) for q in p["wire_btn_pts"]]
    leg_z = p["btn_z0"] - p["btn_leg_l"]
    for name, sy in (("a", 1), ("b", -1)):
        lx, ly = _rot((p["button_r"] - p["btn_leg_x"], sy * p["btn_leg_y"]), p["button_az"])
        pos = (lx, ly, leg_z)
        ends["btn_" + name] = End("btn_" + name, f"button leg {name.upper()}", "asm_button", LID, pos, (0, 0, 1),
                                  [pos] + route + [_bus(route[-1], z_bus)])

    term = [tuple(q) for q in p["wire_term_pts"]]
    lug_y = -(p["spk_basket_d"] / 2 + p["spk_term_out"])
    lug_z = (p["spk_term_z0"] + p["spk_term_z1"]) / 2
    for name, sx in (("plus", 1), ("minus", -1)):
        pos = (sx * p["spk_term_w"] * SPK_LUG_FRACTION, lug_y, lug_z)
        label = "speaker " + ("+" if sx > 0 else "−")
        ends["spk_" + name] = End("spk_" + name, label, "asm_speaker", LID, pos, (0, 1, 0),
                                  [pos] + term + [_bus(term[-1], z_bus)])
    return ends, z_bus, plan


WIRES = [
    Wire("M1", "micl_vdd", "dk_3v3_a", "red", trim=MIC_FIT_TRIM),
    Wire("M2", "micl_gnd", "dk_gnd_mic", "brown", trim=MIC_FIT_TRIM),
    Wire("M3", "micl_sck", "dk_mic_sck", "yellow", trim=MIC_FIT_TRIM),
    Wire("M4", "micl_ws", "dk_mic_ws", "green", trim=MIC_FIT_TRIM),
    Wire("M5", "micl_sd", "dk_mic_sd", "blue", trim=MIC_FIT_TRIM),
    Wire("M6", "micr_vdd", "dk_3v3_b", "red", trim=MIC_FIT_TRIM),
    Wire("M7", "micr_gnd", "dk_gnd_mic", "brown", trim=MIC_FIT_TRIM),
    Wire("M8", "micr_sck", "dk_mic_sck", "yellow", trim=MIC_FIT_TRIM),
    Wire("M9", "micr_ws", "dk_mic_ws", "green", trim=MIC_FIT_TRIM),
    Wire("M10", "micr_sd", "dk_mic_sd", "blue", trim=MIC_FIT_TRIM),
    Wire("A1", "amp_lrc", "dk_amp_lrc", "green"),
    Wire("A2", "amp_bclk", "dk_amp_bclk", "yellow"),
    Wire("A3", "amp_din", "dk_amp_din", "blue"),
    Wire("A4", "amp_gnd", "dk_gnd_amp", "brown"),
    Wire("A5", "amp_vin", "dk_5vin", "red"),
    Wire("S1", "spk_plus", "term_plus", "red"),
    Wire("S2", "spk_minus", "term_minus", "black"),
    Wire("R1", "tr_g", "dk_ring", "white", res=True),
    Wire("R2", "tr_s", "dk_gnd_ring", "brown"),
    Wire("R3", "fil_minus", "tr_d", "grey", trim=FIL_MINUS_FIT_TRIM),
    Wire("R4", "fil_plus", "dk_5vin", "red", res=True, trim=FIL_PLUS_FIT_TRIM),
    Wire("B1", "btn_a", "dk_button", "purple"),
    Wire("B2", "btn_b", "dk_gnd_ring", "brown"),
]


def swing(p, pt, hinge_deg, alpha_deg):
    u = (math.cos(math.radians(hinge_deg)), math.sin(math.radians(hinge_deg)))
    t = (-u[1], u[0])
    r_h = p["case_r"] + HINGE_GAP / 2
    z_h = p["body_bot_z"]
    s = pt[0] * u[0] + pt[1] * u[1]
    w = pt[0] * t[0] + pt[1] * t[1]
    vs, vz = s - r_h, pt[2] - z_h
    a = math.radians(alpha_deg)
    s2 = r_h + vs * math.cos(a) - vz * math.sin(a)
    z2 = z_h + vs * math.sin(a) + vz * math.cos(a)
    return (s2 * u[0] + w * t[0], s2 * u[1] + w * t[1], z2)


def open_span(p, dt, db, hinge_deg):
    return max(_dist(swing(p, dt, hinge_deg, a), db) for a in range(0, SOLDER_ANGLE + 1, SWING_STEP))


def round_up(mm):
    return math.ceil(mm / ROUND_STEP - 1e-9) * ROUND_STEP


def route(p, wire, ends, hinge_deg):
    a, b = ends[wire.a], ends[wire.b]
    if a.side == TRAY and b.side != TRAY:
        a, b = b, a
    ca, cb = a.chain, b.chain
    closed = _poly(ca) + _dist(ca[-1], cb[-1]) + _poly(cb)
    opened = closed
    if a.side != TRAY:
        opened = _poly(ca) + open_span(p, cb[-1], ca[-1], hinge_deg) + _poly(cb)
    need = ROUTE_FACTOR * max(closed, opened) + 2 * END_ALLOW - (RES_SPAN if wire.res else 0)
    return Routed(wire, a, b, closed, opened, round_up(need) + wire.trim)


def compute(p, gpio):
    best = None
    for plus_side in ("l", "r"):
        ends, z_bus, plan = build_ends(p, gpio, plus_side)
        for hinge in range(0, 360, HINGE_STEP):
            routed = [route(p, w, ends, hinge) for w in WIRES]
            total = sum(r.length_mm for r in routed)
            if best is None or total < best[0] - 1e-9:
                best = (total, Result(routed, hinge, plus_side, p["body_bot_z"] + HINGE_GAP / 2, z_bus, plan))
    return best[1]


def hinge_words(deg):
    names = ["right", "front-right", "front", "front-left", "left", "back-left", "back", "back-right"]
    return names[round((deg % 360) / 45) % 8]


def render_table(result):
    lines = ["| # | From | To | Length | Colour | Joins |", "|---|---|---|---|---|---|"]
    for r in result.routed:
        lines.append(f"| {r.wire.key} | {r.a.label} | {r.b.label} | {r.length_cm:.1f} cm | {r.wire.colour} | {r.join} |")
    return "\n".join(lines)


def render(result):
    out = [render_table(result), ""]
    out.append(f"hinge side: {result.hinge_deg} deg, {hinge_words(result.hinge_deg)}")
    out.append(f"filament + end: {'left' if result.plus_side == 'l' else 'right'}")
    out.append(f"body support height: {result.body_support_h:.1f} mm")
    out.append(f"wire level above the pins: z {result.z_bus:.1f}")
    out.append(f"total wire: {sum(r.length_mm for r in result.routed) / 10:.1f} cm")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--detail", action="store_true")
    args = ap.parse_args(argv)
    result = compute(load_params(), load_firmware())
    print(render(result))
    if args.detail:
        print()
        for r in result.routed:
            print(f"{r.wire.key}: closed {r.closed:.1f} mm, open {r.open:.1f} mm, cut {r.length_mm:.0f} mm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
