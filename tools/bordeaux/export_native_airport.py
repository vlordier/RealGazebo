"""Export Bordeaux's original aeroway objects as a compact Unreal FBX."""
import os

import bpy

SOURCE = "/private/tmp/bordeaux_native_fbx/Bordeaux.fbx"
OUTPUT = "/private/tmp/bordeaux_native_airport/Bordeaux_Airport_Runways.fbx"

bpy.ops.import_scene.fbx(filepath=SOURCE)
bpy.ops.object.select_all(action="DESELECT")
kept = []
for obj in bpy.context.scene.objects:
    if obj.type == "MESH" and obj.name.lower().startswith("aeroway"):
        obj.select_set(True)
        kept.append(obj)

if not kept:
    raise RuntimeError("no original aeroway meshes found")

bpy.context.view_layer.objects.active = kept[0]
os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
bpy.ops.export_scene.fbx(
    filepath=OUTPUT,
    use_selection=True,
    apply_unit_scale=True,
    bake_space_transform=True,
    object_types={"MESH"},
    mesh_smooth_type="FACE",
    path_mode="COPY",
)
print(f"Exported {len(kept)} original airport meshes to {OUTPUT}")
