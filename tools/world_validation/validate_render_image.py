#!/usr/bin/env python3
"""Quantitative sanity/regression checks for deterministic UE world renders."""
from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


@dataclass(frozen=True)
class ImageMetrics:
    width: int
    height: int
    mean_luma: float
    stdev_luma: float
    min_luma: int
    max_luma: int
    dynamic_range: int
    dark_fraction: float
    bright_fraction: float
    near_gray_fraction: float
    edge_energy: float


def _edge_energy(gray: Image.Image) -> float:
    if gray.width < 2 or gray.height < 2:
        return 0.0
    shifted_x = gray.transform(gray.size, Image.Transform.AFFINE, (1, 0, 1, 0, 1, 0))
    shifted_y = gray.transform(gray.size, Image.Transform.AFFINE, (1, 0, 0, 0, 1, 1))
    horizontal = ImageChops.difference(gray, shifted_x)
    vertical = ImageChops.difference(gray, shifted_y)
    h = ImageStat.Stat(horizontal).mean[0]
    v = ImageStat.Stat(vertical).mean[0]
    return float((h + v) * 0.5)


def compute_metrics(path: Path) -> ImageMetrics:
    with Image.open(path) as source:
        rgb = source.convert("RGB")
        gray = rgb.convert("L")
        values = list(gray.getdata())
        total = max(len(values), 1)
        stat = ImageStat.Stat(gray)
        extrema = gray.getextrema()
        near_gray = sum(
            max(red, green, blue) - min(red, green, blue) <= 3
            for red, green, blue in rgb.getdata()
        )
        return ImageMetrics(
            width=rgb.width,
            height=rgb.height,
            mean_luma=float(stat.mean[0]),
            stdev_luma=float(stat.stddev[0]),
            min_luma=int(extrema[0]),
            max_luma=int(extrema[1]),
            dynamic_range=int(extrema[1] - extrema[0]),
            dark_fraction=sum(value <= 5 for value in values) / total,
            bright_fraction=sum(value >= 250 for value in values) / total,
            near_gray_fraction=near_gray / total,
            edge_energy=_edge_energy(gray),
        )


def normalized_rms_difference(current: Path, baseline: Path) -> float:
    with Image.open(current) as current_image, Image.open(baseline) as baseline_image:
        a = current_image.convert("RGB")
        b = baseline_image.convert("RGB")
        if a.size != b.size:
            return math.inf
        stat = ImageStat.Stat(ImageChops.difference(a, b))
        return float(sum(stat.rms) / (len(stat.rms) * 255.0))


def validate_metrics(
    metrics: ImageMetrics,
    *,
    expected_width: int = 1920,
    expected_height: int = 1080,
    max_dark_fraction: float = 0.95,
    max_bright_fraction: float = 0.95,
    min_stdev_luma: float = 5.0,
    min_dynamic_range: int = 30,
    min_edge_energy: float = 0.5,
    max_near_gray_fraction: float = 0.98,
) -> list[str]:
    errors: list[str] = []
    if (metrics.width, metrics.height) != (expected_width, expected_height):
        errors.append(
            f"unexpected resolution: {metrics.width}x{metrics.height}, "
            f"expected {expected_width}x{expected_height}"
        )
    if metrics.dark_fraction > max_dark_fraction:
        errors.append(f"render is predominantly black: {metrics.dark_fraction:.3f}")
    if metrics.bright_fraction > max_bright_fraction:
        errors.append(f"render is predominantly white/clipped: {metrics.bright_fraction:.3f}")
    if metrics.stdev_luma < min_stdev_luma:
        errors.append(f"render luminance variance too low: {metrics.stdev_luma:.3f}")
    if metrics.dynamic_range < min_dynamic_range:
        errors.append(f"render dynamic range too low: {metrics.dynamic_range}")
    if metrics.edge_energy < min_edge_energy:
        errors.append(f"render detail/edge energy too low: {metrics.edge_energy:.3f}")
    if metrics.near_gray_fraction > max_near_gray_fraction:
        errors.append(f"render is suspiciously grayscale/flat: {metrics.near_gray_fraction:.3f}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--max-baseline-rms", type=float, default=0.20)
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()

    metrics = compute_metrics(args.image)
    errors = validate_metrics(metrics)
    result: dict[str, object] = {"metrics": asdict(metrics), "errors": errors}

    if args.baseline:
        difference = normalized_rms_difference(args.image, args.baseline)
        result["baseline_normalized_rms"] = difference
        if not math.isfinite(difference):
            errors.append("baseline dimensions do not match")
        elif difference > args.max_baseline_rms:
            errors.append(
                f"render differs too much from baseline: {difference:.4f} > "
                f"{args.max_baseline_rms:.4f}"
            )

    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(json.dumps(result, indent=2, sort_keys=True), encoding="utf-8")

    print(json.dumps(result, indent=2, sort_keys=True))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
