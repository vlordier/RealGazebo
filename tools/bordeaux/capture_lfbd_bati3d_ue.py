"""Render the installed historical LFBD Bati3D context layer for visual QA."""
import os
import time

import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
PREFIX = 'LFBD_Bati3D2012_'
OUT_DIR = os.environ.get('REALGAZEBO_CAPTURE_DIR', '/Users/vincent/Work/RealGazebo')
OUT_NAME = os.environ.get('REALGAZEBO_CAPTURE_NAME', 'lfbd-bati3d-context.png')

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('cannot load RealGazebo map')
tiles = [actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
         if actor.get_actor_label().startswith(PREFIX)]
if len(tiles) < 35:
    raise RuntimeError(f'expected meshes from 35 Bati3D context tiles, found only {len(tiles)} actors')

for actor in tiles:
    actor.set_actor_hidden_in_game(False)
    actor.set_is_temporarily_hidden_in_editor(False)
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if component:
        component.set_visibility(True, True)
        # The historic 3DS export uses the opposite face winding to UE's
        # top-down view.  Reverse culling for this non-authoritative context
        # layer so roofs and building outlines remain visible in QA captures.
        component.set_editor_property('reverse_culling', True)

# FBX material fidelity is validated separately.  For geometry QA, use an
# engine-owned neutral material so a source-material import quirk cannot turn
# the entire historical layer black and hide valid meshes.
qa_material = unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial')
if not qa_material:
    raise RuntimeError('missing Engine basic material for Bati3D geometry QA')
for actor in tiles:
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if component:
        for material_index in range(len(component.get_materials())):
            component.set_material(material_index, qa_material)

bounds = [actor.get_actor_bounds(False) for actor in tiles]
# Unreal's Python Vector is attribute-based rather than indexable.
axes = ('x', 'y', 'z')
mins = [min(getattr(origin, axis) - getattr(extent, axis) for origin, extent in bounds)
        for axis in axes]
maxs = [max(getattr(origin, axis) + getattr(extent, axis) for origin, extent in bounds)
        for axis in axes]
center = unreal.Vector(*((mins[i] + maxs[i]) * 0.5 for i in range(3)))
extent = unreal.Vector(*((maxs[i] - mins[i]) * 0.5 for i in range(3)))
# A plan-view QA capture is stable across editor/game worlds and does not rely
# on perspective near/far clipping at kilometre scale.
height = max(extent.x, extent.y) * 2.0
location = unreal.Vector(center.x, center.y, center.z + height)
rotation = unreal.Rotator(pitch=-90.0, yaw=0.0, roll=0.0)

world = unreal.EditorLevelLibrary.get_editor_world()
# Set the transform on the owning actor after registration.  On UE 5.8/macOS
# passing a transform to spawn_actor_from_class can leave SceneCapture2D at
# its default transform, yielding only the clear colour.
capture_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SceneCapture2D, unreal.Vector(0, 0, 0), unreal.Rotator())
capture_actor.set_actor_label('LFBD_Bati3D_ContextCapture')
capture_actor.set_actor_location(location, False, False)
capture_actor.set_actor_rotation(rotation, False)
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
# Force the registered capture component to the same transform.  This avoids
# the construction-transform bug observed in one-shot editor captures.
capture.set_world_location(location, False, False)
capture.set_world_rotation(rotation, False, False)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('max_view_distance_override', -1.0)
capture.set_editor_property(
    'primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
# ShowOnlyActors is runtime-only in UE 5.8/macOS.  The component method keeps
# a real component list; assigning the reflected property is silently invalid
# in some editor contexts and produced an empty/incorrect capture.
for actor in tiles:
    capture.show_only_actor_components(actor)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', max(extent.x, extent.y) * 2.2)
capture.set_editor_property('auto_calculate_ortho_planes', True)
capture.set_editor_property('update_ortho_planes', True)
# One-shot capture: UE documents capture_scene() as mutually exclusive with
# capture-every-frame.  Keep the default primitive mode to render the full
# scene, rather than relying on version-specific enum aliases.
# UE 5.8 exposes render-target creation via RenderingLibrary (the older
# TextureRenderTarget2D.init_auto_format binding is not present on macOS).
target = unreal.RenderingLibrary.create_render_target2d(
    world, 1920, 1080, unreal.TextureRenderTargetFormat.RTF_RGBA8)
target.set_editor_property('clear_color', unreal.LinearColor(0.035, 0.075, 0.13, 1.0))
capture.set_editor_property('texture_target', target)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR)
# The visual-context import is inspected outside the playable lighting setup.
# Use a temporary neutral rig so this QA image validates geometry and material
# visibility rather than the availability of an unrelated level light rig.
sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight, center + unreal.Vector(0, 0, height * 0.5),
    unreal.Rotator(pitch=-55.0, yaw=-35.0, roll=0.0))
sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_component.set_editor_property('intensity', 12.0)
sun_component.set_editor_property('light_color', unreal.LinearColor(1.0, 0.96, 0.88, 1.0))
sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyLight, center + unreal.Vector(0, 0, height * 0.25), unreal.Rotator())
sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
sky_component.set_editor_property('intensity', 2.0)
sky_component.recapture_sky()
time.sleep(1.0)  # allow actor/component registration before the one-shot draw
capture.capture_scene()
time.sleep(1.0)
os.makedirs(OUT_DIR, exist_ok=True)
unreal.RenderingLibrary.export_render_target(world, target, OUT_DIR, OUT_NAME)
unreal.EditorLevelLibrary.destroy_actor(sun)
unreal.EditorLevelLibrary.destroy_actor(sky)
unreal.EditorLevelLibrary.destroy_actor(capture_actor)
output = os.path.join(OUT_DIR, OUT_NAME)
if not os.path.isfile(output) or os.path.getsize(output) < 1024:
    raise RuntimeError(f'capture missing or too small: {output}')
print('BATI3D_CAPTURE', output, 'center', center, 'extent', extent)
