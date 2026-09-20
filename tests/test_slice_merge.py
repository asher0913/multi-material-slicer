from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import cv2
import numpy as np
import yaml

from slice_1080p import (
    SliceMergeError,
    SliceMerger,
    _estimate_time,
    _fit_to_resolution,
    _material_layers,
    _normalize_config,
    _write_image,
)


class ImageNormalizationTests(unittest.TestCase):
    def test_centers_smaller_image_and_preserves_pixels(self) -> None:
        image = np.array([[1, 2], [3, 4]], dtype=np.uint8)
        fitted = _fit_to_resolution(image, width=4, height=4)

        self.assertEqual((4, 4), fitted.shape)
        np.testing.assert_array_equal(image, fitted[1:3, 1:3])
        self.assertEqual(10, int(fitted.sum()))

    def test_center_crops_larger_image(self) -> None:
        image = np.arange(25, dtype=np.uint8).reshape((5, 5))
        fitted = _fit_to_resolution(image, width=3, height=3)
        np.testing.assert_array_equal(image[1:4, 1:4], fitted)


class ConfigurationTests(unittest.TestCase):
    def test_expands_material_list_and_transition_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            material_a = root / "a"
            material_b = root / "b"
            material_a.mkdir()
            material_b.mkdir()

            config = _normalize_config(
                {
                    "materials": [
                        {"img_path": str(material_a)},
                        {"img_path": str(material_b)},
                    ]
                }
            )

            self.assertEqual(2, config["material_num"])
            self.assertEqual(str(material_a.resolve()), config["img_path_0"])
            self.assertTrue(config["m0Tom1"])
            self.assertEqual(2.0, config["m1Tom0_time"])

    def test_rejects_non_positive_material_count(self) -> None:
        with self.assertRaisesRegex(SliceMergeError, "greater than 0"):
            _normalize_config({"material_num": 0})

    def test_counts_only_numeric_png_layers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for name in ("1.png", "4.PNG", "preview.png", "3.txt"):
                (root / name).touch()
            self.assertEqual(4, _material_layers({"img_path_0": str(root)}, 0))


class SchedulingTests(unittest.TestCase):
    def test_planner_returns_one_tank_per_layer_and_finite_cost(self) -> None:
        merger = SliceMerger.__new__(SliceMerger)
        merger.config = {"material_num": 2}
        active_pixels = np.array(
            [
                [0, 0],
                [10, 0],
                [8, 6],
                [0, 5],
            ]
        )

        tank_path, changes = merger._plan_tank_path(active_pixels, [2, 3])

        self.assertEqual(4, len(tank_path))
        self.assertTrue(all(tank in (0, 1) for tank in tank_path))
        self.assertGreaterEqual(changes, 0)

    def test_estimated_time_accumulates_wait_and_projection(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            gcode = Path(temp_dir) / "run.gcode"
            gcode.write_text("wait 2.5\nproj 1.png 4.0 15\nwait invalid\n", encoding="utf-8")
            self.assertEqual("00:00:06", _estimate_time(gcode))


class EndToEndTests(unittest.TestCase):
    def test_generates_merged_images_and_gcode(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            material_a = root / "material-a"
            material_b = root / "material-b"
            material_a.mkdir()
            material_b.mkdir()

            image_a = np.zeros((8, 10), dtype=np.uint8)
            image_a[1:4, 1:4] = 255
            image_b = np.zeros((8, 10), dtype=np.uint8)
            image_b[4:7, 6:9] = 255
            _write_image(material_a / "1.png", image_a)
            _write_image(material_b / "1.png", image_b)

            config_path = root / "config.yaml"
            config_path.write_text(
                yaml.safe_dump(
                    {
                        "material_num": 2,
                        "img_path_0": str(material_a),
                        "img_path_1": str(material_b),
                        "res_width": 10,
                        "res_height": 8,
                        "bottom_layers": 1,
                    }
                ),
                encoding="utf-8",
            )
            output = root / "output"

            changes, runtime = SliceMerger(config_path, output).run()

            self.assertGreaterEqual(changes, 0)
            self.assertRegex(runtime, r"^\d{2}:\d{2}:\d{2}$")
            self.assertTrue((output / "config.yaml").is_file())
            self.assertTrue((output / "run.gcode").is_file())
            merged = sorted(output.glob("*.png"))
            self.assertTrue(merged)
            decoded = cv2.imread(str(merged[0]), cv2.IMREAD_GRAYSCALE)
            self.assertEqual((8, 10), decoded.shape)


if __name__ == "__main__":
    unittest.main()
