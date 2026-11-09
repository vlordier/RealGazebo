import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level("/RealGazebo/Maps/RealGazebo")
actor = next((a for a in unreal.EditorLevelLibrary.get_all_level_actors()
              if a.get_actor_label() == "BordeauxCUASSurroundings"), None)
if not actor:
    raise RuntimeError("BordeauxCUASSurroundings missing")
origin, extent = actor.get_actor_bounds(False)
component = actor.get_component_by_class(unreal.StaticMeshComponent)
mesh = component.get_editor_property("static_mesh")
print("C-UAS surroundings", "origin", origin, "extent", extent,
      "mesh", mesh.get_path_name(), "materials", component.get_num_materials())
