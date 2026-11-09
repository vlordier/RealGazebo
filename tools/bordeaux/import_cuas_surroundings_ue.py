import unreal

MAP = "/RealGazebo/Maps/RealGazebo"
SOURCE = "/private/tmp/bordeaux_native_airport/Bordeaux_CUAS_Surroundings.fbx"
ASSET = "/RealGazebo/Bordeaux/Native/Bordeaux_CUAS_Surroundings"

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("cannot load map")
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label() == "BordeauxCUASSurroundings":
        unreal.EditorLevelLibrary.destroy_actor(actor)
if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.EditorAssetLibrary.delete_asset(ASSET)
task = unreal.AssetImportTask()
task.set_editor_property("automated", True)
task.set_editor_property("save", True)
task.set_editor_property("filename", SOURCE)
task.set_editor_property("destination_path", "/RealGazebo/Bordeaux/Native")
task.set_editor_property("destination_name", "Bordeaux_CUAS_Surroundings")
task.set_editor_property("factory", unreal.FbxFactory())
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = unreal.EditorAssetLibrary.load_asset(ASSET)
if not mesh:
    raise RuntimeError("surroundings import failed")
materials = [unreal.EditorAssetLibrary.load_asset(path) for path in (
    "/RealGazebo/Bordeaux/Materials/M_Grass",
    "/RealGazebo/Bordeaux/Materials/M_Building_Generic",
    "/RealGazebo/Bordeaux/Materials/M_Road_Asphalt",
)]
slots = list(mesh.get_editor_property("static_materials"))
for index, slot in enumerate(slots):
    slot.set_editor_property("material_interface", materials[index % len(materials)])
mesh.set_editor_property("static_materials", slots)
unreal.EditorAssetLibrary.save_asset(ASSET)
actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
actor.set_actor_label("BordeauxCUASSurroundings")
actor.set_actor_scale3d(unreal.Vector(10, 10, 10))
component = actor.get_component_by_class(unreal.StaticMeshComponent)
component.set_collision_profile_name("BlockAll")
component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
unreal.EditorLevelLibrary.save_current_level()
print("Installed Bordeaux C-UAS surroundings")
