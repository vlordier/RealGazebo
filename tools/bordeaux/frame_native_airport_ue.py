import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level("/RealGazebo/Maps/RealGazebo")
actors = unreal.EditorLevelLibrary.get_all_level_actors()
airport = next((a for a in actors if a.get_actor_label() == "BordeauxNativeAirportDistrict"), None)
camera = next((a for a in actors if a.get_actor_label() == "ValidationCamera"), None)
if not airport or not camera:
    raise RuntimeError("airport district or ValidationCamera missing")
center, extent = airport.get_actor_bounds(False)
height = max(extent.x, extent.y) * 1.35
camera.set_actor_location(unreal.Vector(center.x, center.y, center.z + height), False, False)
camera.set_actor_rotation(unreal.Rotator(roll=0, pitch=-90, yaw=0), False)
component = camera.get_component_by_class(unreal.CameraComponent)
if component:
    component.set_editor_property("field_of_view", 70.0)
unreal.EditorLevelLibrary.save_current_level()
print("Framed ValidationCamera", center, extent, "height", height)
