"""Make the airport validation map readable and independent of its host lighting."""
import unreal

MAP = "/RealGazebo/Maps/RealGazebo"
MATERIALS = {
    "/RealGazebo/Bordeaux/Materials/M_Aeroway_Asphalt": (0.22, 0.24, 0.27),
    "/RealGazebo/Bordeaux/Materials/M_Grass": (0.045, 0.180, 0.055),
}

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("cannot load airport map")

# The inherited map fog and post-process volume were overriding the validation
# view with a near-white exposure.  This airport validation setup is deliberately
# self-lit, so it stays inspectable in unattended renders and in the editor.
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    class_name = actor.get_class().get_name()
    if class_name in ("ExponentialHeightFog", "PostProcessVolume"):
        print("Removing visual override:", actor.get_actor_label(), class_name)
        unreal.EditorLevelLibrary.destroy_actor(actor)

for path, color in MATERIALS.items():
    material = unreal.EditorAssetLibrary.load_asset(path)
    if not material:
        raise RuntimeError("missing material: " + path)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    emissive = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -320, 0)
    emissive.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)
    print("Configured unlit validation material:", path)

unreal.EditorLevelLibrary.save_current_level()
print("Saved readable airport validation map")
