import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level("/RealGazebo/Maps/RealGazebo"):
    raise RuntimeError("cannot load airport map")

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    label = actor.get_actor_label()
    if (label.startswith("BordeauxNativeAirport") or label == "BordeauxAirportGround" or
            "Fog" in actor.get_class().get_name() or "PostProcess" in actor.get_class().get_name()):
        print("ACTOR", label, actor.get_class().get_name(), actor.get_actor_location(), actor.get_actor_scale3d())
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if component:
            print("  MATERIALS", component.get_num_materials(),
                  [str(component.get_material(i).get_path_name()) if component.get_material(i) else "None"
                   for i in range(component.get_num_materials())])

for path in ("/RealGazebo/Bordeaux/Materials/M_Aeroway_Asphalt",
             "/RealGazebo/Bordeaux/Materials/M_Grass"):
    material = unreal.EditorAssetLibrary.load_asset(path)
    print("MATERIAL", path, "two_sided=", material.get_editor_property("two_sided"),
          "blend=", material.get_editor_property("blend_mode"),
          "shading=", material.get_editor_property("shading_model"))
