"""Capture the ValidationCamera through a real SceneCapture, not the editor viewport."""
import os
import time

import unreal

MAP = "/RealGazebo/Maps/RealGazebo"
OUT_DIR = os.environ.get("REALGAZEBO_CAPTURE_DIR", "/Users/vincent/Work/RealGazebo")
OUT_NAME = os.environ.get("REALGAZEBO_CAPTURE_NAME", "airport-scene-capture.png")

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("cannot load airport map")

actors = unreal.EditorLevelLibrary.get_all_level_actors()
tiled = [actor for actor in actors if actor.get_actor_label().startswith("BordeauxTiled_")]
if os.environ.get("REALGAZEBO_CAPTURE_NATIVE") == "1":
    tiled = []
if os.environ.get("REALGAZEBO_CAPTURE_SINGLE_TILE") == "1" and tiled:
    tiled = [tiled[len(tiled) // 2]]
airport = next((actor for actor in actors
                if actor.get_actor_label() == "BordeauxNativeAirportDistrict"), None)
if not tiled and not airport:
    raise RuntimeError("Bordeaux content missing: no native airport or tiled actor loaded")

# Imported editor actors must also be visible to a game-world SceneCapture.
# Explicitly restore these flags because the preparation/audit passes may have
# hidden legacy geometry in the same map.
for actor in tiled + ([airport] if airport else []):
    actor.set_actor_hidden_in_game(False)
    actor.set_is_temporarily_hidden_in_editor(False)
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if component:
        component.set_visibility(True, True)
        if os.environ.get("REALGAZEBO_CAPTURE_NEUTRAL_MATERIAL") == "1":
            neutral = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")
            for slot in range(component.get_num_materials()):
                component.set_material(slot, neutral)
        if os.environ.get("REALGAZEBO_CAPTURE_REVERSE_CULLING") == "1":
            component.set_editor_property("reverse_culling", True)

if tiled:
    bounds = [actor.get_actor_bounds(False) for actor in tiled]
    mins = [
        min(origin.x - size.x for origin, size in bounds),
        min(origin.y - size.y for origin, size in bounds),
        min(origin.z - size.z for origin, size in bounds),
    ]
    maxs = [
        max(origin.x + size.x for origin, size in bounds),
        max(origin.y + size.y for origin, size in bounds),
        max(origin.z + size.z for origin, size in bounds),
    ]
    center = unreal.Vector(*((mins[i] + maxs[i]) * 0.5 for i in range(3)))
    extent = unreal.Vector(*((maxs[i] - mins[i]) * 0.5 for i in range(3)))
else:
    center, extent = airport.get_actor_bounds(False)
height = max(extent.x, extent.y) * 1.35
height = max(extent.x, extent.y) * (
    0.75 if os.environ.get("REALGAZEBO_CAPTURE_CLOSE_ALT") == "1" else 2.0)
capture_location = unreal.Vector(center.x, center.y, center.z + height)
capture_rotation = unreal.Rotator(pitch=-90.0, yaw=0.0, roll=0.0)
print("Capturing airport bounds", center, extent, "from", capture_location, capture_rotation)

world = unreal.EditorLevelLibrary.get_editor_world()
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label() == "BordeauxValidationCapture":
        unreal.EditorLevelLibrary.destroy_actor(actor)

capture_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SceneCapture2D, unreal.Vector(0, 0, 0), unreal.Rotator())
capture_actor.set_actor_label("BordeauxValidationCapture")
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
# Move the owning actor, rather than only the component.  In UE5.8 the
# component's world transform can be reset during registration, which leaves
# a SceneCapture rendering its clear color even though the script reports a
# successful capture.
capture_actor.set_actor_location(capture_location, False, False)
capture_actor.set_actor_rotation(capture_rotation, False)
capture.set_editor_property("capture_every_frame", False)
capture.set_editor_property("capture_on_movement", False)
full_scene = os.environ.get("REALGAZEBO_CAPTURE_FULL_SCENE") == "1"
capture.set_editor_property(
    "primitive_render_mode",
    unreal.SceneCapturePrimitiveRenderMode.PRM_RENDER_SCENE_PRIMITIVES
    if full_scene else unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST,
)
capture.set_world_location(capture_location, False, False)
capture.set_world_rotation(capture_rotation, False, False)
capture.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property("ortho_width", max(extent.x, extent.y) * 2.2)
capture.set_editor_property("max_view_distance_override", height * 4.0)
capture.set_editor_property("auto_calculate_ortho_planes", True)
capture.set_editor_property("update_ortho_planes", True)
debug_capture = os.environ.get("REALGAZEBO_CAPTURE_DEBUG_CUBE") == "1"
if not debug_capture and not full_scene:
    for actor in tiled:
        capture.show_only_actor_components(actor)
target = unreal.RenderingLibrary.create_render_target2d(
    world, 1920, 1080, unreal.TextureRenderTargetFormat.RTF_RGBA8)
target.set_editor_property("clear_color", unreal.LinearColor(0.035, 0.075, 0.13, 1.0))
capture.set_editor_property("texture_target", target)
capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR)
sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight, center + unreal.Vector(0, 0, height * 0.5),
    unreal.Rotator(pitch=-55.0, yaw=-35.0, roll=0.0))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 12.0)
sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyLight, center + unreal.Vector(0, 0, height * 0.25), unreal.Rotator())
sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("intensity", 2.0)
sky.get_component_by_class(unreal.SkyLightComponent).recapture_sky()
debug_cube = None
if debug_capture:
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    debug_cube = unreal.EditorLevelLibrary.spawn_actor_from_object(cube, center)
    debug_cube.set_actor_scale3d(unreal.Vector(1000, 1000, 1000))
    capture.show_only_actor_components(debug_cube)
time.sleep(1.0)
capture.capture_scene()
time.sleep(1.0)
capture.capture_scene()
os.makedirs(OUT_DIR, exist_ok=True)
unreal.RenderingLibrary.export_render_target(world, target, OUT_DIR, OUT_NAME)
if debug_cube:
    unreal.EditorLevelLibrary.destroy_actor(debug_cube)
unreal.EditorLevelLibrary.destroy_actor(sun)
unreal.EditorLevelLibrary.destroy_actor(sky)
unreal.EditorLevelLibrary.destroy_actor(capture_actor)
output_path = os.path.join(OUT_DIR, OUT_NAME)
if not os.path.isfile(output_path) or os.path.getsize(output_path) < 1024:
    raise RuntimeError(f"SceneCapture export missing or too small: {output_path}")
print("Captured", output_path)
