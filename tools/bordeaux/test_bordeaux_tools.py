#!/usr/bin/env python3
"""Tests for Bordeaux OBJ preprocessing tools."""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TEST_OBJ = os.path.join(HERE, "test_data", "test.obj")


class TestInspectObj(unittest.TestCase):
    """Test the streaming OBJ inspector."""

    def setUp(self):
        sys.path.insert(0, HERE)
        from inspect_obj import inspect_obj
        self.inspect = inspect_obj

    def test_inspect_returns_dict(self):
        result = self.inspect(TEST_OBJ)
        self.assertIsInstance(result, dict)

    def test_inspect_counts(self):
        result = self.inspect(TEST_OBJ)
        self.assertEqual(result["vertices"], 36)
        self.assertEqual(result["faces"], 13)
        self.assertEqual(result["objects"], 8)

    def test_inspect_bbox(self):
        result = self.inspect(TEST_OBJ)
        bbox = result["bbox"]
        self.assertAlmostEqual(bbox["min"][0], -2000.0)
        self.assertAlmostEqual(bbox["max"][0], 2000.0)
        self.assertAlmostEqual(bbox["min"][2], -2000.0)
        self.assertAlmostEqual(bbox["max"][2], 1500.0)

    def test_inspect_categories(self):
        result = self.inspect(TEST_OBJ)
        cats = result["categories"]
        self.assertIn("aeroway", cats)
        self.assertIn("building", cats)
        self.assertIn("highway", cats)
        self.assertIn("Water", cats)
        self.assertIn("natural", cats)

    def test_inspect_semantic_prefixes(self):
        result = self.inspect(TEST_OBJ)
        prefixes = result["semantic_prefixes"]
        self.assertIn("aeroway", prefixes)
        self.assertIn("building", prefixes)
        self.assertIn("highway", prefixes)
        self.assertIn("Water", prefixes)
        self.assertIn("natural", prefixes)

    def test_inspect_materials(self):
        result = self.inspect(TEST_OBJ)
        # Test OBJ has no usemtl lines, so materials should be empty
        self.assertEqual(len(result["materials"]), 0)

    def test_inspect_largest_objects(self):
        result = self.inspect(TEST_OBJ)
        largest = result["object_stats"]["largest_objects"]
        self.assertTrue(len(largest) > 0)
        # The building has 6 faces, should be largest
        self.assertEqual(largest[0]["name"], "building=yes_1")
        self.assertEqual(largest[0]["faces"], 6)


class TestAnalyzeRunways(unittest.TestCase):
    """Test the runway analyzer."""

    def setUp(self):
        sys.path.insert(0, HERE)
        from analyze_runways import (
            build_connected_components,
            compute_bbox,
            compute_pca_oriented_bbox,
            parse_obj_vertices_and_faces,
        )
        self.parse = parse_obj_vertices_and_faces
        self.build_components = build_connected_components
        self.bbox = compute_bbox
        self.pca = compute_pca_oriented_bbox

    def test_parse_aeroway_faces(self):
        _vertices, face_groups = self.parse(TEST_OBJ, "aeroway")
        self.assertIn("aeroway=ru_1", face_groups)
        self.assertEqual(len(face_groups["aeroway=ru_1"]), 1)

    def test_parse_vertex_count(self):
        vertices, _ = self.parse(TEST_OBJ, "aeroway")
        self.assertEqual(len(vertices), 36)

    def test_build_components(self):
        vertices, face_groups = self.parse(TEST_OBJ, "aeroway")
        components = self.build_components(vertices, face_groups)
        self.assertTrue(len(components) >= 1)

    def test_compute_bbox(self):
        vertices, face_groups = self.parse(TEST_OBJ, "aeroway")
        components = self.build_components(vertices, face_groups)
        if components:
            bbox = self.bbox(vertices, components[0]["vertex_indices"])
            self.assertIn("min", bbox)
            self.assertIn("max", bbox)
            self.assertIn("extent", bbox)

    def test_compute_pca(self):
        vertices, face_groups = self.parse(TEST_OBJ, "aeroway")
        components = self.build_components(vertices, face_groups)
        if components:
            pca = self.pca(vertices, components[0]["vertex_indices"])
            self.assertIsNotNone(pca)
            self.assertIn("length", pca)
            self.assertIn("width", pca)
            self.assertIn("aspect_ratio", pca)


class TestPreprocessBordeaux(unittest.TestCase):
    """Test the preprocessing pipeline."""

    def setUp(self):
        sys.path.insert(0, HERE)
        from preprocess_bordeaux import (
            ORIGIN_OBJ,
            SCALE_UE_CM,
            TILE_SIZE,
            get_semantic_category,
            get_tile_key,
            get_ue_material,
            obj_to_ue,
            should_keep,
        )
        self.get_cat = get_semantic_category
        self.should_keep = should_keep
        self.obj_to_ue = obj_to_ue
        self.get_tile = get_tile_key
        self.get_mat = get_ue_material
        self.scale = SCALE_UE_CM
        self.origin = ORIGIN_OBJ
        self.tile_size = TILE_SIZE

    def test_scale_constant(self):
        self.assertAlmostEqual(self.scale, 14.3, delta=0.1)

    def test_tile_size_constant(self):
        self.assertEqual(self.tile_size, 10000.0)

    def test_get_semantic_category(self):
        self.assertEqual(self.get_cat("aeroway=ru_1"), "aeroway")
        self.assertEqual(self.get_cat("building=yes_1"), "building")
        self.assertEqual(self.get_cat("highway=primary_1"), "highway")
        self.assertEqual(self.get_cat("Water_1"), "Water")
        self.assertEqual(self.get_cat("natural_1"), "natural")

    def test_should_keep_aeroway(self):
        self.assertTrue(self.should_keep("aeroway=ru_1"))

    def test_should_keep_building(self):
        self.assertTrue(self.should_keep("building=yes_1"))

    def test_should_keep_highway_primary(self):
        self.assertTrue(self.should_keep("highway=primary_1"))

    def test_should_discard_footway(self):
        self.assertFalse(self.should_keep("highway=footway_1"))

    def test_should_discard_route_bus(self):
        self.assertFalse(self.should_keep("route_bus_1"))

    def test_should_keep_water(self):
        self.assertTrue(self.should_keep("Water_1"))

    def test_should_keep_natural(self):
        self.assertTrue(self.should_keep("natural_1"))

    def test_obj_to_ue_transform(self):
        # Origin at (-12000, 0, 11000)
        # OBJ (0, 0, 0) -> UE (12000*14.3, -(-11000)*14.3, 0*14.3)
        ue = self.obj_to_ue(0.0, 0.0, 0.0)
        self.assertAlmostEqual(ue[0], 12000.0 * self.scale, delta=1.0)
        self.assertAlmostEqual(ue[1], 11000.0 * self.scale, delta=1.0)
        self.assertAlmostEqual(ue[2], 0.0, delta=1.0)

    def test_obj_to_ue_at_origin(self):
        # At the origin point, UE should be (0, 0, 0)
        ue = self.obj_to_ue(self.origin[0], self.origin[1], self.origin[2])
        self.assertAlmostEqual(ue[0], 0.0, delta=1.0)
        self.assertAlmostEqual(ue[1], 0.0, delta=1.0)
        self.assertAlmostEqual(ue[2], 0.0, delta=1.0)

    def test_get_tile_key(self):
        # At origin, tile should be (0, 0)
        ue_origin = self.obj_to_ue(self.origin[0], self.origin[1], self.origin[2])
        tile = self.get_tile(ue_origin[0], ue_origin[1])
        self.assertEqual(tile, (0, 0))

    def test_get_ue_material_aeroway(self):
        mat = self.get_mat("aeroway=ru_1", "aeroway")
        self.assertIn("M_Aeroway_Asphalt", mat)

    def test_get_ue_material_building(self):
        mat = self.get_mat("building=yes_1", "building")
        self.assertIn("M_Building_Generic", mat)

    def test_get_ue_material_water(self):
        mat = self.get_mat("Water_1", "Water")
        self.assertIn("M_Water", mat)

    def test_get_ue_material_default(self):
        mat = self.get_mat("unknown_category_1", None)
        self.assertIn("M_Ground", mat)


class TestAnalyzeScale(unittest.TestCase):
    """Test the scale analysis tool."""

    def setUp(self):
        sys.path.insert(0, HERE)
        from analyze_scale import analyze_scale
        self.analyze = analyze_scale

    def test_analyze_returns_dict(self):
        result = self.analyze(TEST_OBJ)
        self.assertIsInstance(result, dict)

    def test_analyze_has_runways(self):
        result = self.analyze(TEST_OBJ)
        self.assertIn("runway_ne", result)
        self.assertIn("runway_sw", result)

    def test_analyze_has_face_widths(self):
        result = self.analyze(TEST_OBJ)
        # Test OBJ has no top surface faces (Y=0.5), so face_width_analysis may be absent
        # But the function should still return without error
        self.assertIn("total_top_faces", result)


if __name__ == "__main__":
    unittest.main()