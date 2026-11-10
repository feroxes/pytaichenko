import functools
import itertools
from pathlib import Path

import manifold3d
import numpy as np
import pytest
import trimesh

from conftest import _manifold, mesh, overlap, params

TOL = 1e-3
CASE_DIR = Path(__file__).resolve().parent.parent
ANTENNA_CLEAR = 10.0

PRINTED = [
    "top",
    "body",
    "ring_arc",
    "tray",
    "button_cap",
    "speaker_gasket",
    "top_gasket",
    "tray_gasket",
    "top_plugs",
    "feet",
    "com_bushing",
]

DUMMIES = [
    "speaker",
    "devkit",
    "antenna",
    "com_plug",
    "amp",
    "cap",
    "mic_l",
    "mic_r",
    "button",
    "filament",
    "filament_tails",
    "transistor",
]

FASTENERS = [
    "speaker_screws",
    "metal_washers",
    "top_screws",
    "inserts",
    "tray_screws",
]

METAL = [
    "speaker",
    "speaker_screws",
    "metal_washers",
    "top_screws",
    "inserts",
    "tray_screws",
    "amp",
    "cap",
    "com_plug",
]

BUILD = {
    "top": "PETG-black_top",
    "body": "PETG-black_body",
    "ring_arc": "PETG-white_ring_arc",
    "tray": "PETG-black_tray",
    "button_cap": "PETG-black_button_cap",
    "speaker_gasket": "TPU_speaker_gasket",
    "top_gasket": "TPU_top_gasket",
    "tray_gasket": "TPU_tray_gasket",
    "top_plug": "PETG-black_top_plug_x3",
    "foot": "TPU_foot_x4",
    "com_bushing": "TPU_com_bushing",
}

PAIRS = [(a, b) for a, b in itertools.combinations(PRINTED, 2)] + [
    (a, b) for a in PRINTED for b in DUMMIES + FASTENERS
]


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


@pytest.mark.parametrize("a,b", PAIRS)
def test_no_overlap(a, b):
    assert overlap(f"asm_{a}", f"asm_{b}") < TOL


@functools.lru_cache(maxsize=None)
def _antenna_zone():
    ant = _solid("asm_antenna")
    return ant.minkowski_sum(manifold3d.Manifold.sphere(ANTENNA_CLEAR, 48))


@pytest.mark.parametrize("name", METAL)
def test_antenna_clear_of_metal(name):
    assert float((_antenna_zone() ^ _solid(f"asm_{name}")).volume()) < TOL


def test_antenna_metal_list_covers_every_metal_part():
    text = (CASE_DIR / "pytaichenko-case.scad").read_text()
    for name in ("speaker_screws", "metal_washers", "top_screws", "inserts", "tray_screws", "spk_inserts"):
        assert f'"asm_{name}"' in text
    inserts = _solid("asm_inserts")
    assert float((_solid("asm_spk_inserts") - inserts).volume()) < TOL
    assert len(inserts.decompose()) == 2 + len(params()["top_screw_xy"]) + len(params()["tray_screw_xy"])


def _probe(x, y, z0, z1, d):
    return manifold3d.Manifold.cylinder(z1 - z0, d / 2, d / 2, 48).translate([x, y, z0])


def test_screw_access_order():
    p = params()
    d = p["screw_head_d"]
    for x, y in p["spk_ear_xy"]:
        probe = _probe(x, y, p["spk_screw_head_z"] - 60, p["spk_screw_head_z"] - 0.01, d)
        for name in ("top", "speaker"):
            assert float((probe ^ _solid(f"asm_{name}")).volume()) < TOL, (x, name)
        assert float((probe ^ _solid("asm_tray")).volume()) > 1.0
    everything = PRINTED + DUMMIES + FASTENERS
    for x, y in p["top_screw_xy"]:
        probe = _probe(x, y, p["top_screw_head_z"] + 0.01, p["H"] + 30, d)
        for name in everything:
            if name == "top_plugs":
                continue
            assert float((probe ^ _solid(f"asm_{name}")).volume()) < TOL, (x, name)
        assert float((probe ^ _solid("asm_top_plugs")).volume()) > 1.0
    for name in everything:
        assert overlap("asm_tray_screw_probe", f"asm_{name}") < TOL, name


def test_overall_size():
    p = params()
    verts = np.concatenate([mesh(f"asm_{n}").vertices for n in PRINTED])
    assert 2 * np.hypot(verts[:, 0], verts[:, 1]).max() <= 84.01
    assert verts[:, 2].max() == pytest.approx(p["H"], abs=1e-4)
    assert verts[:, 2].min() == pytest.approx(-p["foot_out"], abs=1e-4)
    assert verts[:, 2].max() - verts[:, 2].min() == pytest.approx(p["H"] + p["foot_out"], abs=1e-4)


def test_build_lists_every_printed_part():
    text = (CASE_DIR / "build.sh").read_text()
    for part, name in BUILD.items():
        assert f'"{part}:{name}"' in text
    assert text.count(":PETG-") + text.count(":TPU_") == len(BUILD)


def test_stl_folder_holds_exactly_the_printed_parts():
    stl = CASE_DIR / "stl"
    names = sorted(f.stem for f in stl.glob("*.stl"))
    assert names == sorted(BUILD.values())


@pytest.mark.parametrize("part,name", sorted(BUILD.items()))
def test_committed_stl_matches_the_model(part, name):
    saved = trimesh.load(CASE_DIR / "stl" / f"{name}.stl", force="mesh", process=True)
    m = mesh(part)
    assert saved.is_watertight
    assert saved.volume == pytest.approx(m.volume, rel=1e-4)
    assert np.allclose(saved.bounds, m.bounds, atol=1e-3)


LEAK_BUDGET = 20.0
OUTSIDE = (45.5, 0.0, -4.8)
INSIDE = (-25.0, 10.0, 20.0)


def _union(names, extra=()):
    return manifold3d.Manifold.batch_boolean([_solid(f"asm_{n}") for n in names] + list(extra), manifold3d.OpType.Add)


def _cyl(x, y, z0, z1, r, fn=64):
    return manifold3d.Manifold.cylinder(z1 - z0, r, r, fn).translate([x, y, z0])


def _band(r_out, w, z0, h):
    return _cyl(0, 0, z0, z0 + h, r_out, 360) - _cyl(0, 0, z0 - 1, z0 + h + 1, r_out - w, 360)


def _contact_seals(p):
    seals = [
        _band(p["top_groove_out_r"] + 0.02, p["top_groove_out_r"] - p["top_groove_in_r"] + 0.04,
              p["top_z0"] + p["top_gasket_c"] - p["top_gasket_t"], p["top_gasket_t"]),
        _band(p["tray_groove_out_r"] + 0.02, p["tray_groove_out_r"] - p["tray_groove_in_r"] + 0.04,
              p["body_bot_z"] - p["tray_groove_depth"], p["tray_gasket_t"]),
        _band(p["spk_gasket_od"] / 2, (p["spk_gasket_od"] - p["spk_gasket_id"]) / 2, p["spk_rim_top_z"], p["spk_gasket_t"]),
    ]
    cx, cz = p["dk_com_x"], p["usb_z"]
    outer = manifold3d.Manifold.cube([p["tray_com_w"] + 0.02, 4, p["tray_com_h"] + 0.02], True)
    hole = manifold3d.Manifold.cube([p["com_hole_w"], 6, p["com_hole_h"]], True)
    seals.append((outer - hole).translate([cx, -40, cz]))
    r0, r1 = p["mic_port_d"] / 2 + 0.2, p["mic_d"] / 2 - 0.2
    for az in (p["mic_az_l"], p["mic_az_r"]):
        ring = manifold3d.Manifold.cylinder(0.1, r1, r1, 64) - manifold3d.Manifold.cylinder(0.3, r0, r0, 32).translate([0, 0, -0.1])
        seals.append(ring.translate([0, 0, -0.05]).rotate([0, 90, 0]).translate([p["mic_face_r"], 0, p["mic_z"]]).rotate([0, 0, az]))
    return seals


def _button_xy(p):
    a = np.radians(p["button_az"])
    return p["button_r"] * np.cos(a), p["button_r"] * np.sin(a)


def _leak_plugs(p):
    cx, cz = p["dk_com_x"], p["usb_z"]
    w, h = p["com_hole_w"] + 0.04, p["com_hole_h"] + 0.04
    plugs = {"com": manifold3d.Manifold.cube([w, 45 + p["com_bush_y1"] - 0.5, h]).translate([cx - w / 2, -45, cz - h / 2])}
    bx, by = _button_xy(p)
    plugs["button"] = _cyl(bx, by, p["btn_plate_z"] + 0.5, p["H"] + 1, p["button_hole_d"] / 2 + 0.05, 96)
    for i, (x, y) in enumerate(p["tray_screw_xy"]):
        plugs[f"tray_screw_{i}"] = _cyl(x, y, p["tray_seat_z"] + p["csk_h"] + 0.2, p["body_bot_z"] - 0.5,
                                        p["screw_thru_d"] / 2 + 0.05, 48)
    for i, (x, y) in enumerate(p["top_screw_xy"]):
        zc = p["top_seat_z"] - p["csk_h"]
        plugs[f"top_screw_{i}"] = _cyl(x, y, zc - 1.0, zc - 0.2, p["screw_thru_d"] / 2 + 0.05, 48)
    z0, z1 = p["ring_z0"] - 0.3, p["top_z0"] + 0.3
    for i, a in enumerate((p["bridge_a0"], p["bridge_a1"])):
        cs = manifold3d.CrossSection.square([4.8, z1 - z0]).translate([37.4, z0])
        plugs[f"arc_end_{i}"] = manifold3d.Manifold.revolve(cs, 16, 0.6).rotate([0, 0, a - 0.3])
    return plugs


def _connected(solid):
    env = _cyl(0, 0, -5, params()["H"] + 3, 46, 96)
    s = 0.2
    probe = manifold3d.Manifold.cube([s, s, s], True)
    for comp in (env - solid).decompose():
        if float((comp ^ probe.translate(list(OUTSIDE))).volume()) > 0.5 * s ** 3:
            return float((comp ^ probe.translate(list(INSIDE))).volume()) > 0.5 * s ** 3
    raise AssertionError("outside point is not free")


def _open_area(section, region):
    return float((region - section).area())


def _leak_areas(p, lid="top"):
    cs = manifold3d.CrossSection
    areas = {}
    cx, cz = p["dk_com_x"], p["usb_z"]
    com = _union(["tray", "com_bushing", "com_plug"]).rotate([-90, 0, 0]).slice(40.0)
    rect = cs.square([p["com_hole_w"] + 0.4, p["com_hole_h"] + 0.4]).translate(
        [cx - p["com_hole_w"] / 2 - 0.2, cz - p["com_hole_h"] / 2 - 0.2])
    areas["com"] = _open_area(com, rect)
    bx, by = _button_xy(p)
    btn = _union([lid, "button_cap"]).slice(float(p["H"] - 1))
    areas["button"] = _open_area(btn, cs.circle(p["button_hole_d"] / 2 + 0.3, 96).translate([bx, by]))
    z = (p["tray_seat_z"] + p["csk_h"] + p["body_bot_z"]) / 2
    scr = _union(["tray", "tray_screws"]).slice(float(z))
    areas["tray_screws"] = sum(_open_area(scr, cs.circle(2.0, 64).translate([x, y])) for x, y in p["tray_screw_xy"])
    ring = _union(["body", "ring_arc"])
    r0, r1, dz = 41.5, 41.9, 0.1
    slit = 0.0
    for a in (p["bridge_a0"], p["bridge_a1"]):
        wedge = cs.square([r1 - r0, 1.4]).translate([r0, -0.7]).rotate(float(a))
        for zz in np.arange(p["ring_z0"] + dz / 2, p["ring_top_z"], dz):
            slit += _open_area(ring.slice(float(zz)), wedge) / (r1 - r0) * dz
    areas["arc_ends"] = slit
    return areas


def test_leak_budget(lid="top"):
    p = params()
    names = [lid if n == "top" else n for n in PRINTED if not n.endswith("gasket")] + DUMMIES + FASTENERS
    box = _union(names, _contact_seals(p))
    s = manifold3d.Manifold.cube([0.2, 0.2, 0.2], True)
    assert float((box ^ s.translate(list(INSIDE))).volume()) < TOL
    plugs = _leak_plugs(p)
    assert _connected(box)
    assert not _connected(manifold3d.Manifold.batch_boolean([box] + list(plugs.values()), manifold3d.OpType.Add))
    areas = _leak_areas(p, lid)
    total = sum(areas.values())
    msg = f"open area {total:.2f} mm2: " + ", ".join(f"{k} {v:.2f}" for k, v in areas.items())
    assert total < LEAK_BUDGET, msg
    assert areas["com"] == pytest.approx(p["com_hole_w"] * p["com_hole_h"] - p["com_plug_w"] * p["com_plug_h"], abs=0.1), msg
    print(msg)


LID_GROUP = ["top", "speaker", "speaker_gasket", "speaker_screws", "metal_washers", "spk_inserts", "button", "button_cap",
             "top_gasket"]
BODY_GROUP = ["body", "ring_arc", "filament", "filament_tails", "mic_l", "mic_r", "body_inserts"]
TRAY_GROUP = ["tray", "tray_gasket", "com_bushing", "devkit", "antenna", "amp", "cap", "transistor", "feet"]
SWEEP_STEP = 0.1


def _sweep(moving, still, direction, length):
    worst = 0.0
    for t in np.arange(0.0, length + 1e-9, SWEEP_STEP):
        worst = max(worst, float((moving.translate([0, 0, direction * t]) ^ still).volume()))
    return worst


def test_assembly_paths_are_clear():
    p = params()
    lid, body, tray = _union(LID_GROUP), _union(BODY_GROUP), _union(TRAY_GROUP)
    length = p["H"] + p["foot_out"] + 1
    still = manifold3d.Manifold.batch_boolean([body, tray], manifold3d.OpType.Add)
    assert _sweep(lid, still, 1, length) < TOL
    assert float((lid.translate([0, 0, -0.3]) ^ still).volume()) > TOL
    still = manifold3d.Manifold.batch_boolean([body, lid], manifold3d.OpType.Add)
    assert _sweep(tray, still, -1, length) < TOL
    assert float((tray.translate([0, 0, 0.3]) ^ still).volume()) > TOL
