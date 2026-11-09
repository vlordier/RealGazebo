import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level('/RealGazebo/Maps/RealGazebo')
actors = [a for a in unreal.EditorLevelLibrary.get_all_level_actors()
          if a.get_actor_label().startswith('BordeauxTiled_')]
if not actors:
    raise RuntimeError('no BordeauxTiled actors found')
mins = [float('inf')] * 3
maxs = [float('-inf')] * 3
for actor in actors:
    origin, extent = actor.get_actor_bounds(False)
    values = ((origin.x, extent.x), (origin.y, extent.y), (origin.z, extent.z))
    mins = [min(mins[i], values[i][0] - values[i][1]) for i in range(3)]
    maxs = [max(maxs[i], values[i][0] + values[i][1]) for i in range(3)]
print('TILED_SCENE actors', len(actors), 'min', mins, 'max', maxs,
      'size', [maxs[i] - mins[i] for i in range(3)])
