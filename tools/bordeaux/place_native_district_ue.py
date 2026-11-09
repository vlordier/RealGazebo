import unreal

MAP = "/RealGazebo/Maps/RealGazebo"
ASSET = "/RealGazebo/Bordeaux/Native/Bordeaux_Airport_District"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("cannot load map")
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith("BordeauxNativeAirport"):
        unreal.EditorLevelLibrary.destroy_actor(actor)
mesh = unreal.EditorAssetLibrary.load_asset(ASSET)
if not mesh:
    raise RuntimeError("unified airport asset is unavailable")
actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, unreal.Vector(85800, -114400, 0))
actor.set_actor_label("BordeauxNativeAirportDistrict")
actor.set_actor_scale3d(unreal.Vector(14.3, 14.3, 14.3))
material = unreal.EditorAssetLibrary.load_asset("/RealGazebo/Bordeaux/Materials/M_Aeroway_Asphalt")
component = actor.get_component_by_class(unreal.StaticMeshComponent)
for index in range(component.get_num_materials()):
    component.set_material(index, material)

# Add a visible terrain base under the airport.  The supplied GIS model has
# hard surfaces but no continuous terrain mesh, so without this the aerial
# view is dominated by the sky colour and looks like missing geometry.
for existing in unreal.EditorLevelLibrary.get_all_level_actors():
    if existing.get_actor_label() == "BordeauxAirportGround":
        unreal.EditorLevelLibrary.destroy_actor(existing)
ground_mesh = unreal.load_asset("/Engine/BasicShapes/Plane.Plane")
ground = unreal.EditorLevelLibrary.spawn_actor_from_object(ground_mesh, unreal.Vector(0, 0, -20))
ground.set_actor_label("BordeauxAirportGround")
ground.set_actor_scale3d(unreal.Vector(6000, 6000, 1))
ground_component = ground.get_component_by_class(unreal.StaticMeshComponent)
grass = unreal.EditorAssetLibrary.load_asset("/RealGazebo/Bordeaux/Materials/M_Grass")
ground_component.set_material(0, grass)
unreal.EditorLevelLibrary.save_current_level()
print("Placed unified Bordeaux airport district")
