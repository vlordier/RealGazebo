"""Build a light, source-aligned C-UAS surroundings layer from native FBX GIS meshes."""
import os

import bpy

SOURCE = "/private/tmp/bordeaux_native_fbx/Bordeaux.fbx"
OUTPUT = "/private/tmp/bordeaux_native_airport/Bordeaux_CUAS_Surroundings.fbx"
MIN_X, MAX_X, MIN_Y, MAX_Y = -260.0, 250.0, -240.0, 80.0
CENTER_X, CENTER_Y = -5.0, -80.0

bpy.ops.import_scene.fbx(filepath=SOURCE)
verts, faces, face_materials = [], [], []

def add_box(min_x, max_x, min_y, max_y, height, material):
    base = len(verts)
    for z in (0.0, height):
        verts.extend((x - CENTER_X, y - CENTER_Y, z)
                     for x, y in ((min_x, min_y), (max_x, min_y), (max_x, max_y), (min_x, max_y)))
    box_faces = ((0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6),
                 (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2),
                 (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0))
    faces.extend(tuple(base + index for index in face) for face in box_faces)
    face_materials.extend([material] * len(box_faces))

# Continuous ground gives the simulation a navigation/landing reference.
add_box(MIN_X, MAX_X, MIN_Y, MAX_Y, -0.15, 0)

for obj in bpy.context.scene.objects:
    if obj.type != "MESH":
        continue
    name = obj.name.lower()
    if not name.startswith(("building", "highway")):
        continue
    world = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    if not world:
        continue
    min_x, max_x = min(p.x for p in world), max(p.x for p in world)
    min_y, max_y = min(p.y for p in world), max(p.y for p in world)
    if min_x > MAX_X or max_x < MIN_X or min_y > MAX_Y or max_y < MIN_Y:
        continue
    # Preserve compact source footprints.  Large source objects are aggregate
    # GIS building/road groups, so represent them as lightweight blocks/corridors
    # rather than discarding the surrounding city entirely.
    width, depth = max_x - min_x, max_y - min_y
    if width <= 0.05 or depth <= 0.05:
        continue
    if name.startswith("building"):
        if width * depth <= 5000:
            height = 1.2 + (sum(map(ord, obj.name)) % 20) / 10.0
            add_box(min_x, max_x, min_y, max_y, height, 1)
        else:
            # Coarse urban blocks inside the aggregate source extent, with
            # streets left between them for drone/vehicle navigation.
            cell = 22.0
            x = min_x
            seed = sum(map(ord, obj.name))
            while x < max_x:
                y = min_y
                while y < max_y:
                    if ((int((x - min_x) / cell) * 7 + int((y - min_y) / cell) * 11 + seed) % 5) != 0:
                        add_box(x + 2.0, min(x + cell - 2.0, max_x), y + 2.0,
                                min(y + cell - 2.0, max_y), 1.2 + ((seed + int(x + y)) % 18) / 10.0, 1)
                    y += cell
                x += cell
    else:
        # Preserve road direction from the source bounds, but constrain their
        # thickness so aggregate road objects cannot become flat slabs.
        if width >= depth:
            mid = (min_y + max_y) / 2
            add_box(min_x, max_x, mid - min(3.0, depth / 2), mid + min(3.0, depth / 2), 0.03, 2)
        else:
            mid = (min_x + max_x) / 2
            add_box(mid - min(3.0, width / 2), mid + min(3.0, width / 2), min_y, max_y, 0.03, 2)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
mesh = bpy.data.meshes.new("Bordeaux_CUAS_Surroundings")
mesh.from_pydata(verts, [], faces)
mesh.update()
for material_name in ("M_Ground", "M_Buildings", "M_Roads"):
    mesh.materials.append(bpy.data.materials.new(material_name))
for polygon, material_index in zip(mesh.polygons, face_materials, strict=True):
    polygon.material_index = material_index
obj = bpy.data.objects.new("Bordeaux_CUAS_Surroundings", mesh)
bpy.context.collection.objects.link(obj)
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
bpy.ops.export_scene.fbx(filepath=OUTPUT, use_selection=True, object_types={"MESH"},
                         apply_unit_scale=True, bake_space_transform=True,
                         mesh_smooth_type="FACE")
print(f"Exported C-UAS surroundings: faces={len(faces)} verts={len(verts)} to {OUTPUT}")
