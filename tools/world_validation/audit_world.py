#!/usr/bin/env python3
"""Audit a RealGazebo UE5.8 map using Unreal's editor Python APIs.

Run inside UnrealEditor, for example:
  UnrealEditor Project.uproject -ExecutePythonScript=.../audit_world.py

Environment variables:
  REALGAZEBO_WORLD_MAP       Asset path, default /RealGazebo/Maps/RealGazebo
  REALGAZEBO_WORLD_REPORT    JSON output path (defaults under Saved/WorldValidation)

Render capture is intentionally separate because Unreal screenshot tasks are latent and
require editor ticks. See Content/Python/world_validation/test_world_render.py.
"""
from __future__ import annotations

import json
import os
from collections import Counter
from pathlib import Path
from typing import Any

import unreal

DEFAULT_MAP = "/RealGazebo/Maps/RealGazebo"


def safe_prop(obj: Any, name: str, default: Any = None) -> Any:
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def object_path(obj: Any) -> str | None:
    if obj is None:
        return None
    try:
        return obj.get_path_name()
    except Exception:
        return str(obj)


def actor_label(actor: Any) -> str:
    try:
        return actor.get_actor_label()
    except Exception:
        return actor.get_name()


def class_name(obj: Any) -> str:
    try:
        return obj.get_class().get_name()
    except Exception:
        return type(obj).__name__


def load_map(asset_path: str) -> None:
    loaded = unreal.EditorLoadingAndSavingUtils.load_map(asset_path)
    if not loaded:
        raise RuntimeError(f"failed to load map: {asset_path}")


def inventory_world() -> dict[str, Any]:
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(actor_subsystem.get_all_level_actors())
    components = list(actor_subsystem.get_all_level_actors_components())

    actor_classes = Counter(class_name(a) for a in actors)
    component_classes = Counter(class_name(c) for c in components)

    categories: dict[str, list[dict[str, Any]]] = {
        "directional_lights": [],
        "sky_lights": [],
        "sky_atmospheres": [],
        "volumetric_clouds": [],
        "height_fogs": [],
        "post_process_volumes": [],
        "landscapes": [],
        "cameras": [],
        "static_mesh_actors": [],
    }
    category_by_class = {
        "DirectionalLight": "directional_lights",
        "SkyLight": "sky_lights",
        "SkyAtmosphere": "sky_atmospheres",
        "VolumetricCloud": "volumetric_clouds",
        "ExponentialHeightFog": "height_fogs",
        "PostProcessVolume": "post_process_volumes",
        "Landscape": "landscapes",
        "LandscapeStreamingProxy": "landscapes",
        "CameraActor": "cameras",
        "CineCameraActor": "cameras",
        "StaticMeshActor": "static_mesh_actors",
    }

    for actor in actors:
        cname = class_name(actor)
        category = category_by_class.get(cname)
        if not category:
            continue
        location = actor.get_actor_location()
        rotation = actor.get_actor_rotation()
        item = {
            "label": actor_label(actor),
            "class": cname,
            "path": object_path(actor),
            "location_cm": [location.x, location.y, location.z],
            "rotation_deg": [rotation.roll, rotation.pitch, rotation.yaw],
        }
        if cname == "PostProcessVolume":
            item["unbound"] = bool(safe_prop(actor, "unbound", False))
            item["blend_weight"] = float(safe_prop(actor, "blend_weight", 0.0) or 0.0)
        categories[category].append(item)

    mesh_assets: set[str] = set()
    material_assets: set[str] = set()
    null_mesh_components: list[str] = []
    null_material_slots: list[str] = []

    for component in components:
        cname = class_name(component)
        try:
            owner = component.get_owner()
        except Exception:
            owner = None
        owner_name = actor_label(owner) if owner else "<no-owner>"
        if cname not in {
            "StaticMeshComponent",
            "InstancedStaticMeshComponent",
            "HierarchicalInstancedStaticMeshComponent",
        }:
            continue
        mesh = safe_prop(component, "static_mesh")
        if mesh is None:
            null_mesh_components.append(f"{owner_name}:{component.get_name()}")
        else:
            path = object_path(mesh)
            if path:
                mesh_assets.add(path.split(".")[0])
        try:
            count = component.get_num_materials()
        except Exception:
            count = 0
        for index in range(count):
            try:
                material = component.get_material(index)
            except Exception:
                material = None
            if material is None:
                null_material_slots.append(f"{owner_name}:{component.get_name()}[{index}]")
            else:
                path = object_path(material)
                if path:
                    material_assets.add(path.split(".")[0])

    missing_assets = [
        path
        for path in sorted(mesh_assets | material_assets)
        if not unreal.EditorAssetLibrary.does_asset_exist(path)
    ]

    warnings: list[str] = []
    required = {
        "directional_lights": "DirectionalLight",
        "sky_lights": "SkyLight",
        "sky_atmospheres": "SkyAtmosphere",
        "post_process_volumes": "PostProcessVolume",
    }
    for key, human in required.items():
        if not categories[key]:
            warnings.append(f"missing {human}")
    if categories["post_process_volumes"] and not any(
        item.get("unbound") for item in categories["post_process_volumes"]
    ):
        warnings.append(
            "no unbound PostProcessVolume; FPV cameras may render inconsistent exposure/post-process"
        )
    if not categories["landscapes"]:
        warnings.append("no Landscape actor found; world may rely on static meshes or an unloaded partition")
    if null_mesh_components:
        warnings.append(f"{len(null_mesh_components)} static-mesh components have no mesh assigned")
    if null_material_slots:
        warnings.append(f"{len(null_material_slots)} material slots resolve to None")
    if missing_assets:
        warnings.append(f"{len(missing_assets)} referenced mesh/material assets are missing")

    return {
        "actor_count": len(actors),
        "component_count": len(components),
        "actor_classes": dict(sorted(actor_classes.items())),
        "component_classes": dict(sorted(component_classes.items())),
        "categories": categories,
        "mesh_assets": sorted(mesh_assets),
        "material_assets": sorted(material_assets),
        "null_mesh_components": sorted(null_mesh_components),
        "null_material_slots": sorted(null_material_slots),
        "missing_assets": missing_assets,
        "warnings": warnings,
    }


def main() -> None:
    map_path = os.environ.get("REALGAZEBO_WORLD_MAP", DEFAULT_MAP)
    project_saved = Path(unreal.Paths.project_saved_dir())
    report_path = Path(
        os.environ.get(
            "REALGAZEBO_WORLD_REPORT",
            str(project_saved / "WorldValidation" / "world_audit.json"),
        )
    )

    load_map(map_path)
    report = {
        "map": map_path,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "world": inventory_world(),
        "screenshot": {"requested": False, "reason": "render capture runs as a latent automation test"},
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    unreal.log(f"RealGazebo world audit written to {report_path}")
    for warning in report["world"]["warnings"]:
        unreal.log_warning(f"RealGazebo world audit: {warning}")


if __name__ == "__main__":
    main()
