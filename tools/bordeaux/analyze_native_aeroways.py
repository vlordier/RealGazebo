import bpy

SOURCE = "/private/tmp/bordeaux_native_fbx/Bordeaux.fbx"
bpy.ops.import_scene.fbx(filepath=SOURCE)
for obj in bpy.context.scene.objects:
    if obj.type != "MESH" or not obj.name.lower().startswith("aeroway"):
        continue
    areas = sorted((poly.area for poly in obj.data.polygons), reverse=True)
    print(obj.name, "faces", len(areas), "total", round(sum(areas), 3),
          "largest", [round(a, 3) for a in areas[:8]])
