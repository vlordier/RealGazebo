"""Import the corrected per-tile Bordeaux FBX set into the Unreal map."""
import json
import os

import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
MANIFEST = '/private/tmp/bordeaux_processed_v2/manifest.json'
TILES_DIR = '/private/tmp/bordeaux_processed_v2/fbx'
DEST = '/RealGazebo/Bordeaux/Tiled'
LIMIT = int(os.environ.get('BORDEAUX_TILED_LIMIT', '0'))
START = int(os.environ.get('BORDEAUX_TILED_START', '0'))

with open(MANIFEST) as f:
    manifest = json.load(f)
tiles = manifest['tiles'][START:] if LIMIT <= 0 else manifest['tiles'][START:START + LIMIT]
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('could not load Bordeaux map')

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith('BordeauxTiled_'):
        unreal.EditorLevelLibrary.destroy_actor(actor)

tools = unreal.AssetToolsHelpers.get_asset_tools()


def bind_mesh_materials(mesh, material_paths):
    """Replace importer-generated material packages with plugin assets."""
    materials = []
    for path in material_paths:
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not material:
            raise RuntimeError(f'missing Bordeaux material: {path}')
        materials.append(material)
    if not materials:
        raise RuntimeError(f'no materials declared for {mesh.get_name()}')

    slots = list(mesh.get_editor_property('static_materials'))
    if not slots:
        raise RuntimeError(f'no material slots imported for {mesh.get_name()}')
    for slot, material_slot in enumerate(slots):
        material_slot.set_editor_property('material_interface', materials[slot % len(materials)])
    mesh.set_editor_property('static_materials', slots)
    unreal.EditorAssetLibrary.save_asset(path_for_asset(mesh))


def path_for_asset(asset):
    return asset.get_path_name().split('.')[0]


for info in tiles:
    name = info['name']
    source = os.path.join(TILES_DIR, name + '.fbx')
    path = DEST + '/' + name
    if not os.path.exists(source):
        print('SKIP missing', source)
        continue
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('filename', source)
    task.set_editor_property('destination_path', DEST)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('factory', unreal.FbxFactory())
    tools.import_asset_tasks([task])
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not mesh:
        print('FAILED import', name)
        continue
    bind_mesh_materials(mesh, info.get('materials', []))
    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
    if not actor:
        print('FAILED spawn', name)
        continue
    actor.set_actor_label('BordeauxTiled_' + name)
    # Assimp FBX export maps the preprocessed OBJ axes as (X, Z, Y). Rotate
    # once around X so the tile's thin source-Y height becomes Unreal Z and
    # the source-Z horizontal axis becomes Unreal Y.
    actor.set_actor_rotation(unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0), False)
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if component:
        component.set_collision_profile_name('BlockAll')
        component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        materials = []
        for mat_path in info.get('materials', []):
            material = unreal.EditorAssetLibrary.load_asset(mat_path)
            if material:
                materials.append(material)
        if materials:
            for slot in range(component.get_num_materials()):
                component.set_material(slot, materials[slot % len(materials)])
    origin, extent = actor.get_actor_bounds(False)
    print('TILED_IMPORT', name, 'origin', origin, 'extent', extent)

unreal.EditorLevelLibrary.save_current_level()
print('Installed corrected Bordeaux tiled FBX set:', len(tiles), 'tiles')
