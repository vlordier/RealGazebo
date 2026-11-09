"""Prove that a UE SceneCapture2D sees a known mesh before capturing Bati3D.

This deliberately does not touch persistent map content.  A wide cube is put
in the exact Bati3D capture frame, rendered by the same SceneCapture path,
then removed.  If this image is not an obvious rectangle, any Bati3D capture
would be meaningless and the fault is the capture setup, not the source data.
"""
import os
import time

import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
PREFIX = 'LFBD_Bati3D2012_'
OUT_DIR = '/Users/vincent/Work/RealGazebo'
OUT_NAME = 'scene-capture-proof.png'

if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP):
    raise RuntimeError('cannot load RealGazebo map')
tiles = [a for a in unreal.EditorLevelLibrary.get_all_level_actors()
         if a.get_actor_label().startswith(PREFIX)]
if len(tiles) < 35:
    raise RuntimeError(f'expected >=35 Bati3D tiles, found {len(tiles)}')

bounds = [a.get_actor_bounds(False) for a in tiles]
axes = ('x', 'y', 'z')
mins = [min(getattr(o, axis) - getattr(e, axis) for o, e in bounds) for axis in axes]
maxs = [max(getattr(o, axis) + getattr(e, axis) for o, e in bounds) for axis in axes]
center = unreal.Vector(*((mins[i] + maxs[i]) * 0.5 for i in range(3)))
extent = unreal.Vector(*((maxs[i] - mins[i]) * 0.5 for i in range(3)))
frame = max(extent.x, extent.y) * 2.2
height = max(extent.x, extent.y) * 2.0
world = unreal.EditorLevelLibrary.get_editor_world()

# A 650 m x 360 m x 20 m slab. Its rectangular top must be unmistakable.
proof = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.StaticMeshActor, center + unreal.Vector(0, 0, 1000), unreal.Rotator())
proof.set_actor_label('LFBD_SceneCapture_ProofSlab')
proof_component = proof.get_component_by_class(unreal.StaticMeshComponent)
proof_component.set_editor_property('static_mesh', unreal.load_asset('/Engine/BasicShapes/Cube.Cube'))
proof_component.set_material(0, unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'))
proof.set_actor_scale3d(unreal.Vector(650.0, 360.0, 20.0))

capture_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SceneCapture2D, unreal.Vector(0, 0, 0), unreal.Rotator())
capture_actor.set_actor_location(center + unreal.Vector(0, 0, height), False, False)
top_down = unreal.Rotator(pitch=-90.0, yaw=0.0, roll=0.0)
capture_actor.set_actor_rotation(top_down, False)
capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
# Explicitly move the registered component too.  Actor transforms alone can
# leave a one-shot editor capture at its construction transform on macOS.
capture.set_world_location(center + unreal.Vector(0, 0, height), False, False)
capture.set_world_rotation(top_down, False, False)
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
# UE 5.8 exposes this list as a runtime-only property: populate it through
# the component API rather than attempting to assign the template property.
capture.show_only_actor_components(proof)
capture.set_editor_property('projection_type', unreal.CameraProjectionMode.ORTHOGRAPHIC)
capture.set_editor_property('ortho_width', frame)
capture.set_editor_property('auto_calculate_ortho_planes', True)
capture.set_editor_property('update_ortho_planes', True)
target = unreal.RenderingLibrary.create_render_target2d(world, 1920, 1080, unreal.TextureRenderTargetFormat.RTF_RGBA8)
target.set_editor_property('clear_color', unreal.LinearColor(0.12, 0.02, 0.02, 1.0))
capture.set_editor_property('texture_target', target)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR)
print('SCENE_CAPTURE_PROOF_STATE', 'proof', proof.get_actor_location(), proof.get_actor_bounds(False),
      'capture_actor', capture_actor.get_actor_location(), capture_actor.get_actor_rotation(),
      'capture_component', capture.get_world_location(), capture.get_world_rotation(), 'ortho', frame)

sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight, center + unreal.Vector(0, 0, height * 0.5),
    unreal.Rotator(pitch=-55.0, yaw=-35.0, roll=0.0))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('intensity', 20.0)
time.sleep(1.0)
capture.capture_scene()
time.sleep(1.0)
os.makedirs(OUT_DIR, exist_ok=True)
unreal.RenderingLibrary.export_render_target(world, target, OUT_DIR, OUT_NAME)
for actor in (sun, capture_actor, proof):
    unreal.EditorLevelLibrary.destroy_actor(actor)
output = os.path.join(OUT_DIR, OUT_NAME)
if not os.path.isfile(output) or os.path.getsize(output) < 1024:
    raise RuntimeError(f'proof capture missing: {output}')
print('SCENE_CAPTURE_PROOF', output, 'center', center, 'frame', frame)
