import functools

import manifold3d
import numpy as np
import pytest

from conftest import _manifold, first_hit, mesh, overlap, params
from test_top import CLEAR_OF, TOP_OVERHANG_MAX

TOL = 1e-3
CS = manifold3d.CrossSection


@functools.lru_cache(maxsize=None)
def _solid(part):
    return _manifold(mesh(part))


def _disc(r, c=(0.0, 0.0), fn=360):
    return CS.circle(r, fn).translate(list(c))


def _annulus(r0, r1, c=(0.0, 0.0)):
    return _disc(r1, c) - _disc(r0, c)


def _signed_area(q):
    x, y = q[:, 0], q[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(np.roll(x, -1), y))


def _centroid(q):
    x, y = q[:, 0], q[:, 1]
    x1, y1 = np.roll(x, -1), np.roll(y, -1)
    cr = x * y1 - x1 * y
    a = cr.sum() / 2
    return np.array([((x + x1) * cr).sum() / (6 * a), ((y + y1) * cr).sum() / (6 * a)])


def _button_xy(p):
    a = np.radians(p["button_az"])
    return np.array([p["button_r"] * np.cos(a), p["button_r"] * np.sin(a)])


def _features(p):
    yield _button_xy(p), p["button_hole_d"] / 2
    for c in p["top_screw_xy"]:
        yield np.array(c), p["top_plug_d"] / 2


def _near_feature(p, c, pad=0.5):
    return any(np.linalg.norm(np.asarray(c) - f) < r + pad for f, r in _features(p))


def _face_holes(part, z):
    p = params()
    return [q for q in _solid(part).slice(z).to_polygons()
            if _signed_area(q) < 0 and not _near_feature(p, _centroid(q))]


def _grille_holes(part):
    p = params()
    return [q for q in _face_holes(part, p["grille_bot_z"] + 0.3) if np.linalg.norm(_centroid(q)) < p["spk_recess_r"]]


def _cs(loops):
    return CS([np.asarray(q)[::-1].tolist() for q in loops], manifold3d.FillRule.Positive)


def _feature_discs(p, pad):
    out = CS()
    for c, r in _features(p):
        out = out + _disc(r + pad, tuple(c), 96)
    return out


def _open_fraction(lid="top"):
    p = params()
    cone = _disc(p["spk_open_d"] / 2)
    return 1 - float((_solid(f"asm_{lid}").slice(p["grille_bot_z"] + 0.3) ^ cone).area()) / float(cone.area())


def test_no_through_hole_outside_the_speaker():
    p = params()
    depth = p["sf_dimple_depth"]
    band = _annulus(p["face_thru_r"] + 0.01, p["top_face_r"]) - _feature_discs(p, 0.05)
    sl = _solid("asm_top").slice(p["H"] - depth - 0.05)
    assert float((band - sl).area()) < 1e-3
    holes = _solid("asm_top").slice(p["H"] - depth - 0.05).to_polygons()
    for q in holes:
        if _signed_area(q) < 0:
            c = _centroid(q)
            assert np.linalg.norm(q, axis=1).max() <= p["face_thru_r"] + 0.01 or _near_feature(p, c, 0.1), c
    m = mesh("asm_top")
    for r in np.linspace(p["face_thru_r"] + 0.3, p["top_face_r"] - 0.2, 6):
        for a in np.radians(np.arange(0, 360, 7.5)):
            x, y = r * np.cos(a), r * np.sin(a)
            if _near_feature(p, (x, y), 0.3):
                continue
            t = first_hit(m, (x, y, p["H"] + 1), (0, 0, -1))
            assert t is not None and t <= 1 + depth + 0.02, (x, y, t)


def test_open_area_over_the_speaker():
    assert _open_fraction() >= 0.30


def test_lid_overhangs():
    p = params()
    m = mesh("top")
    normals = m.face_normals
    steep = normals[:, 2] < -np.cos(np.radians(45))
    on_bed = np.all(m.triangles[:, :, 2] <= m.bounds[0][2] + 1e-6, axis=1)
    ceiling = np.all(np.abs(m.triangles[:, :, 2] - p["sf_dimple_depth"]) < 1e-4, axis=1)
    assert float(m.area_faces[steep & ~on_bed & ~ceiling].sum()) < TOP_OVERHANG_MAX


def test_plug_and_button_rims_stay_flat():
    p = params()
    face = _solid("asm_top").slice(p["H"] - 0.05)
    for c in p["top_screw_xy"]:
        ring = _annulus(p["top_plug_d"] / 2 + 0.05, p["top_plug_d"] / 2 + p["top_plug_rim"], tuple(c))
        assert float((ring - face).area()) < 1e-3
    bx, by = _button_xy(p)
    ring = _annulus(p["button_hole_d"] / 2 + 0.05, p["button_hole_d"] / 2 + p["btn_rim_min"], (bx, by))
    assert float((ring - face).area()) < 1e-3


def test_webs_between_through_holes():
    p = params()
    holes = _grille_holes("asm_top")
    grown = _cs(holes).offset(p["face_min_web"] / 2 - 0.03, manifold3d.JoinType.Round, 2.0, 64)
    assert len(grown.decompose()) == len(holes)


def test_sunflower_dimples_are_blind_with_solid_below():
    p = params()
    pts = np.asarray(p["sf_pts"])
    dimples = pts[pts[:, 3] == 0]
    depth = p["sf_dimple_depth"]
    floors = CS()
    for x, y, dd, _t in dimples:
        floors = floors + _disc(max(dd - p["sf_dimple_floor_shrink"], 0.2) / 2, (x, y), 48)
    for z in (p["H"] - depth - 0.05, p["H"] - depth - 0.75):
        sl = _solid("asm_top").slice(z)
        assert float((floors - sl).area()) < 1e-3, z
    above = _solid("asm_top").slice(p["H"] - depth + 0.05)
    assert float((floors ^ above).area()) < 1e-3
    tops = CS()
    for x, y, dd, _t in dimples:
        tops = tops + _disc(dd / 2 - 0.05, (x, y), 48)
    assert float((tops ^ _solid("asm_top").slice(p["H"] - 0.01)).area()) < 1e-3


def test_sunflower_is_a_vogel_spiral_of_equal_holes():
    p = params()
    pts = np.asarray(p["sf_pts"])
    r = np.hypot(pts[:, 0], pts[:, 1])
    n = np.rint((r / p["sf_c"]) ** 2 - p["sf_n0"])
    assert np.allclose(r, p["sf_c"] * np.sqrt(n + p["sf_n0"]), atol=1e-4)
    a = np.degrees(np.arctan2(pts[:, 1], pts[:, 0]))
    da = (a - n * 180 * (3 - np.sqrt(5)) - p["sf_spin"] + 180) % 360 - 180
    assert np.allclose(da, 0, atol=1e-3)
    thru = pts[pts[:, 3] == 1]
    assert len(thru) == int(p["sf_thru_n"]) >= 40
    assert thru[:, 2].max() == pytest.approx(p["sf_d"])
    assert thru[:, 2].min() >= p["sf_d"] - p["sf_thru_shrink"] - 1e-9
    assert np.all(np.hypot(thru[:, 0], thru[:, 1]) + thru[:, 2] / 2 <= p["face_thru_r"] + 1e-3)
    dimple = pts[pts[:, 3] == 0]
    assert len(dimple) == int(p["sf_dimple_n"]) > len(thru)
    rd = np.hypot(dimple[:, 0], dimple[:, 1])
    assert dimple[np.argmin(rd), 2] > dimple[np.argmax(rd), 2] + 0.5
    xy, d = pts[:, :2], pts[:, 2]
    webs = np.linalg.norm(xy[:, None] - xy[None], axis=2) - (d[:, None] + d[None]) / 2
    np.fill_diagonal(webs, np.inf)
    assert webs.min() >= p["face_min_web"]
    assert webs.min() == pytest.approx(p["sf_min_web"], abs=1e-4)


def test_sunflower_holes_in_the_mesh():
    p = params()
    pts = np.asarray(p["sf_pts"])
    face = _face_holes("asm_top", p["H"] - 0.01)
    assert len(face) == len(pts)
    centres = np.array([_centroid(q) for q in face])
    for (x, y, dd, _t) in pts:
        k = np.argmin(np.linalg.norm(centres - [x, y], axis=1))
        assert np.linalg.norm(centres[k] - [x, y]) < 0.02
        assert np.linalg.norm(face[k] - centres[k], axis=1).max() == pytest.approx(dd / 2, abs=0.03)
    thru = _grille_holes("asm_top")
    assert len(thru) == int(p["sf_thru_n"])
    for q in thru:
        assert np.linalg.norm(q, axis=1).max() <= p["face_thru_r"] + 0.01
