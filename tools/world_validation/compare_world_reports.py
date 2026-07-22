#!/usr/bin/env python3
"""Compare two Unreal world-audit reports and flag structural regressions."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


CATEGORY_KEYS = (
    "directional_lights",
    "sky_lights",
    "sky_atmospheres",
    "volumetric_clouds",
    "height_fogs",
    "post_process_volumes",
    "landscapes",
    "cameras",
    "static_mesh_actors",
)


def _world(report: dict[str, Any]) -> dict[str, Any]:
    world = report.get("world")
    if not isinstance(world, dict):
        raise ValueError("report missing world object")
    return world


def _set(world: dict[str, Any], key: str) -> set[str]:
    value = world.get(key, [])
    return {str(item) for item in value} if isinstance(value, list) else set()


def compare_reports(
    baseline: dict[str, Any],
    current: dict[str, Any],
    *,
    max_actor_loss_fraction: float = 0.10,
    max_component_loss_fraction: float = 0.10,
    max_asset_loss_fraction: float = 0.05,
) -> list[str]:
    errors: list[str] = []
    base = _world(baseline)
    now = _world(current)

    for key, threshold in (
        ("actor_count", max_actor_loss_fraction),
        ("component_count", max_component_loss_fraction),
    ):
        base_value = int(base.get(key, 0))
        current_value = int(now.get(key, 0))
        if base_value > 0 and current_value < base_value * (1.0 - threshold):
            errors.append(
                f"{key} regressed: baseline={base_value}, current={current_value}, "
                f"allowed_loss={threshold:.1%}"
            )

    base_categories = base.get("categories", {})
    now_categories = now.get("categories", {})
    if isinstance(base_categories, dict) and isinstance(now_categories, dict):
        for key in CATEGORY_KEYS:
            base_items = base_categories.get(key, [])
            now_items = now_categories.get(key, [])
            base_count = len(base_items) if isinstance(base_items, list) else 0
            now_count = len(now_items) if isinstance(now_items, list) else 0
            if base_count > 0 and now_count < base_count:
                errors.append(
                    f"world category regressed: {key} baseline={base_count}, current={now_count}"
                )

    for key in ("mesh_assets", "material_assets"):
        base_assets = _set(base, key)
        now_assets = _set(now, key)
        if not base_assets:
            continue
        missing = sorted(base_assets - now_assets)
        allowed = int(len(base_assets) * max_asset_loss_fraction)
        if len(missing) > allowed:
            errors.append(
                f"{key} lost {len(missing)}/{len(base_assets)} baseline assets "
                f"(allowed {allowed}): {', '.join(missing[:8])}"
            )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--max-actor-loss", type=float, default=0.10)
    parser.add_argument("--max-component-loss", type=float, default=0.10)
    parser.add_argument("--max-asset-loss", type=float, default=0.05)
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    current = json.loads(args.current.read_text(encoding="utf-8"))
    errors = compare_reports(
        baseline,
        current,
        max_actor_loss_fraction=args.max_actor_loss,
        max_component_loss_fraction=args.max_component_loss,
        max_asset_loss_fraction=args.max_asset_loss,
    )
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("PASS: world structure remains within baseline regression thresholds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
