"""Install the faithful native Bordeaux airport subset in the active UE map."""
import unreal

MAP = "/RealGazebo/Maps/RealGazebo"
SOURCE = "/private/tmp/bordeaux_native_airport/Bordeaux_Airport_Runways.fbx"
ASSET = "/RealGazebo/Bordeaux/Native/Bordeaux_Airport_Runways"

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError(f"cannot load {MAP}")

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith("Tile_") or actor.get_actor_label() == "BordeauxNativeAirport":
        unreal.EditorLevelLibrary.destroy_actor(actor)

for asset_data in unreal.EditorAssetLibrary.list_assets("/RealGazebo/Bordeaux/Native", recursive=True):
    if asset_data.split(".")[0].rsplit("/", 1)[-1].startswith("Bordeaux_Airport_Runways"):
        unreal.EditorAssetLibrary.delete_asset(asset_data)

task = unreal.AssetImportTask()
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", True)
task.set_editor_property("save", True)
task.set_editor_property("filename", SOURCE)
task.set_editor_property("destination_path", "/RealGazebo/Bordeaux/Native")
task.set_editor_property("destination_name", "Bordeaux_Airport_Runways")
task.set_editor_property("factory", unreal.FbxFactory())
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

material = unreal.EditorAssetLibrary.load_asset("/RealGazebo/Bordeaux/Materials/M_Aeroway_Asphalt")
assets = unreal.EditorAssetLibrary.list_assets("/RealGazebo/Bordeaux/Native", recursive=True)
meshes = [unreal.EditorAssetLibrary.load_asset(path) for path in assets
          if path.split(".")[0].rsplit("/", 1)[-1].startswith("Bordeaux_Airport_Runways")]
meshes = [mesh for mesh in meshes if isinstance(mesh, unreal.StaticMesh)]
if not meshes:
    raise RuntimeError("native airport FBX import produced no static meshes")
for index, mesh in enumerate(meshes):
    slots = list(mesh.get_editor_property("static_materials"))
    for slot in slots:
        slot.set_editor_property("material_interface", material)
    mesh.set_editor_property("static_materials", slots)
    unreal.EditorAssetLibrary.save_asset(mesh.get_path_name().split(".")[0])
    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, unreal.Vector(85800, -114400, 0))
    actor.set_actor_label(f"BordeauxNativeAirport_{index:02d}")
    actor.set_actor_scale3d(unreal.Vector(14.3, 14.3, 14.3))
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if component:
        component.set_material(0, material)
unreal.EditorLevelLibrary.save_current_level()
print(f"Installed {len(meshes)} native airport meshes")
