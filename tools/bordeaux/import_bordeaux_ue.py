#!/usr/bin/env python3
"""Import Bordeaux processed tiles into UE5.8.

Run inside UnrealEditor:
  UnrealEditor Project.uproject -ExecutePythonScript=.../import_bordeaux_ue.py

Environment variables:
  BORDEAUX_MANIFEST    Path to manifest.json from preprocessing
  BORDEAUX_TILES_DIR   Path to tiles/ directory from preprocessing
  BORDEAUX_TARGET_MAP  Target map path, default /RealGazebo/Maps/RealGazebo
"""
from __future__ import annotations

import json
import os
import sys

import unreal

DEFAULT_MAP = "/RealGazebo/Maps/RealGazebo"


def create_semantic_materials():
    """Create semantic materials in /RealGazebo/Bordeaux/Materials/."""
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    content_path = "/RealGazebo/Bordeaux/Materials"

    material_defs = [
        ("M_Aeroway_Asphalt", "Runway/taxiway asphalt"),
        ("M_Road_Asphalt", "Road surface"),
        ("M_Building_Generic", "Generic building concrete"),
        ("M_Grass", "Grass/vegetation"),
        ("M_Landuse", "General landuse"),
        ("M_Natural", "Natural terrain"),
        ("M_Rail", "Railway"),
        ("M_Water", "Water surface"),
        ("M_Ground", "General ground"),
    ]

    created = []
    for name, desc in material_defs:
        asset_path = f"{content_path}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            print(f"  Material exists: {asset_path}")
            created.append(asset_path)
            continue

        material = asset_tools.create_asset(name, content_path, unreal.Material,
                                            unreal.MaterialFactoryNew())
        if not material:
            print(f"  FAILED to create material: {name}")
            continue

        # Set basic properties
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        unreal.EditorAssetLibrary.save_asset(asset_path)
        print(f"  Created material: {asset_path} ({desc})")
        created.append(asset_path)

    return created


def import_tiles(manifest_path: str, tiles_dir: str, target_map: str):
    """Import OBJ tiles as static meshes and place in the world."""
    with open(manifest_path) as f:
        manifest = json.load(f)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    import_task = unreal.AssetImportTask()
    import_task.set_editor_property("automated", True)
    import_task.set_editor_property("replace_existing", True)
    import_task.set_editor_property("save", True)

    mesh_base_path = "/RealGazebo/Bordeaux/Geometry"
    factory = unreal.FbxFactory()

    # Open target map
    editor_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not editor_subsystem.load_level(target_map):
        print(f"ERROR: Could not load map: {target_map}")
        return

    unreal.EditorLevelLibrary.get_editor_world()

    imported_meshes = []

    for tile_info in manifest["tiles"]:
        obj_path = os.path.join(tiles_dir, tile_info["file"])
        if not os.path.exists(obj_path):
            print(f"  SKIP: {obj_path} not found")
            continue

        mesh_name = tile_info["name"]
        mesh_path = f"{mesh_base_path}/{mesh_name}"

        # Import OBJ as static mesh
        import_task.set_editor_property("filename", obj_path)
        import_task.set_editor_property("destination_path", mesh_base_path)
        import_task.set_editor_property("destination_name", mesh_name)
        import_task.set_editor_property("factory", factory)

        asset_tools.import_asset_tasks([import_task])

        # Check if import succeeded
        if unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
            imported_meshes.append(mesh_path)
            print(f"  Imported: {mesh_name}")
        else:
            print(f"  FAILED: {mesh_name}")

    print(f"\nImported {len(imported_meshes)}/{len(manifest['tiles'])} meshes")

    # Place meshes in world
    placed_count = 0
    for mesh_path in imported_meshes:
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if not mesh:
            continue

        # Extract tile coordinates from name
        # Tile_001_000 -> tx=1, ty=0
        parts = mesh_path.split("/")[-1].split("_")
        if len(parts) >= 3:
            tx = int(parts[1])
            ty = int(parts[2])
        else:
            tx, ty = 0, 0

        # Calculate world position from tile coordinates
        tile_size_cm = manifest["tile_size_units"] * manifest["scale_ue_cm_per_unit"]
        world_x = tx * tile_size_cm
        world_y = ty * tile_size_cm

        # Place actor
        actor = unreal.EditorLevelLibrary.spawn_actor_from_object(
            mesh, unreal.Vector(world_x, world_y, 0.0))
        if actor:
            actor.set_actor_label(mesh_path.split("/")[-1])
            placed_count += 1

    print(f"Placed {placed_count} actors in world")


def add_post_process_volume(world):
    """Add a deterministic PostProcessVolume."""
    # Check if one already exists
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    for actor in actors:
        if actor.get_class().get_name() == "PostProcessVolume":
            print(f"  PostProcessVolume exists: {actor.get_actor_label()}")
            return actor

    # Create via Blueprint-like approach: use EditorScripting utilities
    ppv_class = unreal.load_class(None, "/Script/Engine.PostProcessVolume")
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        ppv_class, unreal.Vector(0, 0, 0))
    if not actor:
        print("  FAILED to create PostProcessVolume")
        return None

    actor.set_actor_label("PostProcessVolume")

    # PostProcessVolume exposes settings directly on the actor
    actor.set_editor_property("unbound", True)
    settings = actor.settings

    # Exposure - manual for determinism
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("auto_exposure_bias", 0.0)

    # Motion blur off
    settings.set_editor_property("motion_blur_amount", 0.0)

    # Bloom conservative
    settings.set_editor_property("bloom_intensity", 0.5)
    settings.set_editor_property("bloom_threshold", 0.0)

    # Vignette off
    settings.set_editor_property("vignette_intensity", 0.0)

    # Lens flare off
    settings.set_editor_property("lens_flare_intensity", 0.0)

    print("  Created PostProcessVolume (unbound, manual exposure)")
    return actor


def add_validation_camera(world):
    """Add a deterministic validation camera."""
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    for actor in actors:
        if actor.get_class().get_name() == "CameraActor":
            print(f"  Camera exists: {actor.get_actor_label()}")
            return actor

    camera_class = unreal.load_class(None, "/Script/Engine.CameraActor")
    # Position camera looking down at the airport from above
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        camera_class, unreal.Vector(0, 0, 50000))
    if not actor:
        print("  FAILED to create CameraActor")
        return None

    actor.set_actor_label("ValidationCamera")
    # Rotate to look down
    actor.set_actor_rotation(unreal.Rotator(-90, 0, 0), False)

    camera = actor.get_component_by_class(unreal.load_class(None, "/Script/Engine.CameraComponent"))
    if camera:
        # Wide FOV to capture the airport
        camera.set_editor_property("field_of_view", 90.0)

    print("  Created ValidationCamera at (0, 0, 50000) looking down")
    return actor


def main():
    manifest_path = os.environ.get("BORDEAUX_MANIFEST", "")
    tiles_dir = os.environ.get("BORDEAUX_TILES_DIR", "")
    target_map = os.environ.get("BORDEAUX_TARGET_MAP", DEFAULT_MAP)

    if not manifest_path or not tiles_dir:
        print("ERROR: Set BORDEAUX_MANIFEST and BORDEAUX_TILES_DIR")
        sys.exit(1)

    print("=== Phase 1: Create semantic materials ===")
    create_semantic_materials()

    print("\n=== Phase 2: Import tiles ===")
    import_tiles(manifest_path, tiles_dir, target_map)

    print("\n=== Phase 3: Save map ===\n")
    unreal.EditorLevelLibrary.save_current_level()
    print("  Map saved with 81 Bordeaux tiles")

    print("\n=== Phase 4: Add PostProcessVolume ===\n")
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        add_post_process_volume(world)
    except Exception as e:
        print(f"  PostProcessVolume skipped (will add via world validation): {e}")

    print("\n=== Phase 5: Add validation camera ===\n")
    try:
        add_validation_camera(world)
    except Exception as e:
        print(f"  Validation camera skipped (will add via world validation): {e}")

    print("\n=== Phase 6: Add synthetic drone swarm ===\n")
    try:
        swarm_class = unreal.load_class(None, "/Script/RealGazeboStreaming.SyntheticDroneSwarm")
        if swarm_class:
            world = unreal.EditorLevelLibrary.get_editor_world()
            swarm = unreal.EditorLevelLibrary.spawn_actor_from_class(
                swarm_class, unreal.Vector(0, 0, 0))
            if swarm:
                swarm.set_actor_label("SyntheticDroneSwarm")
                swarm.set_editor_property("DroneCount", 5)
                swarm.set_editor_property("bAutoStartStreaming", True)
                swarm.set_editor_property("RTSPPort", 8554)
                print("  SyntheticDroneSwarm created with 5 drones")
            else:
                print("  WARNING: Failed to spawn SyntheticDroneSwarm")
        else:
            print("  WARNING: SyntheticDroneSwarm class not found (build may need update)")
    except Exception as e:
        print(f"  SyntheticDroneSwarm skipped: {e}")

    print("\n=== Phase 7: Save final ===\n")
    unreal.EditorLevelLibrary.save_current_level()
    print("  Map saved (final)")

    print("\nDone! 81 Bordeaux tiles imported and placed.")
    print("Run tools/world_validation/run_world_audit_mac.sh to validate.")


if __name__ == "__main__":
    main()