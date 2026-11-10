import numpy as np
import pytest

import manifold3d

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3

COMPONENTS = [
    "body",
    "filament",
    "devkit",
    "usb_plug",
    "speaker",
    "amp",
    "cap",
    "mic",
    "top_screws",
    "tray_screws",
    "speaker_screws",
    "metal_washers",
    "top_plugs",
    "feet",
]


def arc_half_deg(p):
    return 90.0 - p["bridge_half_deg"]


def axis_samples(p, count=41, margin=0.2):
    half = arc_half_deg(p) - margin
    z = p["ring_arc_h"] / 2
    out = []
    for phi in np.linspace(-half, half, count):
        c, s = np.cos(np.radians(phi)), np.sin(np.radians(phi))
        out.append((np.array([p["filament_r"] * c, p["filament_r"] * s, z]), c, s))
    return out


def az_deg(xy):
    return np.degrees(np.arctan2(xy[:, 1], xy[:, 0]))


def arc_mids(p):
    return [b + 90.0 for b in p["bridge_angles"]]


def test_ring_arc_watertight_fits_bed():
    m = mesh("ring_arc")
    assert m.is_watertight
    assert m.volume > 0
    bed = params()["bed"]
    assert m.extents[0] < bed and m.extents[1] < bed
    assert abs(m.bounds[0][2]) < 1e-6


@pytest.mark.parametrize("name", COMPONENTS)
def test_ring_clear(name):
    assert overlap("asm_ring", f"asm_{name}") < TOL


def test_ring_clear_of_button():
    assert overlap("asm_ring", "asm_button") < TOL


def test_ring_flush():
    p = params()
    m = mesh("asm_ring")
    a = np.radians(arc_mids(p)[0])
    along = m.vertices[:, :2] @ np.array([np.cos(a), np.sin(a)])
    assert abs((along.max() - along.min()) - 2 * p["ring_outer_r"]) < 0.1
    r = np.hypot(m.vertices[:, 0], m.vertices[:, 1])
    assert abs(r.max() - p["ring_outer_r"]) < 0.01


def exit_after(m, origin, direction, first):
    d = np.asarray(direction, dtype=float)
    d = d / np.linalg.norm(d)
    nxt = first_hit(m, np.asarray(origin, dtype=float) + (first + 0.01) * d, d)
    return None if nxt is None else first + 0.01 + nxt


def test_ring_holds_filament():
    p = params()
    m = mesh("ring_arc")
    half_fil = p["fil_d"] / 2
    for origin, c, s in axis_samples(p):
        for d in ((0, 0, -1), (c, s, 0), (-c, -s, 0), (c, s, -1), (-c, -s, -1)):
            t = first_hit(m, origin, d)
            assert t is not None
            assert half_fil + 0.05 <= t <= p["ring_pocket_r"] + 0.01


def test_ring_slit_opens_down_onto_the_body_wall():
    p = params()
    m = mesh("ring_arc")
    for origin, c, s in axis_samples(p):
        assert first_hit(m, origin, (0, 0, 1)) is None
    mid = p["filament_r"] + (p["ring_slit_out"] - p["ring_slit_in"]) / 2
    body = mesh("asm_body")
    for a in [m + d for m in arc_mids(p) for d in (-45, 0, 45)]:
        c, s = np.cos(np.radians(a)), np.sin(np.radians(a))
        under = first_hit(body, (mid * c, mid * s, p["ring_z0"] + 0.3), (0, 0, -1))
        assert under is not None and under <= 0.3 + 1e-6
    assert p["inner_r_profile"][int(p["ring_z0"])] < p["filament_r"] - p["ring_slit_in"] + 0.1


def test_ring_neck_grips_filament():
    p = params()
    m = mesh("ring_arc")
    mid = (p["ring_slit_out"] - p["ring_slit_in"]) / 2
    for origin, c, s in axis_samples(p, count=9):
        for dz in (0.75, 1.0, 1.4):
            start = origin + np.array([mid * c, mid * s, dz])
            out = first_hit(m, start, (c, s, 0))
            back = first_hit(m, start, (-c, -s, 0))
            assert out is not None and back is not None
            assert p["fil_d"] - 0.5 < out + back < p["fil_d"] - 0.05
    assert p["ring_neck_w"] < p["fil_d"]


def test_ring_heights():
    p = params()
    assert p["ring_h"] == pytest.approx(3.0)
    assert p["ring_z1"] - p["ring_z0"] == pytest.approx(3.0)
    assert p["ring_arc_h"] == pytest.approx(2.88)
    assert p["ring_above_t"] >= 0.55 and p["ring_below_t"] >= 0.55
    m = mesh("ring_arc")
    r = p["ring_pocket_r"]
    for origin, c, s in axis_samples(p, count=9):
        roof = first_hit(m, origin, (0, 0, -1))
        assert roof == pytest.approx(r, abs=0.01)
        top = exit_after(m, origin, (0, 0, -1), roof)
        assert top - roof >= 0.55
        for side, x in (("in", -(p["ring_slit_in"] + 0.15)), ("out", p["ring_slit_out"] + 0.15)):
            q = origin + np.array([x * c, x * s, r + 0.05])
            down = first_hit(m, q, (0, 0, 1))
            back = first_hit(m, q, (0, 0, -1))
            assert down is not None and back is not None, side
            assert 0.05 + down >= 0.55, side
    assert p["ring_neck_w"] < p["fil_d"]
    assert p["ring_pocket_r"] - p["fil_d"] / 2 >= 0.05


def test_ring_front_wall_thin():
    p = params()
    m = mesh("ring_arc")
    for origin, c, s in axis_samples(p, count=61, margin=0.2):
        for tilt in (-0.3, 0.0, 0.3):
            d = (c, s, tilt)
            first = first_hit(m, origin, d)
            last = exit_after(m, origin, d, first)
            assert last is not None and first_hit(m, origin + (last + 0.01) * np.array(d) / np.linalg.norm(d), d) is None
            assert 0.55 <= (last - first) * np.cos(np.arctan(tilt)) <= 0.7
    assert p["ring_front_t"] == pytest.approx(0.6)
    assert p["ring_outer_r"] - p["filament_r"] - p["ring_pocket_r"] == pytest.approx(p["ring_front_t"])


def _near_ring(part, p):
    m = mesh(part)
    tri = m.triangles
    r = np.linalg.norm(tri[:, :, :2], axis=2)
    keep = (r.max(axis=1) > p["ring_in_r"] - 1.0) & (tri[:, :, 2].min(axis=1) < p["ring_z1"] + 3.0) & (
        tri[:, :, 2].max(axis=1) > p["ring_z0"] - 3.0
    )
    return type("M", (), {"triangles": tri[keep]})()


def test_nothing_opaque_in_front_of_lit_arc():
    p = params()
    ring = mesh("asm_ring")
    others = [_near_ring(name, p) for name in ("asm_body", "asm_top", "asm_speaker", "asm_button", "asm_top_screws")]
    half = 90.0 - p["bridge_half_deg"] - 0.3
    for base in arc_mids(p):
        for phi in np.linspace(-half, half, 25):
            a = np.radians(base + phi)
            c, s = np.cos(a), np.sin(a)
            origin = np.array([p["filament_r"] * c, p["filament_r"] * s, p["fil_z"]])
            for tilt in (-0.6, -0.3, 0.0, 0.3, 0.6):
                d = np.array([c, s, tilt])
                for m in others:
                    assert first_hit(m, origin, d) is None, (base + phi, tilt)
                assert first_hit(ring, origin, d) is not None


def test_ring_is_as_small_as_the_button_allows():
    p = params()
    assert p["ring_outer_r"] * 2 <= 108
    reach = np.hypot(p["button_r"] + p["btn_in"], p["btn_in"])
    assert p["ring_outer_r"] - reach == pytest.approx(p["btn_wall"], abs=1e-3)
    assert p["button_r"] - p["btn_w"] / 2 - (p["spk_flange_half"] + p["spk_seat_flange_clr"]) == pytest.approx(
        p["btn_pocket_clr"], abs=1e-3)
    assert (p["button_az"] - p["spk_rot"]) % 90 == pytest.approx(0.0)
    assert p["top_z0"] <= p["btn_z0"] - p["btn_stub_l"] - p["btn_leg_gap"] + 1e-6
    v = mesh("asm_button").vertices
    over = v[np.hypot(v[:, 0], v[:, 1]) > p["ring_in_r"] - p["ring_in_clr"]]
    assert over[:, 2].min() >= p["ring_z1"] + p["btn_leg_gap"] - 1e-6
    assert over[:, 2].min() - mesh("asm_ring").bounds[1][2] >= 0.3 - 1e-6
    inner = v[v[:, 2] < p["ring_z1"]]
    assert np.hypot(inner[:, 0], inner[:, 1]).max() <= p["ring_in_r"] - p["ring_in_clr"]
    assert p["ring_in_r"] - p["spk_flange_max_r"] >= p["ring_in_clr"]
    assert p["ring_in_r"] >= p["spk_seat_max_r"] + p["ring_in_clr"]
    for name in ("asm_body", "asm_top", "asm_ring", "asm_tray"):
        w = mesh(name).vertices
        assert np.hypot(w[:, 0], w[:, 1]).max() <= p["ring_outer_r"] + 0.01, name
    v = mesh("asm_ring").vertices
    band = v[(v[:, 2] > p["ring_z0"] - 1e-3) & (v[:, 2] < p["ring_z1"] + 1e-3)]
    assert np.linalg.norm(band[:, :2], axis=1).min() == pytest.approx(p["ring_in_r"], abs=0.02)
    half = 90.0 - p["bridge_half_deg"] - 0.5
    keep = None
    for base in arc_mids(p):
        wedge = manifold3d.CrossSection([[[0, 0], [80 * np.cos(np.radians(base - half)), 80 * np.sin(np.radians(base - half))],
                                          [80 * np.cos(np.radians(base)), 80 * np.sin(np.radians(base))],
                                          [80 * np.cos(np.radians(base + half)), 80 * np.sin(np.radians(base + half))]]])
        keep = wedge if keep is None else keep + wedge
    zone = keep ^ (manifold3d.CrossSection.circle(p["ring_in_r"], 720) - manifold3d.CrossSection.circle(p["ring_in_r"] - 0.5, 720))
    for name in ("asm_top", "asm_body", "asm_speaker", "asm_speaker_gasket", "asm_metal_washers", "asm_speaker_screws", "asm_button", "asm_top_screws", "asm_top_plugs"):
        solid = _manifold(mesh(name))
        for z in np.linspace(p["ring_z0"] + 0.05, p["ring_z1"] - 0.05, 7):
            assert float((solid.slice(float(z)) ^ zone).area()) == 0.0, (name, z)


def test_filament_excess_fits():
    p = params()
    assert p["ring_arc_len"] == pytest.approx(np.pi * p["filament_r"] - p["bridge_w"], abs=1e-3)
    assert p["ring_arc_len"] < p["fil_lit"]
    assert p["fil_excess"] == pytest.approx((p["fil_lit"] - p["ring_arc_len"]) / 2, abs=1e-3)
    assert p["fil_excess"] >= 0
    assert p["chan_len"] >= p["fil_excess"]
    assert p["chan_len"] == pytest.approx(p["fil_excess"] + p["fil_contact"], abs=1e-3)
    assert p["fil_straight_lit"] == pytest.approx(p["fil_excess"] - p["chan_bend_len"], abs=1e-3)
    assert p["fil_straight_lit"] >= p["fil_trans"]
    lit = mesh("asm_filament")
    assert lit.is_watertight


def test_bridge_is_as_narrow_as_the_channels_allow():
    p = params()
    separation_slack = p["chan_p2"][1] - p["chan_d"] / 2 - 0.5
    assert 0 <= separation_slack < 0.05
    assert p["bridge_w"] == pytest.approx(14.0)
    assert p["fil_straight_lit"] >= p["fil_trans"]


def test_ring_overhangs():
    m = mesh("ring_arc")
    p = params()
    steep = m.face_normals[:, 2] < -np.cos(np.radians(45.0))
    on_bed = np.all(m.triangles[:, :, 2] <= m.bounds[0][2] + 1e-6, axis=1)
    assert float(m.area_faces[steep & ~on_bed].sum()) < 20


def test_ring_lip_locates_in_wall():
    p = params()
    m = mesh("asm_ring")
    v = m.vertices
    low = v[v[:, 2] < p["ring_z0"] - 0.01]
    assert len(low) > 0
    assert p["ring_z0"] - low[:, 2].min() >= 2.0
    r = np.hypot(low[:, 0], low[:, 1])
    inner = np.interp(low[:, 2], np.arange(len(p["inner_r_profile"])), p["inner_r_profile"])
    gap = inner - r
    assert gap.min() >= 0.15
    assert gap.min() <= 0.4


def test_ring_ends_at_bridge_planes():
    p = params()
    v = mesh("asm_ring").vertices
    az = az_deg(v[:, :2])
    d = np.min([np.abs((az - b + 180.0) % 360.0 - 180.0) for b in p["bridge_angles"]], axis=0)
    assert d.min() >= p["bridge_half_deg"] - 1e-6
    assert d.min() - p["bridge_half_deg"] <= 0.2


def test_ring_arcs_are_rotated_copies():
    a = mesh("ring_arc")
    b = mesh("asm_ring")
    assert abs(b.volume - 2 * a.volume) < 1e-3 * a.volume
    assert abs(b.bounds[0][0] + b.bounds[1][0]) < 0.02
    assert abs(b.bounds[0][1] + b.bounds[1][1]) < 0.02


def test_ring_assembly_z_range():
    p = params()
    b = mesh("asm_ring").bounds
    assert b[1][2] == pytest.approx(p["ring_z0"] + p["ring_arc_h"], abs=1e-4)
    assert p["bridge_top_z"] - b[1][2] == pytest.approx(0.12, abs=1e-4)
    assert b[0][2] < p["ring_z0"]


def printed_top(h, layer, first=0.2):
    t = first
    while t + layer / 2 < h - 1e-9:
        t += layer
    return t


def test_arc_height_matches_the_bridges_of_the_printed_body():
    p = params()
    bridge = printed_top(p["top_z0"], 0.16) - printed_top(p["ring_z0"], 0.16)
    assert bridge == pytest.approx(2.88)
    assert p["ring_arc_h"] == pytest.approx(bridge)
    for layer in (0.12, 0.16):
        assert abs(printed_top(p["ring_arc_h"], layer) - bridge) <= 0.04 + 1e-6
    assert printed_top(p["ring_arc_h"], 0.16) >= bridge


def test_arc_top_face_is_flat():
    p = params()
    m = mesh("asm_ring")
    face_z = p["ring_z0"] + p["ring_arc_h"]
    assert m.bounds[1][2] == pytest.approx(face_z, abs=1e-4)
    top = m.vertices[m.vertices[:, 2] > face_z - 0.3]
    assert np.all(np.abs(top[:, 2] - face_z) < 1e-4)
    r = np.linalg.norm(top[:, :2], axis=1)
    assert r.min() == pytest.approx(p["ring_in_r"], abs=0.01)
    assert r.max() == pytest.approx(p["ring_outer_r"], abs=0.01)


def test_print_arc_lies_on_its_flat_face():
    p = params()
    m = mesh("ring_arc")
    assert m.bounds[0][2] == pytest.approx(0.0, abs=1e-6)
    assert m.bounds[1][2] == pytest.approx(p["ring_arc_h"] + p["ring_lip_h"], abs=1e-4)
    bed = m.vertices[m.vertices[:, 2] < 1e-6]
    r = np.linalg.norm(bed[:, :2], axis=1)
    assert r.min() == pytest.approx(p["ring_in_r"], abs=0.01)
    assert r.max() == pytest.approx(p["ring_outer_r"], abs=0.01)


def _ring_solid_at(m, point, s=0.1):
    cube = manifold3d.Manifold.cube([s, s, s], True).translate(list(point))
    return float((m ^ cube).volume()) / s ** 3 > 0.5


def _slice_max_r(solid, z):
    loops = solid.slice(float(z)).to_polygons()
    return max(np.linalg.norm(np.asarray(q), axis=1).max() for q in loops)


def test_ring_right_under_top():
    p = params()
    ring = mesh("asm_ring")
    assert ring.bounds[1][2] == pytest.approx(p["ring_z0"] + p["ring_arc_h"], abs=1e-4)
    assert p["top_edge_z0"] - p["ring_z1"] <= 4.0
    assert mesh("asm_body").bounds[1][2] == pytest.approx(p["top_col_z1"], abs=1e-4)
    above = _manifold(mesh("asm_body")).slice(float(p["ring_z1"]) + 0.05)
    assert _slice_max_r(_manifold(mesh("asm_body")), p["ring_z1"] + 0.05) <= p["top_screw_r"] + p["boss_od"] / 2 + 1e-3
    assert len(above.to_polygons()) == len(p["top_screw_angles"])
    top = _manifold(mesh("asm_top"))
    r = p["ring_outer_r"]
    assert _slice_max_r(top, p["ring_z1"] + 0.05) == pytest.approx(r, abs=0.02)
    assert _slice_max_r(top, p["top_edge_z0"] - 0.05) == pytest.approx(r, abs=0.02)
    assert _slice_max_r(top, p["top_edge_z0"] + 1.5) < r - 0.1
    assert float(top.slice(float(p["ring_z1"] - 0.05)).area()) < float(top.slice(float(p["ring_z1"] + 0.05)).area())


def test_bridges_front_and_back():
    p = params()
    assert sorted(p["bridge_angles"]) == [-90, 90]
    ring = _manifold(mesh("asm_ring"))
    body = _manifold(mesh("asm_body"))
    z = p["fil_z"]
    r = p["ring_outer_r"] - 0.4
    half = p["bridge_half_deg"]
    for b in (-90.0, 90.0):
        for d in (0.0, half - 0.5, -(half - 0.5)):
            a = np.radians(b + d)
            q = (r * np.cos(a), r * np.sin(a), z)
            assert not _ring_solid_at(ring, q), (b, d)
            assert _ring_solid_at(body, q), (b, d)
        for s in (-1, 1):
            a = np.radians(b + s * (half + 0.5))
            assert _ring_solid_at(ring, (r * np.cos(a), r * np.sin(a), z)), (b, s)
    v = mesh("asm_ring").vertices
    az = az_deg(v[:, :2])
    for b in (-90.0, 90.0):
        near = (az - b + 180.0) % 360.0 - 180.0
        assert near[near > 0].min() == pytest.approx(-near[near < 0].max(), abs=0.05)
