import atexit
import functools
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import manifold3d
import numpy as np
import trimesh

CASE_DIR = Path(__file__).resolve().parent.parent
OUT_DIR = Path(tempfile.mkdtemp(prefix="voice-case-test-"))
atexit.register(shutil.rmtree, OUT_DIR, ignore_errors=True)
OPENSCAD = os.environ.get("OPENSCAD", "/opt/homebrew/bin/openscad")
BACKEND = ["--backend=manifold"]
SCAD_FILE = CASE_DIR / "voice-case.scad"


class OpenScadError(RuntimeError):
    pass


_BAD = re.compile(r"ERROR:|Assertion", re.IGNORECASE)


def _bad_lines(text):
    return [line for line in text.splitlines() if _BAD.search(line)]


def _fail(part, what, proc, extra_lines=()):
    lines = list(dict.fromkeys(_bad_lines(proc.stderr) + list(extra_lines)))
    raise OpenScadError(
        f"openscad {what} failed for part={part!r} (exit {proc.returncode})\n"
        + "\n".join(lines)
        + "\n--- stderr ---\n"
        + proc.stderr
    )


def _run(part, out, extra=()):
    cmd = [OPENSCAD, *BACKEND, "-D", f'part="{part}"', "-o", str(out), *extra, str(SCAD_FILE)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0 or _bad_lines(proc.stderr):
        _fail(part, "export", proc)
    return proc


@functools.lru_cache(maxsize=None)
def scad(part: str, ext: str = "stl") -> Path:
    out = OUT_DIR / f"{part}.{ext}"
    extra = ["--export-format", "binstl"] if ext == "stl" else []
    _run(part, out, extra)
    return out


@functools.lru_cache(maxsize=None)
def mesh(part: str) -> trimesh.Trimesh:
    return trimesh.load(scad(part), force="mesh", process=True)


def _parse_value(text):
    text = text.strip()
    if text in ("true", "false"):
        return text == "true"
    if text.startswith("["):
        return [float(x) for x in text.strip("[]").split(",") if x.strip()]
    if text.startswith('"'):
        return text.strip('"')
    return float(text)


@functools.lru_cache(maxsize=None)
def _params():
    out = OUT_DIR / "params.echo"
    proc = _run("params", out)
    text = out.read_text()
    bad = _bad_lines(text)
    if bad:
        _fail("params", "echo", proc, bad)
    values = {}
    for line in text.splitlines():
        if not line.startswith("ECHO:"):
            continue
        body = line[len("ECHO:"):]
        for name, raw in re.findall(r"(\w+) = (\[[^\]]*\]|\"[^\"]*\"|[^,]+)", body):
            values[name] = _parse_value(raw)
    return values


def params() -> dict:
    return dict(_params())


def _manifold(m: trimesh.Trimesh) -> manifold3d.Manifold:
    data = manifold3d.Mesh(
        vert_properties=np.asarray(m.vertices, dtype=np.float32),
        tri_verts=np.asarray(m.faces, dtype=np.uint32),
    )
    return manifold3d.Manifold(data)


def overlap(a: str, b: str) -> float:
    inter = _manifold(mesh(a)) ^ _manifold(mesh(b))
    return float(inter.volume())


def overhang_area(m: trimesh.Trimesh, max_deg: float = 45.0) -> float:
    normals = m.face_normals
    areas = m.area_faces
    limit = np.cos(np.radians(90.0 - max_deg))
    steep = normals[:, 2] < -limit
    on_bed = np.all(m.triangles[:, :, 2] <= m.bounds[0][2] + 1e-6, axis=1)
    return float(areas[steep & ~on_bed].sum())


def first_hit(m, origin, direction):
    tri = m.triangles
    d = np.asarray(direction, dtype=float)
    d = d / np.linalg.norm(d)
    v0 = tri[:, 0]
    e1 = tri[:, 1] - v0
    e2 = tri[:, 2] - v0
    h = np.cross(d, e2)
    a = np.einsum("ij,ij->i", e1, h)
    ok = np.abs(a) > 1e-12
    f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
    s = np.asarray(origin, dtype=float) - v0
    u = f * np.einsum("ij,ij->i", s, h)
    q = np.cross(s, e1)
    v = f * (q @ d)
    t = f * np.einsum("ij,ij->i", e2, q)
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-9)
    return float(t[hit].min()) if hit.any() else None


@functools.lru_cache(maxsize=None)
def _slice_loops(part: str, z: float):
    return tuple(np.asarray(q) for q in _manifold(mesh(part)).slice(z).to_polygons())


def inside_xy(part: str, z: float, points) -> np.ndarray:
    pts = np.asarray(points, dtype=float)
    hit = np.zeros(len(pts), dtype=bool)
    for loop in _slice_loops(part, float(z)):
        x1, y1 = loop[:, 0], loop[:, 1]
        x2, y2 = np.roll(x1, -1), np.roll(y1, -1)
        px, py = pts[:, 0][:, None], pts[:, 1][:, None]
        crosses = (y1 > py) != (y2 > py)
        with np.errstate(divide="ignore", invalid="ignore"):
            xi = x1 + (py - y1) * (x2 - x1) / (y2 - y1)
        hit ^= (crosses & (px < xi)).sum(axis=1) % 2 == 1
    return hit
