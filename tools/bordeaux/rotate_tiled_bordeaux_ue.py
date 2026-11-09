import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level('/RealGazebo/Maps/RealGazebo')
count = 0
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label().startswith('BordeauxTiled_'):
        actor.set_actor_rotation(unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0), False)
        count += 1
unreal.EditorLevelLibrary.save_current_level()
print('Rotated corrected Bordeaux tiles into Unreal Z-up:', count)
