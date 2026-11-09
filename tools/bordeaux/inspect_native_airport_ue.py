import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level("/RealGazebo/Maps/RealGazebo")
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith("BordeauxNativeAirport"):
        origin, extent = actor.get_actor_bounds(False)
        print(f"{actor.get_actor_label()} loc={actor.get_actor_location()} scale={actor.get_actor_scale3d()} bounds origin={origin} extent={extent}")
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if component:
            print("  visible", component.is_visible(), "hidden", component.get_editor_property("hidden_in_game"),
                  "materials", [str(component.get_material(i)) for i in range(component.get_num_materials())])
