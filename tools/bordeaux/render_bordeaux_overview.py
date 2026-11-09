"""Rasterize the imported airport and C-UAS surroundings from their OBJ previews.

The Unreal map has a project skydome material that makes unattended viewport
captures unreliable. This preview uses the exact exported FBX geometry after
OBJ conversion and produces a clear top-down verification image.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = Path("/Users/vincent/Work/RealGazebo")
SURR = Path("/private/tmp/bordeaux_preview/Bordeaux_CUAS_Surroundings.obj")
AIRPORT = Path("/private/tmp/bordeaux_preview/Bordeaux_Airport_District.obj")
W, H = 2200, 1400

COLORS = {
    "M_Ground": (68, 104, 67, 255),
    "M_Roads": (92, 96, 105, 255),
    "M_Buildings": (183, 145, 105, 255),
    "DefaultMaterial": (58, 74, 94, 255),
}


def read_obj(path):
    vertices = []
    faces = []
    material = "DefaultMaterial"
    for line in path.open(errors="ignore"):
        if line.startswith("v "):
            p = line.split()
            vertices.append((float(p[1]), float(p[2]), float(p[3])))
        elif line.startswith("usemtl "):
            material = line.split()[1].strip()
        elif line.startswith("f "):
            indices = []
            for token in line.split()[1:]:
                indices.append(int(token.split("/")[0]) - 1)
            if len(indices) >= 3:
                faces.append((tuple(indices), material))
    return vertices, faces


sv, sf = read_obj(SURR)
av, af = read_obj(AIRPORT)
all_v = sv + av
min_x = min(v[0] for v in all_v)
max_x = max(v[0] for v in all_v)
min_y = min(v[2] for v in all_v)
max_y = max(v[2] for v in all_v)


def draw_view(path, crop, title):
    cx0, cx1, cy0, cy1 = crop
    pad = 48
    sx = (W - 2 * pad) / (cx1 - cx0)
    sy = (H - 2 * pad) / (cy1 - cy0)
    scale = min(sx, sy)

    def project(v):
        return (pad + (v[0] - cx0) * scale,
                H - pad - (v[2] - cy0) * scale)

    image = Image.new("RGBA", (W, H), (19, 27, 38, 255))
    draw = ImageDraw.Draw(image, "RGBA")

    # Draw the surroundings in layer order, then the native airport district.
    for verts, faces, _offset in ((sv, sf, 0), (av, af, 0)):
        for indices, mat in faces:
            pts = [project(verts[i]) for i in indices]
            if max(x for x, _ in pts) < 0 or min(x for x, _ in pts) > W:
                continue
            if max(y for _, y in pts) < 0 or min(y for _, y in pts) > H:
                continue
            color = COLORS.get(mat, COLORS["DefaultMaterial"])
            # Airport overlay is deliberately opaque enough to read as the
            # runway/taxiway district over the surrounding urban footprint.
            if verts is av:
                color = (72, 92, 122, 235)
            draw.polygon(pts, fill=color)

    # Add a small legend and scale bar for visual verification.
    font = ImageFont.load_default()
    draw.rectangle((28, 24, 520, 112), fill=(8, 13, 20, 210))
    draw.text((44, 38), title, fill=(240, 245, 250, 255), font=font)
    draw.rectangle((44, 64, 62, 82), fill=(72, 92, 122, 235))
    draw.text((72, 66), "airport district", fill=(235, 240, 245, 255), font=font)
    draw.rectangle((210, 64, 228, 82), fill=(183, 145, 105, 255))
    draw.text((238, 66), "buildings", fill=(235, 240, 245, 255), font=font)
    draw.rectangle((345, 64, 363, 82), fill=(92, 96, 105, 255))
    draw.text((373, 66), "roads", fill=(235, 240, 245, 255), font=font)

    bar_m = 1000.0 if (cx1 - cx0) > 3000 else 500.0
    bar_px = bar_m * scale
    x0, y0 = W - 260, H - 65
    draw.line((x0, y0, x0 + bar_px, y0), fill=(245, 248, 250, 255), width=6)
    draw.line((x0, y0 - 10, x0, y0 + 10), fill=(245, 248, 250, 255), width=4)
    draw.line((x0 + bar_px, y0 - 10, x0 + bar_px, y0 + 10), fill=(245, 248, 250, 255), width=4)
    draw.text((x0, y0 + 18), f"{int(bar_m)} m", fill=(245, 248, 250, 255), font=font)
    image.convert("RGB").save(path)
    print("Rendered", path)


draw_view(OUT_DIR / "bordeaux-airport-surroundings-overview.png",
          (min_x, max_x, min_y, max_y),
          "Bordeaux airport + C-UAS operating area (overview)")

# A tighter airport view makes the runway/taxiway district easy to inspect.
draw_view(OUT_DIR / "bordeaux-airport-detail.png",
          (-26000, 26000, -33500, 16000),
          "Bordeaux airport district (detail)")
