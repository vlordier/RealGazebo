import unreal

MAP = "/RealGazebo/Maps/RealGazebo"
SOURCE = "/private/tmp/bordeaux_native_airport/Bordeaux_Airport_District.fbx"
DEST = "/RealGazebo/Bordeaux/Native"
NAME = "Bordeaux_Airport_District"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level(MAP)
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith("BordeauxNativeAirport"):
        unreal.EditorLevelLibrary.destroy_actor(actor)
path = f"{DEST}/{NAME}"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
task = unreal.AssetImportTask()
task.set_editor_property("automated", True)
task.set_editor_property("save", True)
task.set_editor_property("filename", SOURCE)
task.set_editor_property("destination_path", DEST)
task.set_editor_property("destination_name", NAME)
task.set_editor_property("factory", unreal.FbxFactory())
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = unreal.EditorAssetLibrary.load_asset(path)
if not mesh:
    raise RuntimeError("district import failed")
actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
actor.set_actor_label("BordeauxNativeAirportDistrict")
# The source coordinates are metres. UE imports Blender FBX at centimetres;
# Bordeaux's source model is approximately 1:10, so this restores runway scale
# without retaining any global-coordinate displacement.
actor.set_actor_scale3d(unreal.Vector(10.0, 10.0, 10.0))
material = unreal.EditorAssetLibrary.load_asset("/RealGazebo/Bordeaux/Materials/M_Aeroway_Asphalt")
component = actor.get_component_by_class(unreal.StaticMeshComponent)
component.set_material(0, material)
unreal.EditorLevelLibrary.save_current_level()
print("Installed unified native airport district")
