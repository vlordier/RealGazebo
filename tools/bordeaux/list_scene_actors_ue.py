import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level('/RealGazebo/Maps/RealGazebo')
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    label = actor.get_actor_label()
    name = actor.get_name()
    cls = actor.get_class().get_name()
    if any(token in (label + ' ' + cls).lower() for token in ('sky', 'fog', 'post', 'camera', 'bordeaux')):
        print('SCENE_ACTOR', label, name, cls, actor.get_actor_location())
