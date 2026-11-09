"""Render the reviewed LFBD Bati3D FBX context as a deterministic visual QA image.

This is a source-model validation fallback for the Unreal integration.  It
uses the same 35 historical context tiles and their manifest coordinates; it
does not create a claim of current airport geometry or operational authority.
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path('/private/tmp/lfbd_bati3d_core')
MANIFEST = ROOT / 'manifest.json'
OUTPUT = Path('/Users/vincent/Work/RealGazebo/lfbd-bati3d-context-blender.png')
OBLIQUE_OUTPUT = Path('/Users/vincent/Work/RealGazebo/lfbd-bati3d-context-oblique.png')

manifest = json.loads(MANIFEST.read_text())
reference_e, reference_n = manifest['reference']['epsg3945_easting_northing']

# Clear the factory scene deterministically.
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
for datablocks in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras, bpy.data.lights):
    for item in list(datablocks):
        if item.users == 0:
            datablocks.remove(item)

def material_for_tile(tile):
    """Use Bati3D's supplied orthophoto texture; preserve UVs from the mesh."""
    material = bpy.data.materials.new('Historical_Bati3D_' + tile['id'])
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    principled = nodes.get('Principled BSDF')
    image = bpy.data.images.load(str(ROOT / tile['texture']), check_existing=True)
    image_node = nodes.new('ShaderNodeTexImage')
    image_node.image = image
    links.new(image_node.outputs['Color'], principled.inputs['Base Color'])
    principled.inputs['Roughness'].default_value = 0.9
    return material

objects = []
for tile in manifest['tiles']:
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT / tile['fbx']), use_custom_normals=False)
    imported = set(bpy.context.scene.objects) - before
    x0, y0, x1, y1 = tile['epsg3945_bbox']
    # Original subtiles are local 200m meshes. Place each relative to the
    # airport reference in metres (Blender native units).
    offset = ((x0 + x1) * 0.5 - reference_e, (y0 + y1) * 0.5 - reference_n, 0.0)
    tile_material = material_for_tile(tile)
    for obj in imported:
        if obj.type != 'MESH':
            continue
        # Assimp's FBX declares centimetres and Blender faithfully applies
        # the 0.01 unit scale to every imported transform.  Restore metres
        # before placing this 200m tile in its manifest position.
        # 3DS/Assimp axes: X=east, Z=north, Y=height.  Blender uses X/Y as
        # the ground plane and Z as height, hence the -90° X-axis remap.
        obj.matrix_world = (
            Matrix.Translation(offset)
            @ Matrix.Rotation(-math.pi / 2.0, 4, 'X')
            @ Matrix.Scale(100.0, 4)
            @ obj.matrix_world
        )
        obj.data.materials.clear()
        obj.data.materials.append(tile_material)
        objects.append(obj)

if not objects:
    raise RuntimeError('no FBX mesh objects imported')

corners = [obj.matrix_world @ __import__('mathutils').Vector(corner)
           for obj in objects for corner in obj.bound_box]
actual_min = tuple(min(point[i] for point in corners) for i in range(3))
actual_max = tuple(max(point[i] for point in corners) for i in range(3))
print('BLENDER_BATI3D_BOUNDS', actual_min, actual_max,
      'first', objects[0].name, tuple(objects[0].dimensions))

# A dark base plane makes open-space and built form legible without implying
# terrain or live imagery.
bpy.ops.mesh.primitive_plane_add(size=4000, location=(0, 0, actual_min[2] - 1.0))
ground = bpy.context.object
ground_mat = bpy.data.materials.new('Historical_Context_Ground')
ground_mat.diffuse_color = (0.015, 0.028, 0.045, 1)
ground_mat.use_nodes = True
ground_mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.008, 0.017, 0.03, 1)
ground_mat.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 1.0
ground.data.materials.append(ground_mat)

# Use the known manifest envelope, avoiding any dependence on accidental FBX
# origin transforms.
x_values = [t['epsg3945_bbox'][0] - reference_e for t in manifest['tiles']] + [t['epsg3945_bbox'][2] - reference_e for t in manifest['tiles']]
y_values = [t['epsg3945_bbox'][1] - reference_n for t in manifest['tiles']] + [t['epsg3945_bbox'][3] - reference_n for t in manifest['tiles']]
cx, cy = (min(x_values) + max(x_values)) / 2, (min(y_values) + max(y_values)) / 2
span = max(max(x_values) - min(x_values), max(y_values) - min(y_values))

bpy.ops.object.camera_add(location=(cx, cy, 1850), rotation=(0, 0, 0))
camera = bpy.context.object
camera.data.type = 'ORTHO'
camera.data.ortho_scale = span * 1.2
camera.data.clip_end = 5000.0
# Avoid relying on Blender's camera-add defaults: make its local -Z axis look
# directly at the real manifest centre.
camera.rotation_euler = (Vector((cx, cy, 0.0)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
bpy.context.scene.camera = camera

bpy.ops.object.light_add(type='SUN', location=(cx - 400, cy - 400, 1500))
sun = bpy.context.object
sun.data.energy = 3.0
sun.rotation_euler = (0.35, -0.45, -0.35)

scene = bpy.context.scene
# Blender 5.2 exposes the Eevee Next renderer under the stable enum name.
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 1920
scene.render.resolution_y = 1080
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(OUTPUT)
scene.world.color = (0.004, 0.009, 0.018)
scene.render.film_transparent = False
bpy.ops.wm.save_as_mainfile(filepath='/Users/vincent/Work/RealGazebo/lfbd-bati3d-context.blend')
bpy.ops.render.render(write_still=True)
# A lower perspective establishes this as an actual 3D environment, rather
# than a flat tactical-map texture.
camera.data.type = 'PERSP'
camera.data.lens = 42
camera.data.clip_end = 7000.0
camera.location = Vector((cx - span * 0.65, cy - span * 0.85, 900.0))
camera.rotation_euler = (Vector((cx, cy, 5.0)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
scene.render.filepath = str(OBLIQUE_OUTPUT)
bpy.ops.render.render(write_still=True)
print('BLENDER_BATI3D_RENDER', OUTPUT, OBLIQUE_OUTPUT, 'meshes', len(objects), 'span_m', span)
