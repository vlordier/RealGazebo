"""Report render-relevant state of the installed LFBD Bati3D context layer."""
import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
PREFIX = 'LFBD_Bati3D2012_'

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('cannot load RealGazebo map')

tiles = [actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
         if actor.get_actor_label().startswith(PREFIX)]
print('BATI3D_INSPECT actors', len(tiles))
reported = []
for actor in tiles:
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    mesh = component.get_editor_property('static_mesh') if component else None
    origin, extent = actor.get_actor_bounds(False)
    reported.append((max(extent.x, extent.y, extent.z), actor, component, mesh, origin, extent))
for _, actor, component, mesh, origin, extent in sorted(reported, reverse=True, key=lambda item: item[0])[:20]:
    print('BATI3D_TILE', actor.get_actor_label(),
          'location', actor.get_actor_location(),
          'bounds', origin, extent,
          'hidden_game', actor.get_editor_property('hidden'),
          'visible', component.is_visible() if component else False,
          'mesh', mesh.get_path_name() if mesh else 'NONE',
          'materials', len(component.get_materials()) if component else 0)
