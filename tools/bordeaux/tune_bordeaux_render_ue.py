"""Tune Bordeaux materials and exposure for an inspectable Unreal capture."""
import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
MATERIALS = {
    'M_Aeroway_Asphalt': (0.035, 0.045, 0.055),
    'M_Road_Asphalt': (0.070, 0.075, 0.085),
    'M_Building_Generic': (0.28, 0.20, 0.12),
    'M_Grass': (0.035, 0.14, 0.045),
    'M_Landuse': (0.16, 0.14, 0.07),
    'M_Rail': (0.09, 0.09, 0.10),
    'M_Ground': (0.16, 0.14, 0.10),
}

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level(MAP)

for name, rgb in MATERIALS.items():
    path = '/RealGazebo/Bordeaux/Materials/' + name
    material = unreal.EditorAssetLibrary.load_asset(path)
    if not material:
        continue
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property('two_sided', True)
    base = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    base.set_editor_property('constant', unreal.LinearColor(*rgb, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -300, 120)
    rough.set_editor_property('r', 0.86)
    unreal.MaterialEditingLibrary.connect_material_property(
        rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_class().get_name() == 'PostProcessVolume':
        settings = actor.settings
        settings.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
        settings.set_editor_property('auto_exposure_bias', -2.0)
        settings.set_editor_property('bloom_intensity', 0.0)
        print('Tuned PostProcessVolume exposure to -2.0')
    if actor.get_actor_label() == 'SM_SkySphere':
        actor.set_actor_hidden_in_game(True)
        actor.set_is_temporarily_hidden_in_editor(True)

unreal.EditorLevelLibrary.save_current_level()
print('Tuned Bordeaux materials:', len(MATERIALS))
