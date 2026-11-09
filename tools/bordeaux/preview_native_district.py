"""Render a deterministic top-down preview of the actual exported district FBX."""
import bpy

SOURCE = "/private/tmp/bordeaux_native_airport/Bordeaux_Airport_District.fbx"
OUTPUT = "/Users/vincent/Work/RealGazebo/airport-district-current.png"

bpy.ops.import_scene.fbx(filepath=SOURCE)
objects = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not objects:
    raise RuntimeError("district export contains no mesh")
points = [o.matrix_world @ corner for o in objects for corner in
          [__import__('mathutils').Vector(c) for c in o.bound_box]]
min_x, max_x = min(p.x for p in points), max(p.x for p in points)
min_y, max_y = min(p.y for p in points), max(p.y for p in points)
min_z = min(p.z for p in points)

asphalt = bpy.data.materials.new("Airport asphalt")
asphalt.diffuse_color = (0.12, 0.14, 0.17, 1)
for obj in objects:
    obj.data.materials.clear()
    obj.data.materials.append(asphalt)

bpy.ops.mesh.primitive_plane_add(size=2, location=((min_x + max_x) / 2, (min_y + max_y) / 2, min_z - 2.0))
ground = bpy.context.object
ground.scale = ((max_x - min_x) / 2 * 1.08, (max_y - min_y) / 2 * 1.08, 1)
grass = bpy.data.materials.new("Grass")
grass.diffuse_color = (0.055, 0.16, 0.065, 1)
ground.data.materials.append(grass)
ground.hide_render = True

bpy.ops.object.camera_add(location=((min_x + max_x) / 2, (min_y + max_y) / 2, max(max_x - min_x, max_y - min_y) * 1.2))
camera = bpy.context.object
camera.data.type = "ORTHO"
camera.data.ortho_scale = max(max_x - min_x, max_y - min_y) * 1.1
bpy.context.scene.camera = camera
scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "FLAT"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = False
scene.render.resolution_x = 1600
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = OUTPUT
bpy.ops.render.render(write_still=True)
print("Rendered", OUTPUT)
