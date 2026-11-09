import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level("/RealGazebo/Maps/RealGazebo")
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label() == "BordeauxAirportGround":
        unreal.EditorLevelLibrary.destroy_actor(actor)
        print("Removed occluding validation ground")
unreal.EditorLevelLibrary.save_current_level()
