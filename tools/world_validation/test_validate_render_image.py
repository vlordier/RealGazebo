#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

from validate_render_image import (
    compute_metrics,
    normalized_rms_difference,
    validate_metrics,
)


class RenderImageValidationTests(unittest.TestCase):
    def _path(self, directory: str, name: str) -> Path:
        return Path(directory) / name

    def test_detailed_color_render_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = self._path(temp_dir, "good.png")
            image = Image.new("RGB", (1920, 1080), (40, 90, 150))
            draw = ImageDraw.Draw(image)
            for x in range(0, 1920, 64):
                draw.rectangle((x, 0, x + 31, 1079), fill=(180, 120, (x // 8) % 255))
            for y in range(0, 1080, 80):
                draw.line((0, y, 1919, y), fill=(20, 220, 80), width=6)
            image.save(path)
            self.assertEqual(validate_metrics(compute_metrics(path)), [])

    def test_black_frame_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = self._path(temp_dir, "black.png")
            Image.new("RGB", (1920, 1080), (0, 0, 0)).save(path)
            errors = validate_metrics(compute_metrics(path))
            self.assertTrue(any("predominantly black" in error for error in errors))
            self.assertTrue(any("variance too low" in error for error in errors))

    def test_flat_gray_frame_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = self._path(temp_dir, "gray.png")
            Image.new("RGB", (1920, 1080), (128, 128, 128)).save(path)
            errors = validate_metrics(compute_metrics(path))
            self.assertTrue(any("variance too low" in error for error in errors))
            self.assertTrue(any("grayscale/flat" in error for error in errors))

    def test_wrong_resolution_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = self._path(temp_dir, "small.png")
            Image.new("RGB", (640, 480), (20, 100, 200)).save(path)
            errors = validate_metrics(compute_metrics(path))
            self.assertTrue(any("unexpected resolution" in error for error in errors))

    def test_identical_baseline_has_zero_difference(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            a = self._path(temp_dir, "a.png")
            b = self._path(temp_dir, "b.png")
            Image.new("RGB", (64, 64), (10, 30, 200)).save(a)
            Image.new("RGB", (64, 64), (10, 30, 200)).save(b)
            self.assertEqual(normalized_rms_difference(a, b), 0.0)

    def test_large_baseline_change_is_detectable(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            a = self._path(temp_dir, "a.png")
            b = self._path(temp_dir, "b.png")
            Image.new("RGB", (64, 64), (0, 0, 0)).save(a)
            Image.new("RGB", (64, 64), (255, 255, 255)).save(b)
            self.assertGreater(normalized_rms_difference(a, b), 0.95)

    def test_baseline_dimension_mismatch_returns_infinite_difference(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            a = self._path(temp_dir, "a.png")
            b = self._path(temp_dir, "b.png")
            Image.new("RGB", (64, 64), (1, 2, 3)).save(a)
            Image.new("RGB", (32, 32), (1, 2, 3)).save(b)
            self.assertEqual(normalized_rms_difference(a, b), float("inf"))


if __name__ == "__main__":
    unittest.main()
