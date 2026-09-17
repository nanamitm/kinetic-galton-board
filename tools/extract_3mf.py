"""Step 1 of the geometry pipeline.

Unpacks the Bambu Studio .3mf project and writes each sub-object mesh out as a
binary STL that Blender can import.  Pure stdlib, no Blender needed here.

    python tools/extract_3mf.py [MaxwellBoltzmann775.3mf] [outdir]
"""
import os
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

NS = "{http://schemas.microsoft.com/3dmanufacturing/core/2015/02}"

# object path inside the archive -> short name we use downstream
PARTS = {
    "3D/Objects/object_3.model": "body",
    "3D/Objects/object_2.model": "agitator",
    "3D/Objects/object_4.model": "slider",
    "3D/Objects/object_5.model": "cover",
}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_stl(path, verts, tris):
    with open(path, "wb") as f:
        f.write(b"\0" * 80)
        f.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            va, vb, vc = verts[a], verts[b], verts[c]
            u = [vb[i] - va[i] for i in range(3)]
            w = [vc[i] - va[i] for i in range(3)]
            n = (u[1] * w[2] - u[2] * w[1],
                 u[2] * w[0] - u[0] * w[2],
                 u[0] * w[1] - u[1] * w[0])
            ln = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5 or 1.0
            f.write(struct.pack("<3f", *[v / ln for v in n]))
            for v in (va, vb, vc):
                f.write(struct.pack("<3f", *v))
            f.write(b"\0\0")


def read_assembly_offsets(z):
    """Bambu keeps each part's position in the original design under
    `source_offset_*` in Metadata/model_settings.config.  Subtracting the
    body's offset puts every part back into body-local coordinates."""
    cfg = ET.fromstring(z.read("Metadata/model_settings.config"))
    by_name = {}
    for obj in cfg.findall("object"):
        for part in obj.findall("part"):
            meta = {m.get("key"): m.get("value") for m in part.findall("metadata")}
            name = meta.get("name", "")
            by_name[name] = [float(meta.get(f"source_offset_{a}", 0.0)) for a in "xyz"]
    alias = {"body": "Body for 775.stl", "agitator": "Agitator for 775.stl",
             "slider": "Slider.stl"}
    body = by_name.get(alias["body"], [0.0, 0.0, 0.0])
    return {k: [by_name.get(v, body)[i] - body[i] for i in range(3)]
            for k, v in alias.items()}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(ROOT, "MaxwellBoltzmann775.3mf")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build_assets", "stl")
    os.makedirs(out, exist_ok=True)

    z = zipfile.ZipFile(src)
    names = set(z.namelist())
    for member, name in PARTS.items():
        if member not in names:
            print(f"  ! {member} missing, skipped")
            continue
        mesh = ET.fromstring(z.read(member)).find(f".//{NS}mesh")
        verts = [(float(v.get("x")), float(v.get("y")), float(v.get("z")))
                 for v in mesh.find(f"{NS}vertices")]
        tris = [(int(t.get("v1")), int(t.get("v2")), int(t.get("v3")))
                for t in mesh.find(f"{NS}triangles")]
        write_stl(os.path.join(out, name + ".stl"), verts, tris)
        xs = [v[0] for v in verts]
        ys = [v[1] for v in verts]
        zs = [v[2] for v in verts]
        print(f"  {name:9s} {len(tris):6d} tris  "
              f"X[{min(xs):8.2f},{max(xs):8.2f}] "
              f"Y[{min(ys):8.2f},{max(ys):8.2f}] "
              f"Z[{min(zs):7.2f},{max(zs):7.2f}]")

    import json
    offsets = read_assembly_offsets(z)
    with open(os.path.join(out, "assembly.json"), "w") as f:
        json.dump({"source": os.path.basename(src), "offsets": offsets}, f, indent=2)
    for k, v in offsets.items():
        print(f"  offset {k:9s} ({v[0]:8.2f},{v[1]:8.2f},{v[2]:8.2f})")
    print("STL written to", out)


if __name__ == "__main__":
    main()
