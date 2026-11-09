"""Print the UE 5.8 Python signatures used by the visual QA capture path."""
import unreal

for owner, name in (
    (unreal.AutomationLibrary, 'take_high_res_screenshot'),
    (unreal.EditorLevelLibrary, 'set_level_viewport_camera_info'),
    (unreal.EditorLevelLibrary, 'get_level_viewport_camera_info'),
):
    method = getattr(owner, name, None)
    print('UE_CAPTURE_API', owner.__name__, name, method.__doc__ if method else 'MISSING')
