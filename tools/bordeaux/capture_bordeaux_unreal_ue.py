"""Capture a real Unreal viewport frame of the imported Bordeaux layers."""
import glob
import os
import shutil
import time

import unreal

MAP = '/RealGazebo/Maps/RealGazebo'
OUT = ('/Users/vincent/Work/RealGazebo/bordeaux-unreal-airport-close.png'
       if os.environ.get('BORDEAUX_CAPTURE_CLOSE') == '1'
       else '/Users/vincent/Work/RealGazebo/bordeaux-unreal-current.png')

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('could not load Bordeaux map')
actors = unreal.EditorLevelLibrary.get_all_level_actors()
surroundings = next(a for a in actors if a.get_actor_label() == 'BordeauxCUASSurroundings')
airport = next(a for a in actors if a.get_actor_label() == 'BordeauxNativeAirportDistrict')

# Remove the project's debug skydome from this transient capture only. It is
# the source of the repeated "sky material does not cover" frame.
for actor in actors:
    if actor.get_actor_label() == 'SM_SkySphere':
        actor.set_is_temporarily_hidden_in_editor(True)
        actor.set_actor_hidden_in_game(True)

# Use the corrected tiled scene, not the hidden legacy actors.
tiled = [a for a in actors if a.get_actor_label().startswith('BordeauxTiled_')]
if os.environ.get('BORDEAUX_CAPTURE_CLOSE') == '1':
    tiled = [a for a in tiled if any(key in a.get_actor_label() for key in ('Tile_-01_000', 'Tile_000_000'))]
if tiled:
    mins = [float('inf')] * 3
    maxs = [float('-inf')] * 3
    for actor in tiled:
        origin, extent = actor.get_actor_bounds(False)
        values = ((origin.x, extent.x), (origin.y, extent.y), (origin.z, extent.z))
        for i in range(3):
            mins[i] = min(mins[i], values[i][0] - values[i][1])
            maxs[i] = max(maxs[i], values[i][0] + values[i][1])
    center = unreal.Vector(*(0.5 * (mins[i] + maxs[i]) for i in range(3)))
    extent = unreal.Vector(*(0.5 * (maxs[i] - mins[i]) for i in range(3)))
else:
    center, extent = airport.get_actor_bounds(False)
    if extent.x < 1 or extent.y < 1:
        center, extent = surroundings.get_actor_bounds(False)
height = max(extent.x, extent.y) * 1.25
location = unreal.Vector(center.x, center.y, center.z + height)
rotation = unreal.MathLibrary.find_look_at_rotation(location, center)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor.set_level_viewport_camera_info(location, rotation)
unreal.log(f'Bordeaux capture camera: {center} extent {extent} location {location}')

world = unreal.EditorLevelLibrary.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1920x1080')
time.sleep(3.0)
candidates = glob.glob('/Users/vincent/Work/RealGazebo-ROS2/src/realgazebo-ue5/Saved/Screenshots/MacEditor/*.png')
if not candidates:
    raise RuntimeError('Unreal did not produce a screenshot')
newest = max(candidates, key=os.path.getctime)
shutil.copy(newest, OUT)
unreal.log(f'Bordeaux Unreal screenshot: {newest} -> {OUT}')
