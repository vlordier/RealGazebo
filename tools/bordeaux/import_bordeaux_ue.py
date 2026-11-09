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
MESH_BASE_PATH = "/RealGazebo/Bordeaux/Geometry"


def asset_path(asset):
    """Return an asset path without the object-name suffix."""
    return asset.get_path_name().split(".")[0] if asset else ""


def remove_previous_bordeaux_actors():
    """Remove only actors backed by a Bordeaux tile before rebuilding the map."""
    removed = 0
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label().startswith("Tile_"):
            unreal.EditorLevelLibrary.destroy_actor(actor)
            removed += 1
            continue
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if not component:
            continue
        mesh = component.get_editor_property("static_mesh")
        if asset_path(mesh).startswith(MESH_BASE_PATH + "/Tile_"):
            unreal.EditorLevelLibrary.destroy_actor(actor)
            removed += 1
    print(f"Removed {removed} previous Bordeaux tile actors")


def remove_occluding_ground_plane():
    """Remove the legacy coplanar Plane actor that z-fights the imported ground."""
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label() == "Plane":
            unreal.EditorLevelLibrary.destroy_actor(actor)
            print("Removed legacy coplanar Plane actor")
            return


def delete_previous_tile_assets(manifest):
    """Delete exactly the tile assets about to be re-imported.

    Re-importing Interchange OBJ assets in place accumulates stale material slots.
    Recreating the known tile set produces a clean, deterministic mesh layout.
    """
    deleted = 0
    for tile_info in manifest["tiles"]:
        mesh_path = f"{MESH_BASE_PATH}/{tile_info['name']}"
        if unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
            if not unreal.EditorAssetLibrary.delete_asset(mesh_path):
                raise RuntimeError(f"could not delete stale tile asset: {mesh_path}")
            deleted += 1
    print(f"Deleted {deleted} stale Bordeaux tile assets")


def apply_materials(mesh, material_paths):
    """Bind the manifest's semantic materials to every imported mesh section."""
    materials = []
    for path in material_paths:
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not material:
            raise RuntimeError(f"missing Bordeaux material: {path}")
        materials.append(material)
    if not materials:
        raise RuntimeError(f"tile has no declared materials: {asset_path(mesh)}")

    static_materials = list(mesh.get_editor_property("static_materials"))
    slot_count = len(static_materials)
    if slot_count <= 0:
        raise RuntimeError(f"tile imported without material slots: {asset_path(mesh)}")
    for index in range(slot_count):
        # The preprocessor writes sections in the same stable material order as
        # manifest['materials'].  Cycling is also safe for importer-created
        # duplicate sections, and ensures no slot remains unbound.
        static_materials[index].set_editor_property(
            "material_interface", materials[index % len(materials)]
        )
    mesh.set_editor_property("static_materials", static_materials)
    unreal.EditorAssetLibrary.save_asset(asset_path(mesh))


def apply_material_overrides(actor, material_paths):
    """Bind materials on the placed component as well as on its mesh asset.

    World validation reads ``UStaticMeshComponent.get_material``.  Imported
    meshes can retain empty component slots even after their source asset has
    been repaired, so set explicit actor overrides for every available slot.
    """
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not component:
        raise RuntimeError(f"tile actor has no StaticMeshComponent: {actor.get_actor_label()}")

    materials = []
    for path in material_paths:
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not material:
            raise RuntimeError(f"missing Bordeaux material: {path}")
        materials.append(material)

    slot_count = component.get_num_materials()
    if slot_count <= 0:
        raise RuntimeError(f"tile actor has no material slots: {actor.get_actor_label()}")
    for index in range(slot_count):
        component.set_material(index, materials[index % len(materials)])


def repair_existing_tiles(manifest_path: str, target_map: str):
    """Rebind every tile material and recreate the complete airport actor set."""
    with open(manifest_path) as f:
        manifest = json.load(f)
    editor_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not editor_subsystem.load_level(target_map):
        raise RuntimeError(f"could not load map: {target_map}")
    remove_occluding_ground_plane()
    remove_previous_bordeaux_actors()
    placed = 0
    for tile_info in manifest["tiles"]:
        mesh_path = f"{MESH_BASE_PATH}/{tile_info['name']}"
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if not mesh:
            raise RuntimeError(f"missing rebuilt tile asset: {mesh_path}")
        apply_materials(mesh, tile_info["materials"])
        actor = unreal.EditorLevelLibrary.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
        if actor:
            actor.set_actor_label(tile_info["name"])
            apply_material_overrides(actor, tile_info["materials"])
            placed += 1
    add_validation_camera(unreal.EditorLevelLibrary.get_editor_world())
    unreal.EditorLevelLibrary.save_current_level()
    print(f"Repaired and placed {placed}/{len(manifest['tiles'])} Bordeaux tiles")


def create_semantic_materials():
    """Create semantic materials in /RealGazebo/Bordeaux/Materials/."""
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    content_path = "/RealGazebo/Bordeaux/Materials"

    material_defs = [
        ("M_Aeroway_Asphalt", "Runway/taxiway asphalt", (0.035, 0.040, 0.045)),
        ("M_Road_Asphalt", "Road surface", (0.080, 0.085, 0.090)),
        ("M_Building_Generic", "Generic building concrete", (0.42, 0.40, 0.36)),
        ("M_Grass", "Grass/vegetation", (0.10, 0.28, 0.08)),
        ("M_Landuse", "General landuse", (0.32, 0.30, 0.18)),
        ("M_Natural", "Natural terrain", (0.18, 0.32, 0.12)),
        ("M_Rail", "Railway", (0.12, 0.12, 0.12)),
        ("M_Water", "Water surface", (0.02, 0.16, 0.32)),
        ("M_Ground", "General ground", (0.28, 0.25, 0.18)),
    ]

    created = []
    for name, desc, color in material_defs:
        asset_path = f"{content_path}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            material = unreal.EditorAssetLibrary.load_asset(asset_path)
        else:
            material = asset_tools.create_asset(name, content_path, unreal.Material,
                                                unreal.MaterialFactoryNew())
        if not material:
            raise RuntimeError(f"failed to create material: {name}")

        # The original placeholder materials had no graph, so every semantic
        # surface rendered as the same flat default.  Keep this intentionally
        # simple and deterministic while retaining a visible airport taxonomy.
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        # The source OBJ has predominantly downward-wound ground faces.
        # Two-sided rendering is required for the aerial/simulation camera to
        # see roads, runways and aprons from above.
        material.set_editor_property("two_sided", True)
        base_color = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, -300, 0
        )
        base_color.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(
            base_color, "", unreal.MaterialProperty.MP_BASE_COLOR
        )
        # GIS surfaces are often perfectly flat and receive little useful
        # lighting in an unattended aerial capture.  A small emissive term
        # preserves the semantic colour and makes the airport inspectable.
        unreal.MaterialEditingLibrary.connect_material_property(
            base_color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        )
        roughness = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant, -300, 120
        )
        roughness.set_editor_property("r", 0.82)
        unreal.MaterialEditingLibrary.connect_material_property(
            roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
        )
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.EditorAssetLibrary.save_asset(asset_path)
        print(f"  Configured material: {asset_path} ({desc})")
        created.append(asset_path)

    return created


def import_tiles(manifest_path: str, tiles_dir: str, target_map: str):
    """Import OBJ tiles as static meshes and place in the world."""
    with open(manifest_path) as f:
        manifest = json.load(f)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # Open target map
    editor_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not editor_subsystem.load_level(target_map):
        print(f"ERROR: Could not load map: {target_map}")
        return

    unreal.EditorLevelLibrary.get_editor_world()
    if os.environ.get("BORDEAUX_RESET", "0") == "1":
        remove_previous_bordeaux_actors()
        delete_previous_tile_assets(manifest)

    start = int(os.environ.get("BORDEAUX_TILE_START", "0"))
    count = int(os.environ.get("BORDEAUX_TILE_COUNT", str(len(manifest["tiles"]))))
    selected_tiles = manifest["tiles"][start:start + count]
    if not selected_tiles:
        raise RuntimeError(f"no Bordeaux tiles selected (start={start}, count={count})")

    imported_meshes = []

    for tile_info in selected_tiles:
        obj_path = os.path.join(tiles_dir, tile_info["file"])
        if not os.path.exists(obj_path):
            print(f"  SKIP: {obj_path} not found")
            continue

        mesh_name = tile_info["name"]
        mesh_path = f"{MESH_BASE_PATH}/{mesh_name}"

        # Use a fresh task/factory per OBJ: Interchange retains factory state
        # across imports, which was the source of the stale material sections.
        import_task = unreal.AssetImportTask()
        import_task.set_editor_property("automated", True)
        import_task.set_editor_property("replace_existing", False)
        import_task.set_editor_property("replace_existing_settings", False)
        import_task.set_editor_property("save", True)
        import_task.set_editor_property("filename", obj_path)
        import_task.set_editor_property("destination_path", MESH_BASE_PATH)
        import_task.set_editor_property("destination_name", mesh_name)
        import_task.set_editor_property("factory", unreal.FbxFactory())

        asset_tools.import_asset_tasks([import_task])

        # Check if import succeeded
        if unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
            imported_meshes.append(mesh_path)
            print(f"  Imported: {mesh_name}")
        else:
            print(f"  FAILED: {mesh_name}")

    print(f"\nImported {len(imported_meshes)}/{len(selected_tiles)} selected meshes")

    # Place meshes in world
    placed_count = 0
    for mesh_path in imported_meshes:
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if not mesh:
            continue
        tile_info = next(item for item in selected_tiles if item["name"] == mesh.get_name())
        apply_materials(mesh, tile_info["materials"])

        # Tile vertices are already transformed to absolute UE coordinates by
        # preprocess_bordeaux.obj_to_ue().  Do not apply tile coordinates again.
        actor = unreal.EditorLevelLibrary.spawn_actor_from_object(
            mesh, unreal.Vector(0.0, 0.0, 0.0))
        if actor:
            actor.set_actor_label(mesh_path.split("/")[-1])
            apply_material_overrides(actor, tile_info["materials"])
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
    actor = None
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    for actor in actors:
        if actor.get_actor_label() == "ValidationCamera":
            print("  Updating ValidationCamera")
            break
    else:
        actor = None

    if not actor:
        camera_class = unreal.load_class(None, "/Script/Engine.CameraActor")
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            camera_class, unreal.Vector(0, 0, 350000))
        if not actor:
            print("  FAILED to create CameraActor")
            return None
        actor.set_actor_label("ValidationCamera")

    # Use named fields: the positional UE Python Rotator constructor is
    # roll/pitch/yaw, so the previous positional call rolled the camera 90°.
    # The clean raw-data pipeline rebases the runway footprint at the origin.
    # A 3.5 km top-down view frames both runways, apron and terminal.
    actor.set_actor_location(unreal.Vector(0, 0, 350000), False, False)
    actor.set_actor_rotation(unreal.Rotator(roll=0, pitch=-90, yaw=0), False)

    camera = actor.get_component_by_class(unreal.load_class(None, "/Script/Engine.CameraComponent"))
    if camera:
        # Wide FOV to capture the airport
        camera.set_editor_property("field_of_view", 90.0)

    print("  Created ValidationCamera at (0, 0, 350000) looking down")
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

    if os.environ.get("BORDEAUX_REPAIR_ONLY", "0") == "1":
        repair_existing_tiles(manifest_path, target_map)
        return

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

                # Set drone Blueprint class using correct UE5.8 API
                drone_bp = unreal.EditorAssetLibrary.load_blueprint_class(
                    "/RealGazebo/Blueprints/Vehicles/RealGazeboStreaming/UAV/BP_X500Pawn_Stream")
                if drone_bp:
                    swarm.set_editor_property("DroneClass", drone_bp)
                    unreal.log("  DroneClass set to BP_X500Pawn_Stream")
                else:
                    unreal.log_warning("  Could not load BP_X500Pawn_Stream Blueprint")
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
