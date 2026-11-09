"""Capture LFBD Bati3D via Unreal's editor viewport, not SceneCapture2D.

This is the robust visual-QA fallback for UE 5.8/macOS when an independent
off-screen SceneCapture2D returns only its target clear colour.  It captures
the editor's normal renderer through an explicit CameraActor; it is not a
sensor product and remains historical visual context only.
"""
import os
import time

import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
PREFIX = 'LFBD_Bati3D2012_'
OUT_DIR = os.environ.get('REALGAZEBO_CAPTURE_DIR', '/Users/vincent/Work/RealGazebo')
OUT_NAME = os.environ.get('REALGAZEBO_CAPTURE_NAME', 'lfbd-bati3d-viewport.png')

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('cannot load RealGazebo map')
tiles = [actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
         if actor.get_actor_label().startswith(PREFIX)]
if len(tiles) < 35:
    raise RuntimeError(f'expected meshes from 35 Bati3D tiles, found only {len(tiles)} actors')

bounds = [actor.get_actor_bounds(False) for actor in tiles]
axes = ('x', 'y', 'z')
mins = [min(getattr(origin, axis) - getattr(extent, axis) for origin, extent in bounds)
        for axis in axes]
maxs = [max(getattr(origin, axis) + getattr(extent, axis) for origin, extent in bounds)
        for axis in axes]
center = unreal.Vector(*((mins[i] + maxs[i]) * 0.5 for i in range(3)))
extent = unreal.Vector(*((maxs[i] - mins[i]) * 0.5 for i in range(3)))
location = unreal.Vector(center.x, center.y, center.z + max(extent.x, extent.y) * 1.75)
rotation = unreal.MathLibrary.find_look_at_rotation(location, center)

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_actor_label() == 'LFBD_Bati3D_ViewportCamera':
        unreal.EditorLevelLibrary.destroy_actor(actor)
camera = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.CameraActor, location, rotation)
camera.set_actor_label('LFBD_Bati3D_ViewportCamera')
camera.set_actor_location(location, False, False)
camera.set_actor_rotation(rotation, False)
unreal.EditorLevelLibrary.set_level_viewport_camera_info(location, rotation)

os.makedirs(OUT_DIR, exist_ok=True)
output = os.path.join(OUT_DIR, OUT_NAME)
task = unreal.AutomationLibrary.take_high_res_screenshot(
    1920, 1080, output, camera, False, False,
    unreal.ComparisonTolerance.LOW, 'LFBD historical context visual QA', 1.0, True)
deadline = time.monotonic() + 20.0
while time.monotonic() < deadline and not os.path.isfile(output):
    time.sleep(0.25)
unreal.EditorLevelLibrary.destroy_actor(camera)
if not os.path.isfile(output) or os.path.getsize(output) < 1024:
    raise RuntimeError(f'viewport capture missing or too small: {output}')
print('BATI3D_VIEWPORT_CAPTURE', output, 'actors', len(tiles), 'center', center, 'extent', extent)
