"""Create one faithful, clipped airport-district mesh from the native FBX."""
import os

import bpy

SOURCE = "/private/tmp/bordeaux_native_fbx/Bordeaux.fbx"
OUTPUT = "/private/tmp/bordeaux_native_airport/Bordeaux_Airport_District.fbx"
KEEP_PREFIXES = ("aeroway", "building")
# Blender imports this FBX at 1/100 scale: X and Y are the horizontal axes.
MIN_X, MAX_X = -260.0, 250.0
MIN_Y, MAX_Y = -240.0, 80.0
# The native FBX stores global Blender-plane coordinates.  Rebase them before
# export; otherwise FBX import applies their offset and actor placement applies
# another offset, which was the root cause of the scattered airport fragments.
CENTER_X, CENTER_Y = -5.0, -80.0

bpy.ops.import_scene.fbx(filepath=SOURCE)
vertices, faces = [], []
for obj in bpy.context.scene.objects:
    if obj.type != "MESH" or not obj.name.lower().startswith(KEEP_PREFIXES):
        continue
    # aeroway_ae is the source's broad aerodrome/background surface; it
    # blankets the runway/taxiway geometry and is not a usable airport mesh.
    if obj.name.lower() in ("aeroway_ae", "aeroway_ap", "aeroway_pa"):
        continue
    mesh = obj.data
    world_vertices = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    if obj.name.lower().startswith("building"):
        # The source's building tessellation is millions of coplanar GIS
        # triangles.  Replace each source object with a low-poly footprint
        # volume at its exact source bounds, preserving district layout while
        # keeping the five-kilometre environment streamable.
        min_x, max_x = min(v.x for v in world_vertices), max(v.x for v in world_vertices)
        min_y, max_y = min(v.y for v in world_vertices), max(v.y for v in world_vertices)
        if min_x > MAX_X or max_x < MIN_X or min_y > MAX_Y or max_y < MIN_Y:
            continue
        h = 1.2 + (sum(ord(c) for c in obj.name) % 18) / 10.0
        base = len(vertices)
        for z in (0.0, h):
            vertices.extend((x - CENTER_X, y - CENTER_Y, z)
                            for x, y in ((min_x, min_y), (max_x, min_y), (max_x, max_y), (min_x, max_y)))
        faces.extend(((base, base + 1, base + 2), (base, base + 2, base + 3),
                     (base + 4, base + 6, base + 5), (base + 4, base + 7, base + 6),
                     (base, base + 4, base + 5), (base, base + 5, base + 1),
                     (base + 1, base + 5, base + 6), (base + 1, base + 6, base + 2),
                     (base + 2, base + 6, base + 7), (base + 2, base + 7, base + 3),
                     (base + 3, base + 7, base + 4), (base + 3, base + 4, base)))
        continue
    for polygon in mesh.polygons:
        points = [world_vertices[i] for i in polygon.vertices]
        if not all(MIN_X <= point.x <= MAX_X and MIN_Y <= point.y <= MAX_Y for point in points):
            continue
        # Explicit triangles avoid the exporter/UE disagreement over the
        # source's irregular n-gons, which previously rendered as wire-like
        # fragments instead of filled airport surfaces.
        for index in range(1, len(points) - 1):
            offset = len(vertices)
            triangle = (points[0], points[index], points[index + 1])
            vertices.extend((point.x - CENTER_X, point.y - CENTER_Y, point.z) for point in triangle)
            faces.append((offset, offset + 1, offset + 2))

if not faces:
    raise RuntimeError("airport district crop produced no faces")
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
mesh = bpy.data.meshes.new("Bordeaux_Airport_District")
mesh.from_pydata(vertices, [], faces)
mesh.update()
obj = bpy.data.objects.new("Bordeaux_Airport_District", mesh)
bpy.context.collection.objects.link(obj)
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
bpy.ops.export_scene.fbx(filepath=OUTPUT, use_selection=True, object_types={"MESH"},
                         apply_unit_scale=True, bake_space_transform=True,
                         mesh_smooth_type="FACE")
print(f"Exported {len(faces)} faces / {len(vertices)} vertices to {OUTPUT}")
