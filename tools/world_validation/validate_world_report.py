#!/usr/bin/env python3
"""Validate a JSON report emitted by audit_world.py without requiring Unreal."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

REQUIRED_CATEGORIES = (
    "directional_lights",
    "sky_lights",
    "sky_atmospheres",
    "post_process_volumes",
)


def validate_report(report: dict[str, Any], *, require_screenshot: bool = False) -> list[str]:
    errors: list[str] = []
    world = report.get("world")
    if not isinstance(world, dict):
        return ["missing world object"]

    if int(world.get("actor_count", 0)) <= 0:
        errors.append("world contains no actors")
    if int(world.get("component_count", 0)) <= 0:
        errors.append("world contains no components")

    categories = world.get("categories")
    if not isinstance(categories, dict):
        errors.append("missing categories object")
        categories = {}

    for key in REQUIRED_CATEGORIES:
        items = categories.get(key, [])
        if not isinstance(items, list) or not items:
            errors.append(f"missing required world category: {key}")

    ppvs = categories.get("post_process_volumes", [])
    if isinstance(ppvs, list) and ppvs and not any(
        bool(item.get("unbound")) for item in ppvs if isinstance(item, dict)
    ):
        errors.append("no unbound post-process volume")

    missing_assets = world.get("missing_assets", [])
    if missing_assets:
        errors.append(f"missing referenced assets: {len(missing_assets)}")

    null_meshes = world.get("null_mesh_components", [])
    if null_meshes:
        errors.append(f"static-mesh components without mesh: {len(null_meshes)}")

    null_materials = world.get("null_material_slots", [])
    if null_materials:
        errors.append(f"material slots without material: {len(null_materials)}")

    screenshot = report.get("screenshot", {})
    if require_screenshot:
        if not isinstance(screenshot, dict) or not screenshot.get("requested"):
            errors.append("screenshot was not requested")
        elif screenshot.get("error"):
            errors.append(f"screenshot request failed: {screenshot['error']}")
        else:
            path = screenshot.get("path")
            if not path or not Path(path).is_file():
                errors.append("screenshot file does not exist")
            elif Path(path).stat().st_size < 1024:
                errors.append("screenshot file is unexpectedly small")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--require-screenshot", action="store_true")
    args = parser.parse_args()

    report = json.loads(args.report.read_text(encoding="utf-8"))
    errors = validate_report(report, require_screenshot=args.require_screenshot)
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("PASS: world audit satisfies required render-world contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
