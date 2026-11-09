"""Install a prepared Bati3D 2012 visual-context layer into RealGazebo.

The layer is intentionally separate from operational airport geometry.  Its
actor labels and tags make the historical 2012 / context-only boundary visible
to the editor and any scene inspection tooling.
"""
import json
import os

import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
MANIFEST = os.environ.get('LFBD_BATI3D_MANIFEST', '/private/tmp/lfbd_bati3d_core/manifest.json')
FBX_DIR = os.path.dirname(MANIFEST)
DEST = '/RealGazebo/Bordeaux/Bati3D_2012'
PREFIX = 'LFBD_Bati3D2012_'
# The Bati3D 3DS coordinates are metres while the host world grid below is
# centimetres. Correct only these explicitly tagged visual-context actors.
# This layer is never airport geometry, collision, obstacle, or navigation
# authority.
CONTEXT_GEOMETRY_SCALE = 100.0
# The source FBX carries 3DS axes (X=east, Z=north, Y=height).  Unreal's
# ground plane is X/Y with Z up, so roll -90° maps the historical tiles into
# the correct world frame.  The transform applies only to this tagged,
# historical visual-context layer.
SOURCE_TO_UNREAL_ROTATION = unreal.Rotator(pitch=0.0, yaw=0.0, roll=-90.0)

with open(MANIFEST) as source:
    manifest = json.load(source)
if manifest.get('provenance', {}).get('classification') != 'historical_open_context':
    raise RuntimeError('refusing unclassified Bati3D import')
if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP):
    raise RuntimeError('could not load RealGazebo map')

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith(PREFIX):
        unreal.EditorLevelLibrary.destroy_actor(actor)

# This namespace is owned exclusively by the generated historical layer.
# Clearing it makes a reimport deterministic while leaving all native airport
# and user-authored assets untouched.
if unreal.EditorAssetLibrary.does_directory_exist(DEST):
    unreal.EditorAssetLibrary.delete_directory(DEST)

tools = unreal.AssetToolsHelpers.get_asset_tools()
reference_easting, reference_northing = manifest['reference']['epsg3945_easting_northing']
for tile in manifest['tiles']:
    name = tile['id']
    source = os.path.join(FBX_DIR, tile['fbx'])
    if not os.path.exists(source):
        raise RuntimeError('missing prepared FBX: ' + source)
    task = unreal.AssetImportTask()
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('filename', source)
    task.set_editor_property('destination_path', DEST)
    task.set_editor_property('destination_name', name)
    # Import mesh, material and texture packages as one coherent FBX unit.
    # Leaving the factory to its defaults created StaticMeshes whose m0/m00
    # material dependencies did not exist after a deterministic reimport.
    options = unreal.FbxImportUI()
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_as_skeletal', False)
    options.set_editor_property('import_materials', True)
    options.set_editor_property('import_textures', True)
    task.set_editor_property('options', options)
    tools.import_asset_tasks([task])
    meshes = []
    for imported_path in task.get_editor_property('imported_object_paths'):
        imported = unreal.EditorAssetLibrary.load_asset(imported_path)
        if isinstance(imported, unreal.StaticMesh):
            meshes.append(imported)
    if not meshes:
        raise RuntimeError('failed to resolve static meshes for ' + name)
    west, south, east, north = tile['epsg3945_bbox']
    location = unreal.Vector((west + east - 2 * reference_easting) * 50,
                             (south + north - 2 * reference_northing) * 50, 0)
    for mesh_index, mesh in enumerate(meshes):
        actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, location)
        if not actor:
            raise RuntimeError('failed to spawn ' + name)
        # One source tile can contain several independently materialled meshes.
        # Retaining them all is required to render the supplied 3DS scene.
        actor.set_actor_label(f'{PREFIX}{name}__{mesh_index:02d}')
        actor.set_actor_rotation(SOURCE_TO_UNREAL_ROTATION, False)
        actor.set_actor_scale3d(unreal.Vector(
            CONTEXT_GEOMETRY_SCALE, CONTEXT_GEOMETRY_SCALE, CONTEXT_GEOMETRY_SCALE))
        actor.set_editor_property('tags', [unreal.Name('FuriaContextOnly'), unreal.Name('HistoricalBati3D2012'), unreal.Name('NoOperationalAuthority')])
    print('BATI3D_IMPORT', name, 'meshes', len(meshes), location)

unreal.EditorLevelLibrary.save_current_level()
print('Installed historical Bati3D context tiles:', len(manifest['tiles']))
