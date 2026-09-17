"""Step 2 of the geometry pipeline - runs inside Blender.

Imports the STLs produced by extract_3mf.py, cuts the body and the agitator
with a plane normal to the printed depth axis, walks the cut edges into closed
contours, then probes those contours to recover the parameters the simulator
needs (floor, ceiling, effusion gap, bin pitch, fin heights, slider slot).

    blender -b -P tools/blender_extract.py -- <stldir> <out.json> [preview.png]

Everything the C++ side knows about the machine's shape comes out of here; the
app never parses the 3MF itself.
"""
import bmesh
import bpy
import json
import math
import os
import sys

argv = sys.argv[sys.argv.index("--") + 1:]
STL_DIR = argv[0]
OUT_JSON = argv[1]
PREVIEW = argv[2] if len(argv) > 2 else ""

SLICE_Z = 0.0        # mid-depth of the tray: cuts walls, divider and all fins
SIMPLIFY_EPS = 0.08  # mm, Douglas-Peucker tolerance on the extracted contours
BALL_D = 4.0         # mm, from the model description ("They are 4 mm diameter")


# --------------------------------------------------------------------------
# mesh -> planar contours
# --------------------------------------------------------------------------
def load_bmesh(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    try:
        bpy.ops.wm.stl_import(filepath=path)
    except AttributeError:
        bpy.ops.import_mesh.stl(filepath=path)
    obj = bpy.context.selected_objects[0]
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    return bm


def bbox3(bm):
    lo = [min(v.co[i] for v in bm.verts) for i in range(3)]
    hi = [max(v.co[i] for v in bm.verts) for i in range(3)]
    return lo, hi


def cross_section(src, z):
    """Bisect a copy of `src` at z and return closed XY loops."""
    bm = src.copy()
    res = bmesh.ops.bisect_plane(
        bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:],
        plane_co=(0, 0, z), plane_no=(0, 0, 1),
        clear_inner=True, clear_outer=True)
    edges = [g for g in res["geom_cut"] if isinstance(g, bmesh.types.BMEdge)]

    def key(v):
        return (round(v.co.x, 4), round(v.co.y, 4))

    adj = {}
    for e in edges:
        a, b = key(e.verts[0]), key(e.verts[1])
        if a != b:
            adj.setdefault(a, set()).add(b)
            adj.setdefault(b, set()).add(a)

    loops, used = [], set()
    for start in list(adj):
        for first in list(adj[start]):
            if (start, first) in used or (first, start) in used:
                continue
            used.add((start, first))
            loop, prev, cur = [start], start, first
            while True:
                loop.append(cur)
                nxt = next((c for c in adj[cur]
                            if c != prev
                            and (cur, c) not in used and (c, cur) not in used), None)
                if nxt is None:
                    break
                used.add((cur, nxt))
                prev, cur = cur, nxt
                if cur == start:
                    break
            if len(loop) >= 4:
                loops.append(loop[:-1] if loop[0] == loop[-1] else loop)
    bm.free()
    loops.sort(key=lambda lp: -abs(signed_area(lp)))
    return loops


def interior_depth(src, lo, hi, rings_at):
    """The tray is printed as an open box: solid from the outer back face up to
    the inner floor, hollow above it.  Walk the print axis and find where the
    cross-section stops covering the whole footprint."""
    full = (hi[0] - lo[0]) * (hi[1] - lo[1])

    def solid(z):
        rings = rings_at(z)
        if not rings:
            return False
        return abs(signed_area(max(rings, key=lambda r: abs(signed_area(r))))) > 0.5 * full

    z0, z1 = lo[2] + 1e-3, hi[2] - 1e-3
    if not solid(z0):
        z0, z1 = z1, z0                      # printed the other way up
    a, b = z0, z1
    for _ in range(40):                      # bisect the solid/hollow boundary
        m = 0.5 * (a + b)
        if solid(m):
            a = m
        else:
            b = m
    back = 0.5 * (a + b)
    front = z1
    return (min(back, front), max(back, front))


def motor_hole(rings_at, z_probe):
    """The round hole in the back plate that the motor's pilot boss sits in.
    Its centre is where the agitator turns, measured rather than assumed."""
    best = None
    for ring in rings_at(z_probe, min_area=5.0):
        area = abs(signed_area(ring))
        if area > 2000.0:
            continue
        cx = sum(p[0] for p in ring) / len(ring)
        cy = sum(p[1] for p in ring) / len(ring)
        rs = [math.hypot(p[0] - cx, p[1] - cy) for p in ring]
        r = sum(rs) / len(rs)
        if max(rs) - min(rs) > 0.05 * r:      # not round enough to be a bore
            continue
        if best is None or r > best[2]:
            best = (cx, cy, r)
    return best


def signed_area(pts):
    a = 0.0
    for i in range(len(pts)):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % len(pts)]
        a += x1 * y2 - x2 * y1
    return a / 2.0


def simplify(pts, eps):
    """Douglas-Peucker on a closed ring, split at its two extreme points."""
    if len(pts) < 8:
        return pts
    i0 = min(range(len(pts)), key=lambda i: (pts[i][0], pts[i][1]))
    i1 = max(range(len(pts)), key=lambda i: (pts[i][0], pts[i][1]))
    rot = pts[i0:] + pts[:i0]
    j = (i1 - i0) % len(pts)
    return _dp(rot[:j + 1], eps)[:-1] + _dp(rot[j:] + [rot[0]], eps)[:-1]


def _dp(pts, eps):
    if len(pts) < 3:
        return list(pts)
    (ax, ay), (bx, by) = pts[0], pts[-1]
    dx, dy = bx - ax, by - ay
    n = math.hypot(dx, dy)
    best, bi = -1.0, 0
    for i in range(1, len(pts) - 1):
        px, py = pts[i]
        d = (abs(dy * px - dx * py + bx * ay - by * ax) / n) if n > 1e-12 \
            else math.hypot(px - ax, py - ay)
        if d > best:
            best, bi = d, i
    if best <= eps:
        return [pts[0], pts[-1]]
    return _dp(pts[:bi + 1], eps)[:-1] + _dp(pts[bi:], eps)


# --------------------------------------------------------------------------
# probing the extracted outline (even-odd ray casts)
# --------------------------------------------------------------------------
class Outline:
    def __init__(self, rings):
        self.rings = rings
        xs = [p[0] for r in rings for p in r]
        ys = [p[1] for r in rings for p in r]
        self.lo = (min(xs), min(ys))
        self.hi = (max(xs), max(ys))

    def _spans(self, axis, at):
        """Solid intervals along `axis` (0=x, 1=y) on the line other-axis == at."""
        o = 1 - axis
        hits = []
        for ring in self.rings:
            n = len(ring)
            for i in range(n):
                a, b = ring[i], ring[(i + 1) % n]
                if (a[o] > at) == (b[o] > at):
                    continue
                t = (at - a[o]) / (b[o] - a[o])
                hits.append(a[axis] + t * (b[axis] - a[axis]))
        hits.sort()
        return [(hits[i], hits[i + 1]) for i in range(0, len(hits) - 1, 2)]

    def spans_x(self, y):
        return self._spans(0, y)

    def spans_y(self, x):
        return self._spans(1, x)


def detect(outline):
    """Recover the machine's functional dimensions from the sliced outline."""
    (x_lo, y_lo), (x_hi, y_hi) = outline.lo, outline.hi
    f = {}

    # --- floor / ceiling: lowest top-of-bottom-wall and highest bottom-of-top-wall
    floor_c, ceil_c = [], []
    for k in range(1, 60):
        x = x_lo + (x_hi - x_lo) * k / 60.0 + 0.0137   # nudge off vertices
        sp = outline.spans_y(x)
        if len(sp) >= 2:
            floor_c.append(sp[0][1])
            ceil_c.append(sp[-1][0])
    f["floor_y"] = min(floor_c)
    f["ceiling_y"] = max(ceil_c)

    # --- side walls: probe at mid height, clear of the big interior corner fillets
    sp = outline.spans_x(0.5 * (f["floor_y"] + f["ceiling_y"]) + 0.0137)
    f["left_wall_x"] = sp[0][1]
    f["right_wall_x"] = sp[-1][0]

    # --- divider: scan down from the ceiling until an interior span appears
    divider = None
    y = f["ceiling_y"] - 0.25
    while y > f["floor_y"] and divider is None:
        for a, b in outline.spans_x(y):
            if a > f["left_wall_x"] + 0.5 and b < f["right_wall_x"] - 0.5:
                divider = (a, b)
                break
        y -= 0.1
    f["divider_x0"], f["divider_x1"] = divider
    xm = 0.5 * (divider[0] + divider[1])
    # the divider grows out of the bottom wall, so its tip ends the first span
    f["divider_top_y"] = outline.spans_y(xm)[0][1]
    f["effusion_gap"] = f["ceiling_y"] - f["divider_top_y"]

    # --- bin fins: probe above the base fillets, then measure each tip
    probe_y = f["floor_y"] + 8.0
    fins = []
    for a, b in outline.spans_x(probe_y):
        if a <= f["left_wall_x"] + 0.5 or b >= f["right_wall_x"] - 0.5:
            continue
        if a < f["divider_x1"] + 0.5:      # that's the divider itself
            continue
        fins.append((a, b))
    f["fins"] = [{"x0": a, "x1": b,
                  "top_y": outline.spans_y(0.5 * (a + b))[0][1]} for a, b in fins]
    if len(fins) > 1:
        f["fin_pitch"] = (fins[-1][0] - fins[0][0]) / (len(fins) - 1)
    f["fin_top_y"] = min(fn["top_y"] for fn in f["fins"]) if fins else f["floor_y"]
    f["drop_height"] = f["divider_top_y"] - f["fin_top_y"]

    # --- bins: the clear gaps between divider, fins and the right wall
    edges = [f["divider_x1"]] + [v for a, b in fins for v in (a, b)] + [f["right_wall_x"]]
    f["bins"] = [{"x0": edges[i], "x1": edges[i + 1]}
                 for i in range(0, len(edges) - 1, 2)]

    # --- slider slot: the break in the left wall's material
    xw = 0.5 * (x_lo + f["left_wall_x"])
    wall = outline.spans_y(xw)
    gaps = [(wall[i][1], wall[i + 1][0]) for i in range(len(wall) - 1)]
    gaps = [g for g in gaps if 0.5 < g[1] - g[0] < 20.0]
    if gaps:
        g = max(gaps, key=lambda g: g[1] - g[0])
        f["slider_slot_y0"], f["slider_slot_y1"] = g
    return f


# --------------------------------------------------------------------------
def preview_png(path, rings, features):
    """Optional Blender render of the extracted cross-section, for eyeballing."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    me = bpy.data.meshes.new("slice")
    verts, edges, base = [], [], 0
    for r in rings:
        verts += [(p[0], p[1], 0.0) for p in r]
        edges += [(base + i, base + (i + 1) % len(r)) for i in range(len(r))]
        base += len(r)
    me.from_pydata(verts, edges, [])
    ob = bpy.data.objects.new("slice", me)
    bpy.context.collection.objects.link(ob)
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.edge_face_add()
    bpy.ops.object.mode_set(mode='OBJECT')

    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    cam_d = bpy.data.cameras.new("c")
    cam_d.type = 'ORTHO'
    cam_d.ortho_scale = w * 1.04
    cam = bpy.data.objects.new("c", cam_d)
    cam.location = (0.5 * (min(xs) + max(xs)), 0.5 * (min(ys) + max(ys)), 100)
    bpy.context.collection.objects.link(cam)
    sc = bpy.context.scene
    sc.camera = cam
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'FLAT'
    sc.display.shading.color_type = 'SINGLE'
    sc.display.shading.single_color = (0.85, 0.3, 0.2)
    sc.render.resolution_x = 1400
    sc.render.resolution_y = max(64, int(1400 * h / w))
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)


def main():
    asm_path = os.path.join(STL_DIR, "assembly.json")
    offsets = json.load(open(asm_path))["offsets"] if os.path.exists(asm_path) else {}

    body = load_bmesh(os.path.join(STL_DIR, "body.stl"))
    lo, hi = bbox3(body)
    agit = load_bmesh(os.path.join(STL_DIR, "agitator.stl"))
    alo, ahi = bbox3(agit)
    rings = cross_section(body, SLICE_Z)
    rings = [simplify(r, SIMPLIFY_EPS) for r in rings]
    # bolt holes buried inside the walls are not reachable by a ball
    rings = [r for r in rings if abs(signed_area(r)) > 50.0]
    outline = Outline(rings)
    feat = detect(outline)

    arings = cross_section(agit, 0.0)
    arings = [simplify(r, SIMPLIFY_EPS) for r in arings]
    rotor = max(arings, key=lambda r: abs(signed_area(r)))
    rotor_r = max(math.hypot(*p) for p in rotor)

    ao = offsets.get("agitator", [0, 0, 0])
    so = offsets.get("slider", [0, 0, 0])

    def rings_at(z, min_area=50.0):
        rs = cross_section(body, z)
        return [r for r in rs if abs(signed_area(r)) > min_area]

    iz0, iz1 = interior_depth(body, lo, hi, rings_at)

    # The agitator's depth placement cannot come from the 3MF's per-part
    # source offsets: those record where each part sat in the imported scene,
    # not how the machine goes together, and taking them literally buries most
    # of the rotor inside the solid back plate.  Measure it instead - the rotor
    # is wider than the motor hole everywhere along its length, so it can only
    # stand on the inner back face.
    hole = motor_hole(rings_at, 0.5 * (lo[2] + iz0))
    rotor_h = ahi[2] - alo[2]
    az0 = iz0
    az1 = min(iz1, iz0 + rotor_h)
    if hole:
        ao = [hole[0], hole[1], ao[2]]

    data = {
        "units": "mm",
        "generator": "blender_extract.py",
        "blender": bpy.app.version_string,
        "slice_z": SLICE_Z,
        "ball_diameter": BALL_D,
        "body": {
            "bbox_min": lo[:2], "bbox_max": hi[:2], "depth": [lo[2], hi[2]],
            # z range a ball can occupy: inner back face up to the open rim
            "interior_z": [round(iz0, 4), round(iz1, 4)],
            "rings": [[[round(x, 4), round(y, 4)] for x, y in r] for r in rings],
        },
        "agitator": {
            "center": [ao[0], ao[1]],
            # the rotor is an extrusion too; only the part above the inner back
            # face actually sweeps the chamber
            "z": [round(az0, 4), round(az1, 4)],
            "motor_hole_radius": round(hole[2], 4) if hole else None,
            "radius": round(rotor_r, 4),
            "hub_radius": round(min(math.hypot(*p) for p in rotor), 4),
            "profile": [[round(x, 4), round(y, 4)] for x, y in rotor],
        },
        "features": {k: (v if isinstance(v, (list, dict)) else round(v, 4))
                     for k, v in feat.items()},
    }
    os.makedirs(os.path.dirname(os.path.abspath(OUT_JSON)), exist_ok=True)
    with open(OUT_JSON, "w") as fp:
        json.dump(data, fp, indent=1)

    ft = data["features"]
    print("\n=== extracted from the 3MF ===")
    print(f"  body            {hi[0]-lo[0]:.1f} x {hi[1]-lo[1]:.1f} x {hi[2]-lo[2]:.1f} mm")
    print(f"  rings           {[len(r) for r in rings]}")
    print(f"  interior        x[{ft['left_wall_x']:.2f},{ft['right_wall_x']:.2f}] "
          f"y[{ft['floor_y']:.2f},{ft['ceiling_y']:.2f}]")
    print(f"  divider         x[{ft['divider_x0']:.2f},{ft['divider_x1']:.2f}] "
          f"top y={ft['divider_top_y']:.2f}")
    print(f"  effusion gap    {ft['effusion_gap']:.2f} mm "
          f"({ft['effusion_gap']/BALL_D:.2f} ball diameters)")
    print(f"  fins            {len(ft['fins'])}  pitch {ft.get('fin_pitch', 0):.2f} mm  "
          f"tip y={ft['fin_top_y']:.2f}")
    print(f"  bins            {len(ft['bins'])}  "
          f"widths {[round(b['x1']-b['x0'], 2) for b in ft['bins']]}")
    print(f"  drop height     {ft['drop_height']:.2f} mm")
    if "slider_slot_y0" in ft:
        print(f"  slider slot     y[{ft['slider_slot_y0']:.2f},{ft['slider_slot_y1']:.2f}]")
    print(f"  interior depth  z[{iz0:.2f},{iz1:.2f}]  = {iz1 - iz0:.2f} mm "
          f"({(iz1 - iz0) / BALL_D:.2f} ball diameters)")
    if hole:
        print(f"  motor hole      centre ({hole[0]:.2f},{hole[1]:.2f}) r={hole[2]:.2f}")
    print(f"  agitator        centre ({ao[0]:.2f},{ao[1]:.2f}) "
          f"r_hub={data['agitator']['hub_radius']:.2f} r_tip={rotor_r:.2f} "
          f"height={rotor_h:.2f} -> z[{az0:.2f},{az1:.2f}] "
          f"({100.0 * (az1 - az0) / (iz1 - iz0):.0f}% of the chamber depth)")
    if rotor_h > iz1 - iz0 + 1e-6:
        print("  ! rotor is taller than the chamber is deep - clipped")
    print(f"  slider          offset ({so[0]:.2f},{so[1]:.2f},{so[2]:.2f})")
    print("  ->", os.path.abspath(OUT_JSON))

    if PREVIEW:
        preview_png(PREVIEW, rings, feat)
        print("  ->", os.path.abspath(PREVIEW))


main()
