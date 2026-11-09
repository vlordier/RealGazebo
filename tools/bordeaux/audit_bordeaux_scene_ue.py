"""Print the visible Bordeaux scene inventory for unattended render audits."""
import unreal

MAP = "/RealGazebo/Maps/RealGazebo"

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("cannot load airport map")

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not component:
        continue
    origin, extent = actor.get_actor_bounds(False)
    if max(extent.x, extent.y) < 1000:
        continue
    materials = []
    for index in range(component.get_num_materials()):
        material = component.get_material(index)
        materials.append(material.get_path_name() if material else "None")
    print("SCENE_ACTOR", actor.get_actor_label(), "bounds", origin, extent,
          "materials", materials)
