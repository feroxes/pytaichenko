import functools

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overhang_area, overlap, params

TOL = 1e-3

COMPONENTS = [
    "devkit",
    "usb_plug",
    "speaker",
    "amp",
    "cap",
    "mic",
    "filament",
    "top_screws",
    "top_plugs",
    "tray_screws",
    "button",
    "speaker_screws",
    "metal_washers",
    "devkit_screws",
    "feet",
]


def test_body_watertight_fits_bed():
    m = mesh("body")
    assert m.is_watertight
    assert m.volume > 0
    bed = params()["bed"]
    ext = m.extents
    assert ext[0] < bed and ext[1] < bed


def test_body_max_diameter():
    p = params()
    b = mesh("body").bounds
    assert abs((b[1][0] - b[0][0]) - 2 * p["ring_outer_r"]) < 0.3
    assert abs((b[1][1] - b[0][1]) - 2 * p["ring_outer_r"]) < 0.3


def test_body_height():
    p = params()
    b = mesh("body").bounds
    assert abs(b[0][2]) < 1e-6
    assert abs(b[1][2] - p["top_col_z1"]) < 1e-6
    assert p["top_col_z1"] > p["H"] - p["top_region_h"]
    rim = _solid("asm_body").slice(float(p["top_z0"]) + 0.3)
    col = sum(np.pi * (p["boss_od"] / 2) ** 2 - np.pi * (p["insert_d"] / 2) ** 2 for _ in p["top_screw_angles"])
    assert float(rim.area()) == pytest.approx(col, rel=0.01)


@pytest.mark.parametrize("name", COMPONENTS)
def test_body_clear_of_components(name):
    assert overlap("asm_body", f"asm_{name}") < TOL


def test_usb_plug_passes():
    assert overlap("body", "asm_usb_plug") < TOL


def test_mic_port_open():
    assert overlap("asm_body", "asm_mic_port_probe") < TOL


def test_mic_port_is_centred_straight_ahead_under_the_front_bridge():
    p = params()
    assert p["mic_az"] == pytest.approx(-90.0)
    assert -90 in p["bridge_angles"]
    body = mesh("asm_body")
    z = p["mic_z"]
    r_in = p["mic_face_r"] - 1.0
    port_r = p["mic_port_d"] / 2
    assert first_hit(body, (0.0, -r_in, z), (0.0, -1.0, 0.0)) is None
    for dx, dz in ((port_r + 0.2, 0.0), (-(port_r + 0.2), 0.0), (0.0, port_r + 0.2), (0.0, -(port_r + 0.2))):
        assert first_hit(body, (dx, -r_in, z + dz), (0.0, -1.0, 0.0)) is not None, (dx, dz)
    y_out = -(np.interp(z, np.arange(len(p["inner_r_profile"])), p["inner_r_profile"]) + p["wall_t"] - 0.3)
    for sx in (-1, 1):
        assert not solid_at("asm_body", (sx * (port_r - 0.15), y_out, z), 0.05)
        assert solid_at("asm_body", (sx * (port_r + 0.15), y_out, z), 0.05)
    a = np.radians(-110.0)
    assert first_hit(body, (r_in * np.cos(a), r_in * np.sin(a), z), (np.cos(a), np.sin(a), 0.0)) is not None
    boot = mesh("asm_mic_boot").bounds
    assert (boot[0][0] + boot[1][0]) / 2 == pytest.approx(0.0, abs=1e-3)


@pytest.mark.parametrize("b", [20, 200])
def test_no_access_wells_under_the_top_screw_columns(b):
    p = params()
    x, y = _xy(p["top_screw_r"], b)
    m = mesh("asm_body")
    rad = np.array([np.cos(np.radians(b)), np.sin(np.radians(b))])
    tan = np.array([-rad[1], rad[0]])
    inner = p["top_screw_r"] - p["boss_od"] / 2
    for k_rad, k_tan in ((0.0, 0.0), (-1.2, 0.0), (-2.5, 0.0), (-0.3, 1.2), (-0.3, -1.2), (-1.5, 2.0), (-1.5, -2.0)):
        q = np.array([x, y]) + k_rad * rad + k_tan * tan
        hit = first_hit(m, (q[0], q[1], 10.0), (0, 0, 1))
        assert hit is not None
        z_hit = 10.0 + hit
        assert z_hit <= p["top_ledge_z0"] + 1e-3, (k_rad, k_tan)
        if k_tan == 0.0:
            r = p["top_screw_r"] + k_rad
            assert z_hit == pytest.approx(p["top_ledge_z0"] - p["chamfer_slope"] * (r - inner), abs=0.02), k_rad
        for z in np.arange(z_hit + 0.1, p["top_insert_bot_z"] - 0.05, 0.5):
            assert solid_at("asm_body", (q[0], q[1], z), 0.05), (k_rad, k_tan, z)
    assert "access_d" not in p and "access_tilt" not in p


def test_filament_channels_are_free():
    assert overlap("asm_body", "asm_filament_channel_probe") < TOL


def test_body_overhangs():
    m = mesh("body")
    p = params()
    flat_z = [p["usb_win_z1"]]
    on_bed = np.all(m.triangles[:, :, 2] <= m.bounds[0][2] + 1e-6, axis=1)
    flat = (m.face_normals[:, 2] < -0.999) & ~on_bed
    z = m.triangles[flat][:, :, 2].max(axis=1)
    area = m.area_faces[flat]
    allowed = np.array([min(abs(zz - f) for f in flat_z) < 0.01 for zz in z])
    assert area[allowed].sum() < 120
    assert overhang_area(m) - area[allowed].sum() < 300


def test_print_body_matches_assembly_orientation():
    a = mesh("asm_body")
    b = mesh("body")
    assert np.allclose(a.bounds, b.bounds, atol=1e-6)
    assert abs(a.volume - b.volume) < 1e-3


def test_top_screw_ledge_and_engagement():
    p = params()
    assert p["top_screw_ledge"] >= 3.0
    assert p["top_screw_thread"] >= p["min_thread"]
    assert p["tray_screw_thread"] >= p["min_thread"]
    assert abs((p["top_screw_ledge_z1"] - p["top_ledge_z0"]) - p["top_screw_ledge"]) < 1e-6
    assert p["top_screw_ledge_z1"] <= p["ring_z0"] - p["ring_lip_h"] - 0.2 + 1e-6
    assert p["top_boss_z0"] - p["top_col_z1"] == pytest.approx(p["top_boss_gap"], abs=1e-6)
    assert p["top_boss_gap"] > 0
    assert p["top_screw_engage"] == pytest.approx(p["top_col_z1"] - p["top_screw_tip_z"], abs=1e-9)
    assert p["top_screw_engage"] >= p["insert_len"]
    assert p["top_screw_tip_z"] - p["top_insert_bot_z"] >= p["insert_tip_gap"] - 1e-9


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def solid_at(part, point, s=0.1):
    cube = manifold3d.Manifold.cube([s, s, s], True).translate(list(point))
    return float((_solid(part) ^ cube).volume()) / s ** 3 > 0.5


def solid_all(part, points):
    return all(solid_at(part, q) for q in points)


def slice_area(part, z):
    return float(_solid(part).slice(z).area())


def _xy(r, a):
    return r * np.cos(np.radians(a)), r * np.sin(np.radians(a))


def test_tray_bosses_present_and_start_above_pad():
    p = params()
    pad = p["tray_pad_t"]
    hole_end = pad + p["tray_boss_hole_depth"]
    for a in p["tray_screw_angles"]:
        x, y = _xy(p["tray_screw_r"], a)
        ring = [(x + dx, y + dy) for dx, dy in [(2.6, 0), (-2.6, 0), (0, 2.6), (0, -2.6)]]
        for qx, qy in ring:
            assert not solid_at("asm_body", (qx, qy, pad - 0.2))
            assert solid_at("asm_body", (qx, qy, pad + 0.3))
            assert solid_at("asm_body", (qx, qy, hole_end + 0.3))
        assert not solid_at("asm_body", (x, y, pad + 4.0))
        assert not solid_at("asm_body", (x, y, hole_end - 0.3))


def _last_z(part, x, y, z_lo, z_hi, want):
    last = None
    for z in np.arange(z_lo, z_hi, 0.1):
        if solid_at(part, (x, y, z), 0.05) == want:
            last = z
    return last


def test_tray_boss_skin():
    p = params()
    x, y = _xy(p["tray_screw_r"], p["tray_screw_angles"][0])
    z0 = p["tray_pad_t"] + p["tray_boss_hole_depth"]
    top = _last_z("asm_body", x, y + 2.8, z0, z0 + 8.0, True)
    assert top is not None
    tip = _last_z("asm_body", x, y, z0 - 1.0, top, False)
    assert tip is not None
    assert top - tip >= 1.6
    assert top - z0 >= 1.6


@pytest.mark.parametrize("b", [20, 200])
def test_top_screw_column_rises_from_the_ledge_with_an_insert_hole_from_above(b):
    p = params()
    assert b in p["top_screw_angles"]
    x, y = _xy(p["top_screw_r"], b)
    z0, z1 = p["top_ledge_z0"], p["top_col_z1"]
    r_out = p["boss_od"] / 2
    r_in = p["insert_d"] / 2
    for z in np.linspace(z0 + 0.1, z1 - 0.1, 12):
        for ang in range(0, 360, 45):
            c, s_ = np.cos(np.radians(ang)), np.sin(np.radians(ang))
            assert solid_at("asm_body", (x + (r_out - 0.15) * c, y + (r_out - 0.15) * s_, z), 0.05), (z, ang)
            if z > p["top_screw_ledge_z1"] + 0.1:
                assert not solid_at("asm_body", (x + (r_out + 0.15) * c, y + (r_out + 0.15) * s_, z), 0.05), (z, ang)
    bot = p["top_insert_bot_z"]
    assert p["top_insert_hole_depth"] == pytest.approx(z1 - bot, abs=1e-9)
    for z in np.arange(bot + 0.1, z1, 0.5):
        assert not solid_at("asm_body", (x, y, z), 0.05), z
        assert not solid_at("asm_body", (x + r_in - 0.1, y, z), 0.05), z
        assert solid_at("asm_body", (x + r_in + 0.1, y, z), 0.05), z
    assert solid_at("asm_body", (x, y, bot - 0.1), 0.05)
    assert not solid_at("asm_body", (x, y, z1 + 0.1), 0.05)
    loops = [np.asarray(q) for q in _solid("asm_body").slice(float(z1) - 1.0).to_polygons()]
    hole = [q for q in loops if np.linalg.norm(q.mean(axis=0) - [x, y]) < 0.5 and np.linalg.norm(q - [x, y], axis=1).max() < 2.0]
    assert len(hole) == 1
    assert np.linalg.norm(hole[0] - [x, y], axis=1).max() == pytest.approx(r_in, abs=0.01)
    assert (p["boss_od"] - p["insert_d"]) / 2 >= 1.6


@pytest.mark.parametrize("b", [20, 200])
def test_top_screw_column_keeps_its_clearances(b):
    p = params()
    x, y = _xy(p["top_screw_r"], b)
    zone = manifold3d.Manifold.cylinder(30.0, p["boss_od"] / 2 + 0.01, p["boss_od"] / 2 + 0.01, 96)
    col = _solid("asm_body") ^ zone.translate([x, y, p["top_screw_ledge_z1"] + 0.05])
    assert float(col.volume()) > 0.9 * np.pi * (p["boss_od"] / 2) ** 2 * (p["top_col_z1"] - p["top_screw_ledge_z1"] - 1.0)
    gaps = {
        "asm_ring": p["ring_in_clr"],
        "asm_top": p["top_boss_gap"],
        "asm_speaker": 2.0,
        "asm_speaker_gasket": 2.0,
        "asm_speaker_screws": 2.0,
        "asm_metal_washers": 2.0,
        "asm_button": 2.0,
        "asm_button_cap": 2.0,
        "asm_filament": 2.0,
        "asm_filament_channel_probe": 2.0,
        "asm_wire_keepout": 2.0,
        "asm_top_plugs": 2.0,
    }
    for name, want in gaps.items():
        assert col.min_gap(_solid(name), 10.0) >= want - 1e-3, name
    assert col.min_gap(_solid("asm_ring"), 10.0) == pytest.approx(p["ring_in_clr"], abs=0.02)
    assert col.min_gap(_solid("asm_top"), 10.0) == pytest.approx(p["top_boss_gap"], abs=0.02)
    assert p["top_screw_r"] + p["boss_od"] / 2 <= p["top_skirt_in_r"] - 0.5
    assert p["top_screw_r"] - p["boss_od"] / 2 >= p["spk_flange_half"] + p["spk_seat_flange_clr"] + 0.5


@pytest.mark.parametrize("b", [20, 200])
def test_top_screw_column_foot_prints_without_supports(b):
    p = params()
    zb = p["top_ledge_z0"]
    inner = p["top_screw_r"] - p["boss_od"] / 2
    assert p["top_corbel_z0"] == pytest.approx(zb - p["chamfer_slope"] * (p["wedge_out_r"] - inner), abs=1e-3)
    assert p["chamfer_slope"] >= 1.0
    m = mesh("body")
    c = m.triangles.mean(axis=1)
    a = np.radians(b)
    rad = c[:, 0] * np.cos(a) + c[:, 1] * np.sin(a)
    tan = -c[:, 0] * np.sin(a) + c[:, 1] * np.cos(a)
    zone = ((rad > inner - 2.0) & (rad < p["ring_outer_r"] + 1.0) & (np.abs(tan) < p["boss_od"] / 2 + 1.5)
            & (c[:, 2] > p["top_corbel_z0"] - 3.0) & (c[:, 2] < p["top_screw_ledge_z1"] + 0.1))
    down = m.face_normals[:, 2]
    assert float(m.area_faces[zone & (down < -np.cos(np.radians(45.0)))].sum()) < 0.5
    assert float(m.area_faces[zone & (down < -0.5)].sum()) > 50.0
    x, y = _xy(p["top_screw_r"], b)
    for ang in range(0, 360, 30):
        q = (x + 3.0 * np.cos(np.radians(ang)), y + 3.0 * np.sin(np.radians(ang)), zb - 0.2)
        assert solid_at("asm_body", q, 0.05), ang
    rad_v = np.array([np.cos(a), np.sin(a)])
    wall = np.interp(p["top_corbel_z0"], np.arange(len(p["inner_r_profile"])), p["inner_r_profile"])
    for z in np.linspace(p["top_corbel_z0"] + 1.0, zb - 0.2, 8):
        reach = p["wedge_out_r"] - (z - p["top_corbel_z0"]) / p["chamfer_slope"]
        q = rad_v * (reach + 0.2)
        assert solid_at("asm_body", (q[0], q[1], z), 0.05), z
        if reach - 0.3 < wall - 0.2:
            q = rad_v * (reach - 0.3)
            assert not solid_at("asm_body", (q[0], q[1], z), 0.05), z
    assert wall + 0.5 < p["wedge_out_r"] < wall + p["wall_t"] - 0.5
    box = manifold3d.Manifold.cube([wall - 0.05 - (inner - 1.0), p["boss_od"] + 3.0, zb - p["top_corbel_z0"] + 1.0])
    box = box.translate([inner - 1.0, -(p["boss_od"] + 3.0) / 2, p["top_corbel_z0"] - 1.0]).rotate([0, 0, b])
    foot = _solid("asm_body") ^ box
    assert float(foot.volume()) > 100.0
    for name, want in (("asm_speaker", 3.0), ("asm_metal_washers", 3.0), ("asm_speaker_screws", 3.0),
                       ("asm_speaker_gasket", 3.0), ("asm_ring", 2.0), ("asm_top", 2.0), ("asm_button", 2.0),
                       ("asm_filament", 2.0), ("asm_filament_channel_probe", 2.0), ("asm_wire_keepout", 2.0),
                       ("asm_mic", 2.0), ("asm_cap", 2.0), ("asm_amp", 2.0), ("asm_devkit", 2.0)):
        assert foot.min_gap(_solid(name), 10.0) >= want, name
    spk = _solid("asm_speaker")
    swept = spk.minkowski_sum(manifold3d.Manifold.cube([0.001, 0.001, 30.0]))
    assert float((swept ^ _solid("asm_body")).volume()) < TOL


def test_bottom_rim_is_printable():
    p = params()
    inner0 = p["inner_r_profile"][0]
    area = slice_area("asm_body", 0.3)
    ring_len = 2 * np.pi * (inner0 + 0.7)
    assert area / ring_len > 1.0


@pytest.mark.parametrize("az", [45, 120])
@pytest.mark.parametrize("z", [6, 20, 35, 50])
def test_wall_thickness_floor(z, az):
    p = params()
    inner = p["inner_r_profile"][z]
    wall = p["wall_t"]
    assert abs(wall - 2.4) < 1e-9
    assert not solid_at("asm_body", (*_xy(inner - 0.1, az), z))
    assert solid_at("asm_body", (*_xy(inner + 0.1, az), z))
    assert solid_at("asm_body", (*_xy(inner + wall - 0.1, az), z))
    assert not solid_at("asm_body", (*_xy(inner + wall + 0.1, az), z))


@pytest.mark.parametrize("a", [60, 150, 270])
@pytest.mark.parametrize("z", [3, 5, 8, 10])
def test_tray_ribs_stay_inside_wall(a, z):
    p = params()
    assert a in p["tray_screw_angles"]
    outer = p["inner_r_profile"][z] + p["wall_t"]
    inner = p["inner_r_profile"][z]
    assert solid_at("asm_body", (*_xy(inner - 0.3, a), z))
    assert solid_at("asm_body", (*_xy(outer - 0.1, a), z))
    assert not solid_at("asm_body", (*_xy(outer + 0.1, a), z))
    assert solid_at("asm_body", (*_xy(inner + 0.1, a), z))


def test_rib_bridges_boss_to_wall_at_every_height():
    p = params()
    pad = p["tray_pad_t"]
    a = p["tray_screw_angles"][0]
    for z in (pad + 0.3, 5.0, 8.0, 10.0):
        r_in = np.interp(z, range(len(p["inner_r_profile"])), p["inner_r_profile"])
        for r in np.arange(p["tray_screw_r"] + 4.2, r_in + 0.2, 0.5):
            assert solid_at("asm_body", (*_xy(r, a), z))


def test_profile_is_a_bowl_on_a_heel_over_a_narrower_plinth():
    p = params()
    prof = np.array(p["inner_r_profile"])
    ring_z0 = int(p["ring_z0"])
    d = np.diff(prof[: ring_z0 + 1])
    assert np.all(d >= -1e-9)
    bowl = d[int(np.ceil(p["edge_zt"])):]
    assert np.all(bowl > 0)
    assert np.all(np.diff(bowl) <= 1e-9)
    assert d[-1] < 0.01
    assert abs(prof[ring_z0] + p["wall_t"] - p["ring_outer_r"]) < 1e-3
    assert prof[0] + p["wall_t"] == pytest.approx(p["plinth_r"], abs=1e-3)
    assert np.all(prof[: int(p["plinth_h"]) + 1] == pytest.approx(prof[0], abs=1e-6))
    hc, bc = np.array(p["heel_c"]), np.array(p["bowl_c"])
    assert np.linalg.norm(hc - bc) == pytest.approx(p["bowl_rad"] - p["heel_r"], abs=1e-3)
    assert p["ring_outer_r"] - p["plinth_r"] == pytest.approx(p["plinth_inset"], abs=1e-3)
    assert p["body_face_r1"] - prof[0] >= p["lip_face_need"] - 1e-3
    assert p["lip_face_need"] == pytest.approx(
        p["tray_flange_lip"] + p["lip_reach"] + p["lip_nose_r"] + 0.4, abs=1e-3)


def _max_r(part, z):
    loops = _solid(part).slice(float(z)).to_polygons()
    return max(np.linalg.norm(np.asarray(q), axis=1).max() for q in loops)


def test_plinth_is_recessed_under_a_heel_with_a_crease():
    p = params()
    assert 6 <= p["plinth_band"] <= 8
    assert p["plinth_h"] - p["tray_bot_z"] == pytest.approx(p["plinth_band"], abs=1e-6)
    assert p["heel_step"] >= 3
    assert p["edge_out_r"] - p["plinth_r"] == pytest.approx(p["heel_step"], abs=1e-3)
    for z in (0.3, p["plinth_h"] / 2, p["plinth_h"] - 0.2):
        assert _max_r("asm_body", z) == pytest.approx(p["plinth_r"], abs=0.05), z
    assert _max_r("asm_body", p["edge_zt"] + 0.3) >= p["plinth_r"] + p["heel_step"] - 0.05
    tv = mesh("asm_tray").vertices
    assert np.hypot(tv[:, 0], tv[:, 1]).max() <= p["plinth_r"] - p["tray_flange_lip"] + 1e-3
    above = _max_r("asm_body", p["plinth_h"] + 0.3) - p["plinth_r"]
    assert 0.5 * 0.3 < above <= 0.3
    m = mesh("asm_body")
    for a in (45, 135, 225, 315):
        c, s_ = np.cos(np.radians(a)), np.sin(np.radians(a))
        for k in (0.5, 1.5, 2.5, p["heel_step"] - 0.5):
            r = p["plinth_r"] + k
            hit = first_hit(m, (r * c, r * s_, -1.0), (0, 0, 1))
            assert hit is not None
            z = hit - 1.0
            assert p["plinth_h"] < z < p["edge_zt"] + 0.01, (a, k, z)
        r = p["plinth_r"] + 0.2
        assert first_hit(m, (r * c, r * s_, p["plinth_h"] - 0.5), (c, s_, 0)) is None
    flat = m.face_normals[:, 2] < -0.999
    zs = m.triangles[flat][:, :, 2].max(axis=1)
    assert not np.any(np.abs(zs - p["plinth_h"]) < 0.01)
    assert 45 < p["edge_phi"] < p["edge_t_phi"] <= 90


@pytest.mark.parametrize("z", [10, 30, 50])
def test_outer_profile_follows_params(z):
    p = params()
    expect = p["inner_r_profile"][z] + p["wall_t"]
    section = _solid("asm_body").slice(float(z))
    (x0, y0), (x1, _) = np.array(section.bounds()).reshape(2, 2)
    assert abs(x1 - expect) < 0.05
    assert abs(x0 + expect) < 0.05
    assert abs(y0 + expect) < 0.3


def test_bridge_constants_are_shared():
    p = params()
    for name in [
        "bridge_in_r",
        "bar_w",
        "fil_contact_w",
        "fil_contact_t",
        "wedge_out_r",
        "chan_d",
        "chan_slot_l",
        "fil_bend_r",
        "bridge_half_deg",
        "bridge_top_z",
        "chamfer_slope",
    ]:
        assert name in p
    assert p["bridge_in_r"] == pytest.approx(p["ring_in_r"], abs=1e-6)
    assert p["chan_p2"][1] - p["chan_d"] / 2 >= 0.5
    for a in p["top_screw_angles"]:
        for b in p["bridge_angles"]:
            gap = abs((a - b + 180.0) % 360.0 - 180.0)
            assert gap > p["bridge_half_deg"] + np.degrees((p["boss_od"] / 2 + 1.0) / p["top_screw_r"]), (a, b)
    assert p["wedge_out_r"] < p["ring_outer_r"] - 1
    assert p["bridge_top_z"] == p["top_z0"]


def _y_projection(solid):
    return solid.rotate([90, 0, 0]).project()


def _wall_region():
    p = params()
    y0 = min(p["inner_r_profile"][:13]) - 2.0
    return manifold3d.Manifold.cube([16, p["wall_t"] + 4.0 + 2.0, 11.5], False).translate([-8, y0, 0.5])


def _window_free():
    region = _wall_region()
    blocked = _y_projection(_solid("asm_body") ^ region)
    frame = _y_projection(region)
    return frame - blocked


def test_usb_window_is_a_closed_rectangle():
    p = params()
    free = _window_free()
    w, h = p["usb_win_w"], p["usb_win_h"]
    assert w == pytest.approx(p["usb_plug_w"] + 0.3)
    assert h == pytest.approx(p["usb_plug_h"] + 0.5)
    assert free.area() == pytest.approx(w * h, rel=1e-3)
    (x0, y0), (x1, y1) = np.array(free.bounds()).reshape(2, 2)
    assert (x1 - x0) == pytest.approx(w, abs=1e-3)
    assert (y1 - y0) == pytest.approx(h, abs=1e-3)
    assert -y1 == pytest.approx(p["usb_z"] - p["usb_plug_h"] / 2 - 0.15, abs=1e-3)
    assert -y0 == pytest.approx(p["usb_z"] + p["usb_plug_h"] / 2 + 0.35, abs=1e-3)
    assert -y1 == pytest.approx(p["usb_win_z0"], abs=1e-3)
    assert -y0 == pytest.approx(p["usb_win_z1"], abs=1e-3)
    assert -y1 >= 1.5
    assert len(free.to_polygons()) == 1


def test_usb_window_roof_is_flat():
    p = params()
    zc = p["usb_win_z1"]
    for x in (-5.0, 0.0, 5.0):
        for y in (p["inner_r_profile"][int(zc)] + d for d in (0.6, 1.2, 1.8)):
            r = np.hypot(x, y)
            if r < p["inner_r_profile"][int(zc)] + 0.1:
                continue
            assert solid_at("asm_body", (x, y, zc + 0.1), 0.05)
            assert not solid_at("asm_body", (x, y, zc - 0.1), 0.05)


def test_usb_plug_goes_through_and_reaches_the_receptacle_face():
    p = params()
    plug = _solid("asm_usb_plug")
    assert plug.bounding_box()[1] == pytest.approx(p["usb_tip_y"], abs=1e-3)
    seg = manifold3d.Manifold.cube([0.001, 30.0, 0.001], False)
    swept = plug.minkowski_sum(seg)
    assert float((swept ^ _solid("asm_body")).volume()) < TOL
    assert float((swept ^ _solid("asm_devkit")).volume()) < TOL


def test_usb_residual_open_area():
    plug = _y_projection(_solid("asm_usb_plug"))
    residual = _window_free() - plug
    assert residual.area() <= 12.0
    assert residual.area() > 0


def _chan_point(p, u):
    p0 = np.array(p["chan_p0"])
    p1 = np.array(p["chan_p1"])
    t = np.array([np.sin(np.radians(p["bridge_half_deg"])), -np.cos(np.radians(p["bridge_half_deg"])), 0.0])
    d = np.array(p["chan_dir"])
    rb = p["fil_bend_r"]
    if u <= p["chan_bend_len"]:
        th = u / rb
        q, tan = p0 + rb * np.sin(th) * t + rb * (1 - np.cos(th)) * d, np.cos(th) * t + np.sin(th) * d
    else:
        q, tan = p1 + (u - p["chan_bend_len"]) * d, d
    return q, tan / np.linalg.norm(tan)


def _to_world(b, s, v):
    v = np.array([v[0], s * v[1], v[2]])
    a = np.radians(b)
    return np.array([v[0] * np.cos(a) - v[1] * np.sin(a), v[0] * np.sin(a) + v[1] * np.cos(a), v[2]])


def _chan_frame(p, b, s, u):
    q, tan = _chan_point(p, u)
    inward = -np.array([q[0], q[1], 0.0]) / np.hypot(q[0], q[1])
    inward = inward - inward.dot(tan) * tan
    inward /= np.linalg.norm(inward)
    side = np.cross(tan, inward)
    return _to_world(b, s, q), _to_world(b, s, tan), _to_world(b, s, inward), _to_world(b, s, side)


def _chan_us(p):
    return list(np.linspace(0.3, p["chan_bend_len"], 7)) + list(
        np.linspace(p["chan_bend_len"] + 0.5, p["chan_len"] - 0.3, 6)
    )


@pytest.mark.parametrize("b", [-90, 90])
@pytest.mark.parametrize("s", [-1, 1])
def test_filament_channel_bend_and_straight_contact(b, s):
    p = params()
    assert p["fil_bend_r"] >= p["fil_bend_r_min"] + (p["chan_d"] - p["fil_d"]) - 1e-9
    assert p["chan_straight"] >= p["fil_trans"] + p["fil_contact"]
    assert p["fil_straight_lit"] >= p["fil_trans"]
    assert 1.6 <= p["chan_d"] <= 1.8
    p0 = np.array(p["chan_p0"])
    assert np.hypot(p0[0], p0[1]) == pytest.approx(p["filament_r"], abs=1e-3)
    assert p0[2] == pytest.approx(p["fil_z"], abs=1e-6)
    assert np.degrees(np.arctan2(p0[1], p0[0])) == pytest.approx(p["bridge_half_deg"], abs=1e-3)
    _, t0 = _chan_point(p, 0.0)
    assert abs(t0.dot(p0 / np.linalg.norm(p0))) < 1e-3
    assert overlap("asm_body", "asm_filament_channel_probe") < TOL
    assert overlap("asm_top", "asm_filament_channel_probe") < TOL
    assert overlap("asm_ring", "asm_filament_channel_probe") < TOL
    end, tan, inward, _ = _chan_frame(p, b, s, p["chan_len"])
    assert end[2] < p["ring_z0"] - p["fil_contact"]
    for k in (0.0, 0.9, 1.5):
        assert not solid_at("asm_body", end + k * tan, 0.05)
        assert not solid_at("asm_body", end + 1.2 * inward, 0.05)


@pytest.mark.parametrize("b", [-90, 90])
@pytest.mark.parametrize("s", [-1, 1])
def test_channel_is_open_toward_the_inside_and_walled_outside(b, s):
    p = params()
    half = p["chan_d"] / 2
    for u in _chan_us(p):
        q, tan, inward, side = _chan_frame(p, b, s, u)
        assert not solid_at("asm_body", q, 0.05), u
        for k in (0.5, 1.0, 1.6):
            assert not solid_at("asm_body", q + k * inward, 0.05), (u, k)
        assert solid_at("asm_body", q - (half + 0.3) * inward, 0.05), u
        r = np.hypot(q[0], q[1])
        outer = p["ring_outer_r"] if q[2] >= p["ring_z0"] else np.interp(
            q[2], np.arange(len(p["inner_r_profile"])), p["inner_r_profile"]) + p["wall_t"]
        assert outer - r - half >= 0.5 - 1e-3, u


def _groove_exit_point(p, b, s, t):
    ang = np.radians(p["bridge_half_deg"]) - t / p["filament_r"]
    local = np.array([p["filament_r"] * np.cos(ang), p["filament_r"] * np.sin(ang), p["fil_z"]])
    inward = -np.array([np.cos(ang), np.sin(ang), 0.0])
    return _to_world(b, s, local), _to_world(b, s, inward)


@pytest.mark.parametrize("b", [-90, 90])
@pytest.mark.parametrize("s", [-1, 1])
def test_filament_tail_lays_into_the_channel_when_the_arc_end_moves_inward_with_it(b, s):
    p = params()
    body = _solid("asm_body")
    rod = manifold3d.Manifold.sphere(p["chan_probe_d"] / 2, 24)
    reach = 2.5
    handover = p["chan_open_l"] - 0.5
    lay_in = [_groove_exit_point(p, b, s, t) for t in np.linspace(0.1, handover, 9)]
    for u in np.linspace(handover, p["chan_len"] - 0.3, 13):
        q, _, inward, _ = _chan_frame(p, b, s, u)
        lay_in.append((q, inward))
    for q, inward in lay_in:
        a = rod.translate(list(q))
        c = rod.translate(list(q + reach * inward))
        assert float((manifold3d.Manifold.batch_hull([a, c]) ^ body).volume()) < TOL, q


@pytest.mark.parametrize("b", [-90, 90])
@pytest.mark.parametrize("s", [-1, 1])
def test_channel_mouth_roof_is_open_over_the_first_mm_and_closed_after_it(b, s):
    p = params()
    top = p["bridge_top_z"]
    for u in np.linspace(0.1, p["chan_open_l"] - 0.3, 8):
        q, _, inward, _ = _chan_frame(p, b, s, u)
        for z in np.arange(q[2], top - 0.1, 0.25):
            assert not solid_at("asm_body", (q[0], q[1], z), 0.05), (u, z)
    for u in np.linspace(p["chan_open_l"] + 1.2, p["chan_open_l"] + 4.0, 5):
        q, _, _, _ = _chan_frame(p, b, s, u)
        assert solid_at("asm_body", (q[0], q[1], top - 0.15), 0.05), u


@pytest.mark.parametrize("b", [-90, 90])
@pytest.mark.parametrize("s", [-1, 1])
def test_channel_mouth_keeps_its_outer_skin_and_the_bar_between_the_channels(b, s):
    p = params()
    top = p["bridge_top_z"]
    half = p["chan_d"] / 2
    for u in np.linspace(0.1, p["chan_open_l"], 8):
        q, _, inward, _ = _chan_frame(p, b, s, u)
        out = np.array([-inward[0], -inward[1], 0.0])
        out /= np.linalg.norm(out)
        r0 = float(np.hypot(q[0], q[1]))
        assert p["ring_outer_r"] - r0 - half >= 0.5 - 1e-3, u
        for z in (q[2], p["fil_z"] + 0.6, top - 0.15):
            for dr in np.arange(half + 0.05, p["ring_outer_r"] - r0 - 0.05, 0.1):
                assert solid_at("asm_body", (q[0] + dr * out[0], q[1] + dr * out[1], z), 0.05), (u, z, dr)
    for y in (-2.5, 0.0, 2.5):
        for x in np.arange(p["bridge_in_r"] + 0.2, p["ring_outer_r"] - 0.2, 0.4):
            for z in (p["fil_z"] - 0.5, p["fil_z"], top - 0.15):
                assert solid_at("asm_body", _to_world(b, 1, (x, y, z)), 0.05), (x, y, z)


@pytest.mark.parametrize("b", [20, 200])
def test_top_screw_bar_and_wedge_reach_the_wall_under_the_ring_lip(b):
    p = params()
    assert b in p["top_screw_angles"]
    zb = p["top_ledge_z0"]
    a = np.radians(b)
    rad = np.array([np.cos(a), np.sin(a), 0.0])
    tan = np.array([-np.sin(a), np.cos(a), 0.0])
    r0 = p["top_screw_r"] + p["boss_od"] / 2 + 0.3
    wall = np.interp(zb, np.arange(len(p["inner_r_profile"])), p["inner_r_profile"])
    assert p["wedge_out_r"] > wall + 1.0
    for r in np.arange(r0, wall + 1.0, 0.4):
        for y in (-p["bar_w"] / 2 + 0.2, 0.0, p["bar_w"] / 2 - 0.2):
            for dz in (0.3, 1.5, p["top_screw_ledge"] - 0.3):
                assert solid_at("asm_body", r * rad + y * tan + np.array([0, 0, zb + dz]), 0.05), (r, y, dz)
            assert solid_at("asm_body", r * rad + y * tan + np.array([0, 0, zb - 0.3]), 0.05), (r, y)
    wedge_low = zb - p["chamfer_slope"] * (p["inner_r_profile"][int(zb)] - p["top_screw_r"]) + 0.3
    assert solid_at("asm_body", (p["inner_r_profile"][int(zb)] - 0.2) * rad + np.array([0, 0, wedge_low]), 0.05)
    ring = _solid("asm_ring")
    bar = manifold3d.Manifold.cube([p["ring_outer_r"] - p["top_screw_r"], p["bar_w"], p["top_screw_ledge"]], False)
    bar = bar.translate([p["top_screw_r"], -p["bar_w"] / 2, zb]).rotate([0, 0, b])
    lip_bottom = mesh("asm_ring").bounds[0][2]
    assert lip_bottom - p["top_screw_ledge_z1"] >= 0.2 - 1e-6
    assert float((ring ^ bar.translate([0, 0, 0.19])).volume()) < TOL
    probe = _solid("asm_filament_channel_probe")
    bar = manifold3d.Manifold.cube([p["ring_outer_r"] - p["top_screw_r"], p["bar_w"], p["top_screw_ledge"] + 20], False)
    bar = bar.translate([p["top_screw_r"], -p["bar_w"] / 2, zb - 20]).rotate([0, 0, b])
    assert float((probe ^ bar).volume()) < TOL


def test_usb_plug_matches_the_measured_overmold():
    p = params()
    assert p["usb_plug_w"] == pytest.approx(10.0)
    assert p["usb_plug_h"] == pytest.approx(6.2)
    assert p["usb_plug_len"] == pytest.approx(13.9)
    b = mesh("asm_usb_plug").bounds
    assert b[1][0] - b[0][0] == pytest.approx(10.0, abs=1e-4)
    assert b[1][2] - b[0][2] == pytest.approx(6.2, abs=1e-4)
    assert b[1][1] - b[0][1] == pytest.approx(13.9, abs=1e-4)
    assert (b[0][2] + b[1][2]) / 2 == pytest.approx(p["usb_z"], abs=1e-4)


def test_usb_plug_sits_in_the_window_with_the_wanted_gaps():
    p = params()
    free = _window_free()
    plug = _y_projection(_solid("asm_usb_plug"))
    (px0, py0), (px1, py1) = np.array(plug.bounds()).reshape(2, 2)
    (wx0, wy0), (wx1, wy1) = np.array(free.bounds()).reshape(2, 2)
    assert px0 - wx0 == pytest.approx(0.15, abs=2e-3)
    assert wx1 - px1 == pytest.approx(0.15, abs=2e-3)
    assert (-py1) - (-wy1) == pytest.approx(0.15, abs=2e-3)
    assert (-wy0) - (-py0) == pytest.approx(0.35, abs=2e-3)
    assert p["usb_win_z0"] >= 1.5
    assert (free - plug).area() <= 12.0


@pytest.mark.parametrize("b", [-90, 90])
@pytest.mark.parametrize("s", [-1, 1])
def test_filament_dummy_ends_in_flat_contacts_and_a_round_lit_body(b, s):
    p = params()
    t, w = p["fil_contact_t"], p["fil_contact_w"]
    for k in (1.0, 2.5, 4.5):
        q, tan, inward, side = _chan_frame(p, b, s, p["fil_excess"] + k)
        assert solid_at("asm_filament", q, 0.02)
        assert solid_at("asm_filament", q + (w / 2 - 0.05) * side, 0.02)
        assert not solid_at("asm_filament", q + (w / 2 + 0.1) * side, 0.02)
        assert not solid_at("asm_filament", q + (t / 2 + 0.05) * inward, 0.02)
        assert not solid_at("asm_filament", q - (t / 2 + 0.05) * inward, 0.02)
    q, tan, _, _ = _chan_frame(p, b, s, p["chan_len"])
    assert not solid_at("asm_filament", q + 0.1 * tan, 0.02)
    q, _, inward, _ = _chan_frame(p, b, s, p["fil_excess"] - 1.0)
    assert solid_at("asm_filament", q + (p["fil_d"] / 2 - 0.1) * inward, 0.02)
    fz = p["fil_z"]
    mx, my = _xy(p["filament_r"], p["bridge_angles"][0] + 90)
    assert solid_at("asm_filament", (mx, my, fz + p["fil_d"] / 2 - 0.1), 0.02)
    assert not solid_at("asm_filament", (mx, my, fz + p["fil_d"] / 2 + 0.1), 0.02)
    m = mesh("asm_filament")
    zs = m.vertices[:, 2]
    assert zs.max() == pytest.approx(fz + p["fil_d"] / 2, abs=0.02)
    assert zs.min() < p["ring_z0"] - p["fil_contact"]


def test_plinth_transition_smooth():
    p = params()
    zs = np.arange(0.1, p["ring_z0"] - 0.05, 0.25)
    rs = np.array([_max_r("asm_body", z) for z in zs])
    slope = np.diff(rs) / np.diff(zs)
    assert np.all(slope >= -1e-3)
    assert np.all(slope <= np.tan(np.radians(45.0)))
    low = zs < p["plinth_h"] - 0.05
    assert np.all(np.abs(rs[low] - p["plinth_r"]) < 0.02)
    start = zs[rs > p["plinth_r"] + 0.02].min()
    end = zs[rs >= p["edge_out_r"] - 0.02].min()
    assert end - start >= 10.0
    heel = (zs > start + 0.5) & (zs < end - 0.5)
    hs = slope[heel[:-1]]
    assert np.all(np.diff(hs) <= 0.02)
    assert hs.max() - hs.min() > 0.5
    assert p["edge_out_r"] - p["plinth_r"] >= 3.0
    assert p["ring_outer_r"] - p["plinth_r"] >= 3.0
    tv = mesh("asm_tray").vertices
    assert np.hypot(tv[:, 0], tv[:, 1]).max() <= p["plinth_r"] + 1e-3
    assert p["plinth_h"] - p["tray_bot_z"] == pytest.approx(p["plinth_band"], abs=1e-6)
