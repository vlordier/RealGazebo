import bpy
from mathutils import Vector

SOURCE = "/private/tmp/bordeaux_native_fbx/Bordeaux.fbx"
bpy.ops.import_scene.fbx(filepath=SOURCE)

def bounds(obj):
    points = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    return (
        min(point.x for point in points), max(point.x for point in points),
        min(point.y for point in points), max(point.y for point in points),
        min(point.z for point in points), max(point.z for point in points),
    )

for obj in bpy.context.scene.objects:
    if obj.type != "MESH":
        continue
    if obj.name.lower().startswith(("aeroway", "building")):
        min_x, max_x, min_y, max_y, min_z, max_z = bounds(obj)
        if obj.name.lower().startswith("aeroway"):
            print(f"AEROWAY {obj.name} verts={len(obj.data.vertices)} x={min_x:.1f},{max_x:.1f} y={min_y:.1f},{max_y:.1f} z={min_z:.1f},{max_z:.1f}")
