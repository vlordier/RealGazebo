import bpy

SOURCE = "/private/tmp/bordeaux_native_fbx/Bordeaux.fbx"
bpy.ops.import_scene.fbx(filepath=SOURCE)

prefixes = ("aeroway", "building", "highway", "landuse", "natural", "water")
for prefix in prefixes:
    objects = [o for o in bpy.context.scene.objects
               if o.type == "MESH" and o.name.lower().startswith(prefix)]
    if not objects:
        print(prefix, "objects=0")
        continue
    vertices = sum(len(o.data.vertices) for o in objects)
    faces = sum(len(o.data.polygons) for o in objects)
    print(prefix, f"objects={len(objects)} vertices={vertices} faces={faces}")
