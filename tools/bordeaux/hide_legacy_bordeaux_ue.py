import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level('/RealGazebo/Maps/RealGazebo')
hidden = []
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label() in ('BordeauxNativeAirportDistrict', 'BordeauxCUASSurroundings'):
        actor.set_actor_hidden_in_game(True)
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if component:
            component.set_visibility(False, True)
        hidden.append(actor.get_actor_label())
unreal.EditorLevelLibrary.save_current_level()
print('Hidden legacy Bordeaux actors:', hidden)
