#!/usr/bin/env python3
from __future__ import annotations

import math
from pathlib import Path


Vec3 = tuple[float, float, float]
Tri = tuple[Vec3, Vec3, Vec3]


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(v: Vec3) -> Vec3:
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length < 1.0e-9:
        return (0.0, 0.0, 1.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def normal(tri: Tri) -> Vec3:
    return normalize(cross(sub(tri[1], tri[0]), sub(tri[2], tri[0])))


def write_ascii_stl(path: Path, name: str, triangles: list[Tri]) -> None:
    with path.open("w", encoding="ascii") as f:
        f.write(f"solid {name}\n")
        for tri in triangles:
            n = normal(tri)
            f.write(f"  facet normal {n[0]:.8f} {n[1]:.8f} {n[2]:.8f}\n")
            f.write("    outer loop\n")
            for v in tri:
                f.write(f"      vertex {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
            f.write("    endloop\n")
            f.write("  endfacet\n")
        f.write(f"endsolid {name}\n")


def add_box(tris: list[Tri], cx: float, cy: float, z0: float, sx: float, sy: float, sz: float) -> None:
    x0, x1 = cx - sx / 2.0, cx + sx / 2.0
    y0, y1 = cy - sy / 2.0, cy + sy / 2.0
    z1 = z0 + sz
    v = {
        "000": (x0, y0, z0),
        "100": (x1, y0, z0),
        "110": (x1, y1, z0),
        "010": (x0, y1, z0),
        "001": (x0, y0, z1),
        "101": (x1, y0, z1),
        "111": (x1, y1, z1),
        "011": (x0, y1, z1),
    }
    tris.extend([
        (v["000"], v["010"], v["110"]), (v["000"], v["110"], v["100"]),
        (v["001"], v["101"], v["111"]), (v["001"], v["111"], v["011"]),
        (v["000"], v["100"], v["101"]), (v["000"], v["101"], v["001"]),
        (v["010"], v["011"], v["111"]), (v["010"], v["111"], v["110"]),
        (v["000"], v["001"], v["011"]), (v["000"], v["011"], v["010"]),
        (v["100"], v["110"], v["111"]), (v["100"], v["111"], v["101"]),
    ])


def add_prism(tris: list[Tri], points: list[tuple[float, float]], z0: float, height: float) -> None:
    z1 = z0 + height
    bottom = [(x, y, z0) for x, y in points]
    top = [(x, y, z1) for x, y in points]
    n = len(points)

    for i in range(1, n - 1):
        tris.append((bottom[0], bottom[i + 1], bottom[i]))
        tris.append((top[0], top[i], top[i + 1]))

    for i in range(n):
        j = (i + 1) % n
        tris.append((bottom[i], bottom[j], top[j]))
        tris.append((bottom[i], top[j], top[i]))


def cylinder(radius: float, height: float, segments: int = 48) -> list[Tri]:
    points = [
        (math.cos(2 * math.pi * i / segments) * radius, math.sin(2 * math.pi * i / segments) * radius)
        for i in range(segments)
    ]
    tris: list[Tri] = []
    add_prism(tris, points, 0.0, height)
    return tris


def star_prism(outer: float, inner: float, height: float, tips: int = 5) -> list[Tri]:
    points: list[tuple[float, float]] = []
    for i in range(tips * 2):
        radius = outer if i % 2 == 0 else inner
        angle = math.pi / 2.0 + i * math.pi / tips
        points.append((math.cos(angle) * radius, math.sin(angle) * radius))
    tris: list[Tri] = []
    add_prism(tris, points, 0.0, height)
    return tris


def rounded_demo_part() -> list[Tri]:
    tris: list[Tri] = []
    add_box(tris, 0.0, 0.0, 0.0, 30.0, 18.0, 4.0)
    add_box(tris, -8.0, 0.0, 4.0, 8.0, 18.0, 10.0)
    add_box(tris, 8.0, 0.0, 4.0, 8.0, 18.0, 10.0)
    return tris


def material_a_base() -> list[Tri]:
    tris: list[Tri] = []
    add_box(tris, 0.0, 0.0, 0.0, 36.0, 24.0, 5.0)
    return tris


def material_b_insert() -> list[Tri]:
    tris: list[Tri] = []
    add_box(tris, 0.0, 0.0, 0.0, 26.0, 6.0, 9.0)
    add_box(tris, 0.0, 0.0, 0.0, 6.0, 18.0, 9.0)
    return tris


def three_towers() -> list[Tri]:
    tris: list[Tri] = []
    add_box(tris, 0.0, 0.0, 0.0, 34.0, 18.0, 3.0)
    for x, h in [(-11.0, 10.0), (0.0, 16.0), (11.0, 7.0)]:
        for tri in cylinder(4.0, h, 32):
            tris.append(tuple((vx + x, vy, vz + 3.0) for vx, vy, vz in tri))  # type: ignore[arg-type]
    return tris


def main() -> None:
    out = Path(__file__).resolve().parents[1] / "demo_stl"
    out.mkdir(parents=True, exist_ok=True)

    files = {
        "01_basic_block.stl": ("basic_block", rounded_demo_part()),
        "02_cylinder_20mm.stl": ("cylinder_20mm", cylinder(10.0, 16.0, 64)),
        "03_star_badge.stl": ("star_badge", star_prism(13.0, 6.5, 6.0, 5)),
        "04_three_towers.stl": ("three_towers", three_towers()),
        "multi_A_base_plate.stl": ("multi_A_base_plate", material_a_base()),
        "multi_B_cross_insert.stl": ("multi_B_cross_insert", material_b_insert()),
    }

    for filename, (name, triangles) in files.items():
        write_ascii_stl(out / filename, name, triangles)
        print(out / filename)


if __name__ == "__main__":
    main()
