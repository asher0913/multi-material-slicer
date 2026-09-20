#!/usr/bin/env python3
import argparse
import shutil
import sys
import traceback
from pathlib import Path

import cv2
import numpy as np
import yaml


DEFAULT_RES_WIDTH = 1920
DEFAULT_RES_HEIGHT = 1080

DEFAULT_CONFIG = {
    "layer_height": 0.05,
    "back_distance": 5,
    "bottom_layers": 3,
    "max_height": 165,
    "min_height": -50,
    "z_acc_h": 100.0,
    "z_dec_h": -100.0,
    "z_speed_h": 450.0,
    "z_acc_l": 50.0,
    "z_dec_l": -50.0,
    "z_speed_l": 100.0,
    "r_acc": 60.0,
    "r_dec": -60.0,
    "r_speed": 120.0,
    "clean_tank": 3,
    "clean_tank_height": 20,
    "clean_height": 25,
    "clean_height_coefficient": 0.95,
    "clean_times": 3,
    "clean_time": 5.0,
    "clean_distance": 10,
    "dry_tank": 4,
    "dry_height": 60,
    "dry_time_bottom": 210,
    "dry_time_standard": 200,
    "pre_z_height": 10,
    "slider_height": 5,
    "drop_time_bottom": 20,
    "drop_layers_bottom": 20,
    "drop_time_standard": 10,
    "ASS": False,
    "ASS_times": 100,
    "ASS_volume": 470,
    "AMS": False,
    "AMS0_layers": "-1 -1",
    "AMS1_layers": "-1 -1",
    "AMS2_layers": "-1 -1",
    "AMS_tank_offset": 2.5,
    "AMS_volume": 30,
    "plate_rotate_height": 140,
    "glass_rotate_height": -50,
    "lapse": False,
    "lapse_height": 160,
    "lapse_tank": 0,
    "groove": False,
    "alpha": False,
    "beta": False,
    "block_size": 16,
    "wait_after_exposure": 0.5,
    "wait_before_exposure": 0.5,
    "finished_clean": False,
}


class SliceMergeError(RuntimeError):
    pass


def _read_image(path):
    data = np.fromfile(str(path), dtype=np.uint8)
    if data.size == 0:
        return None
    return cv2.imdecode(data, cv2.IMREAD_GRAYSCALE)


def _write_image(path, image):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    ok, encoded = cv2.imencode(path.suffix or ".png", image)
    if not ok:
        raise SliceMergeError(f"failed to encode image: {path}")
    encoded.tofile(str(path))


def _fit_to_resolution(image, width, height):
    if image is None:
        return np.zeros((height, width), dtype=np.uint8)

    if image.ndim == 3:
        image = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    src_h, src_w = image.shape[:2]
    x0 = max((src_w - width) // 2, 0)
    y0 = max((src_h - height) // 2, 0)
    cropped = image[y0:y0 + min(src_h, height), x0:x0 + min(src_w, width)]

    dst = np.zeros((height, width), dtype=np.uint8)
    y = max((height - cropped.shape[0]) // 2, 0)
    x = max((width - cropped.shape[1]) // 2, 0)
    dst[y:y + cropped.shape[0], x:x + cropped.shape[1]] = cropped
    return dst


def _estimate_time(gcode_path):
    seconds = 0.0
    with open(gcode_path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            parts = raw_line.strip().split()
            if not parts:
                continue
            if parts[0] == "wait" and len(parts) >= 2:
                try:
                    seconds += float(parts[1])
                except ValueError:
                    pass
            elif parts[0] == "proj" and len(parts) >= 3:
                try:
                    seconds += float(parts[2])
                except ValueError:
                    pass

    total = int(round(seconds))
    h, rem = divmod(total, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


def _config_bool(config, key):
    return bool(config.get(key, DEFAULT_CONFIG.get(key, False)))


def _material_layers(config, material_index):
    path = Path(config[f"img_path_{material_index}"])
    if not path.exists():
        raise SliceMergeError(f"material image folder does not exist: {path}")

    max_layer = 0
    for file_path in path.iterdir():
        if file_path.suffix.lower() == ".png" and file_path.stem.isdigit():
            max_layer = max(max_layer, int(file_path.stem))
    return max_layer


def _normalize_config(raw_config):
    config = dict(DEFAULT_CONFIG)
    config.update(raw_config or {})

    if "materials" in config:
        materials = config["materials"] or []
        config["material_num"] = len(materials)
        for i, material in enumerate(materials):
            if isinstance(material, dict) and "img_path" in material:
                config[f"img_path_{i}"] = material["img_path"]

    if "material_num" not in config:
        raise SliceMergeError("config.yaml missing required key: material_num")

    config["material_num"] = int(config["material_num"])
    if config["material_num"] <= 0:
        raise SliceMergeError("material_num must be greater than 0")

    for i in range(config["material_num"]):
        path_key = f"img_path_{i}"
        if path_key not in config:
            raise SliceMergeError(f"config.yaml missing required key: {path_key}")
        config[path_key] = str(Path(config[path_key]).expanduser().resolve())

        for exposure_key, default_value in (
            (f"bottom_exposure_time_{i}", 7.0),
            (f"bottom_exposure_current_{i}", 15),
            (f"standard_exposure_time_{i}", 2.5),
            (f"standard_exposure_current_{i}", 15),
        ):
            config.setdefault(exposure_key, default_value)

    for src in range(config["material_num"]):
        for dst in range(config["material_num"]):
            if src == dst:
                continue
            config.setdefault(f"m{src}Tom{dst}", True)
            config.setdefault(f"m{src}Tom{dst}_current", 18)
            config.setdefault(f"m{src}Tom{dst}_time", 2.0)

    return config


class SliceMerger:
    def __init__(self, config_path, output_dir):
        self.config_path = Path(config_path).expanduser().resolve()
        self.output_dir = Path(output_dir).expanduser().resolve()
        self.output_dir.mkdir(parents=True, exist_ok=True)

        with open(self.config_path, "r", encoding="utf-8") as handle:
            self.config = _normalize_config(yaml.safe_load(handle))

        self.res_width = int(
            self.config.get("res_width")
            or self.config.get("output_width")
            or self.config.get("projector_width")
            or DEFAULT_RES_WIDTH
        )
        self.res_height = int(
            self.config.get("res_height")
            or self.config.get("output_height")
            or self.config.get("projector_height")
            or DEFAULT_RES_HEIGHT
        )
        self.after_exposure_time = float(self.config.get("wait_after_exposure", 0.5))
        self.before_exposure_time = float(self.config.get("wait_before_exposure", 0.5))

        self.file = None
        self.now_tank = 0
        self.groove = False
        self.slide = [False, False, False]
        self.will_wait = False
        self.will_groove = False
        self.change_cnt = 0
        self.print_sequence = []
        self.alpha_function = _config_bool(self.config, "alpha")
        self.beta_function = _config_bool(self.config, "beta")
        self.mask1 = None
        self.mask2 = None

    def run(self):
        self._copy_config()
        self._prepare_masks()
        active_pixels, layer_counts = self._scan_input_layers()
        end_tank, change_times = self._plan_tank_path(active_pixels, layer_counts)
        run_gcode = self._write_job(active_pixels, end_tank)
        return change_times, _estimate_time(run_gcode)

    def _copy_config(self):
        dst = self.output_dir / "config.yaml"
        if self.config_path != dst:
            shutil.copyfile(self.config_path, dst)

    def _prepare_masks(self):
        if not self.beta_function:
            return

        block_size = int(self.config["block_size"])
        self.mask1 = np.zeros((self.res_height, self.res_width), dtype=np.uint8)
        self.mask2 = np.zeros((self.res_height, self.res_width), dtype=np.uint8)
        for y in range(0, self.res_height, block_size):
            for x in range(0, self.res_width, block_size):
                target = self.mask1 if (y // block_size + x // block_size) % 2 == 0 else self.mask2
                target[y:y + block_size, x:x + block_size] = 255

    def _scan_input_layers(self):
        material_num = self.config["material_num"]
        layer_counts = [_material_layers(self.config, i) for i in range(material_num)]
        layer_total = max(layer_counts) if layer_counts else 0

        active_pixels = np.zeros((layer_total + 1, material_num), int)
        for layer in range(1, layer_total + 1):
            for material in range(material_num):
                if layer > layer_counts[material]:
                    continue
                image = self._read_layer_image(material, layer)
                active_pixels[layer][material] = cv2.countNonZero(image)
        return active_pixels, layer_counts

    def _read_layer_image(self, material, layer):
        path = Path(self.config[f"img_path_{material}"]) / f"{layer}.png"
        return _fit_to_resolution(_read_image(path), self.res_width, self.res_height)

    def _plan_tank_path(self, active_pixels, layer_counts):
        layer_total = max(layer_counts) if layer_counts else 0
        material_num = self.config["material_num"]
        if layer_total == 0:
            return [0], 0

        active_material_count = np.count_nonzero(active_pixels > 0, axis=1)
        infinity = material_num * max(layer_total, 1) + 1
        cost = np.full((layer_total + 1, material_num), infinity, int)
        previous = np.full((layer_total + 1, material_num), -1, int)
        cost[0, :] = 0

        for layer in range(1, layer_total + 1):
            if active_material_count[layer] == 0:
                cost[layer, :] = cost[layer - 1, :]
                previous[layer, :] = np.arange(material_num)
                continue

            for material in range(material_num):
                if active_pixels[layer][material] <= 0:
                    continue
                for last_material in range(material_num):
                    candidate = (
                        cost[layer - 1][last_material]
                        + active_material_count[layer]
                        - (1 if active_pixels[layer][last_material] > 0 else 0)
                        + (1 if material == last_material and active_material_count[layer] > 1 else 0)
                    )
                    if candidate < cost[layer][material]:
                        cost[layer][material] = candidate
                        previous[layer][material] = last_material

        end_material = int(np.argmin(cost[layer_total, :]))
        change_times = int(cost[layer_total][end_material])
        end_tank = [end_material]
        for layer in range(layer_total, 0, -1):
            last = previous[layer][end_tank[-1]]
            end_tank.append(int(last if last >= 0 else end_tank[-1]))
        end_tank.reverse()
        return end_tank, change_times

    def _write_job(self, active_pixels, end_tank):
        run_gcode = self.output_dir / "run.gcode"
        with open(run_gcode, "w", encoding="utf-8") as handle:
            self.file = handle
            self._write_header(end_tank[0])
            self._build_print_sequence(active_pixels, end_tank)

            for index in range(len(self.print_sequence)):
                self._print_layer(index)

            self.move_plate(self.config["plate_rotate_height"], self.config["z_speed_h"])
            self.file.write("glass %.2f %.2f\n" % (self.config["glass_rotate_height"], self.config["z_speed_h"]))
            self.file.write("fanclean 30\n")
            if self.config["AMS"]:
                self.file.write("AMS 0 backflow 50\n")
                self.file.write("AMS 1 backflow 50\n")
                self.file.write("AMS 2 backflow 50\n")

            if self.config.get("finished_clean", False):
                n = max(0, len(end_tank) - 1)
                self.file.write("tank %d\n" % self.config["clean_tank"])
                self.file.write("clean open\n")
                self.move_plate(
                    self.config["clean_height"]
                    + self.config["layer_height"] * float(n) * self.config["clean_height_coefficient"]
                )
                self.file.write("wait %.2f\n" % (self.config["clean_time"] * 2))
                self.move_plate(self.config["plate_rotate_height"])
                self.file.write("clean close\n")

        self.file = None
        return run_gcode

    def _write_header(self, start_tank):
        self.slide = [
            bool(self.config.get("slide_0", False)),
            bool(self.config.get("slide_1", False)),
            bool(self.config.get("slide_2", False)),
        ]

        if self.config["AMS"]:
            self.ams0_layers = [int(i) for i in str(self.config["AMS0_layers"]).split()]
            self.ams1_layers = [int(i) for i in str(self.config["AMS1_layers"]).split()]
            self.ams2_layers = [int(i) for i in str(self.config["AMS2_layers"]).split()]
        else:
            self.ams0_layers = []
            self.ams1_layers = []
            self.ams2_layers = []

        if self.config["lapse"]:
            self.file.write("cameraMode\n")
        self.file.write("fan open\n")
        self.file.write("clean open\n")
        self.file.write("groove open\n")
        self.groove = False
        self.file.write("wait 3\n")
        self.file.write("fan close\n")
        self.file.write("clean close\n")
        self.file.write("plateEnable\n")
        self.file.write("glassEnable\n")
        self.file.write("rotateEnable\n")
        self.move_plate(self.config["plate_rotate_height"], self.config["z_speed_h"])
        self.file.write("glass %.2f %.2f\n" % (self.config["glass_rotate_height"], self.config["z_speed_h"]))
        self.file.write("tank %.2f %.2f\n" % (start_tank, self.config["r_speed"]))
        self.move_plate(self.config["pre_z_height"], self.config["z_speed_h"])
        self.move_plate(self.config["pre_z_height"], self.config["z_speed_l"])
        self.now_tank = start_tank
        self.change_cnt = 0

    def _build_print_sequence(self, active_pixels, end_tank):
        material_num = self.config["material_num"]
        self.print_sequence = []
        for layer in range(1, len(end_tank)):
            previous_tank = end_tank[layer - 1]
            next_tank = end_tank[layer]
            if active_pixels[layer][previous_tank] > 0:
                self.print_sequence.append([previous_tank, layer])
            for material in range(material_num):
                if (
                    active_pixels[layer][material] > 0
                    and material != previous_tank
                    and material != next_tank
                ):
                    self.print_sequence.append([material, layer])
            if next_tank != previous_tank and active_pixels[layer][next_tank] > 0:
                self.print_sequence.append([next_tank, layer])

    def move_plate(self, height, speed=None):
        if height < self.config["plate_rotate_height"] and self.groove:
            if self.config.get("groove", False):
                self.file.write("groove open\n")
            self.groove = False
        if speed is not None:
            self.file.write("plate %.2f %.2f\n" % (height, speed))
        else:
            self.file.write("plate %.2f\n" % height)
        if height >= self.config["plate_rotate_height"] and not self.groove:
            if self.config.get("groove", False):
                if self.will_wait:
                    self.will_groove = True
                else:
                    self.file.write("groove close\n")
            self.groove = True

    def clean_dry(self, height, layer):
        self.file.write("tank %d\n" % self.config["clean_tank"])
        self.file.write("clean open\n")

        for _ in range(int(self.config["clean_times"])):
            self.move_plate(self.config["clean_height"] + height * self.config["clean_height_coefficient"])
            self.file.write("wait %.2f\n" % self.config["clean_time"])
            self.move_plate(
                self.config["clean_height"]
                + height * self.config["clean_height_coefficient"]
                + self.config["clean_distance"]
            )
        self.will_wait = True
        self.move_plate(self.config["plate_rotate_height"])
        self.file.write("clean close\n")
        self.file.write(
            "wait %.2f\n"
            % (
                self.config["drop_time_bottom"]
                if layer < self.config["drop_layers_bottom"]
                else self.config["drop_time_standard"]
            )
        )
        self.will_wait = False
        if self.will_groove:
            self.file.write("groove close\n")
            self.will_groove = False

        self.change_cnt += 1

        self.file.write("tank %d\n" % self.config["dry_tank"])
        self.move_plate(self.config["dry_height"] + height)
        self.file.write("fan open\n")
        self.file.write(
            "wait %.2f\n"
            % (
                self.config["dry_time_bottom"]
                if layer < self.config["drop_layers_bottom"]
                else self.config["dry_time_standard"]
            )
        )
        self.move_plate(self.config["plate_rotate_height"])
        self.file.write("fan close\n")

        if self.config["ASS"] and self.change_cnt > 0 and self.change_cnt % self.config["ASS_times"] == 0:
            self.file.write("ASS output %d\n" % (self.config["ASS_volume"] + 50))
            self.file.write("ASS input %d\n" % self.config["ASS_volume"])
        if self.config["AMS"] and self.change_cnt in self.ams0_layers:
            self.file.write("tank 2.5\n")
            self.file.write("AMS 0 feed %d\n" % self.config["AMS_volume"])
            self.file.write("wait 1\n")
            self.file.write("AMS 0 backflow 5\n")
        if self.config["AMS"] and self.change_cnt in self.ams1_layers:
            self.file.write("tank 3.5\n")
            self.file.write("AMS 1 feed %d\n" % self.config["AMS_volume"])
            self.file.write("wait 1\n")
            self.file.write("AMS 1 backflow 5\n")
        if self.config["AMS"] and self.change_cnt in self.ams2_layers:
            self.file.write("tank 4.5\n")
            self.file.write("AMS 2 feed %d\n" % self.config["AMS_volume"])
            self.file.write("wait 1\n")
            self.file.write("AMS 2 backflow 5\n")

        if self.config["lapse"]:
            self.move_plate(self.config["lapse_height"])
            self.file.write("tank %d\n" % self.config["lapse_tank"])
            self.file.write("wait 2\n")
            self.file.write("capture\n")
            self.file.write("wait 2\n")

    def _print_layer(self, index):
        tank, layer = self.print_sequence[index]
        dst_img_name = f"{layer}_{tank}.png"
        dst_img = self._read_layer_image(tank, layer)

        if self.beta_function:
            dst_img1 = cv2.bitwise_and(dst_img, dst_img, mask=self.mask1)
            dst_img2 = cv2.bitwise_and(dst_img, dst_img, mask=self.mask2)
            _write_image(self.output_dir / f"{layer}_{tank}_1.png", dst_img1)
            _write_image(self.output_dir / f"{layer}_{tank}_2.png", dst_img2)
        else:
            _write_image(self.output_dir / dst_img_name, dst_img)

        height = self.config["layer_height"] * float(layer)
        if self.now_tank != tank:
            self.move_plate(self.config["pre_z_height"] + height, self.config["z_speed_l"])
            self.will_wait = True
            self.move_plate(self.config["plate_rotate_height"], self.config["z_speed_h"])
            self.file.write("glass %.2f %.2f\n" % (self.config["glass_rotate_height"], self.config["z_speed_h"]))
            self.file.write(
                "wait %.2f\n"
                % (
                    self.config["drop_time_bottom"]
                    if layer < self.config["drop_layers_bottom"]
                    else self.config["drop_time_standard"]
                )
            )
            self.will_wait = False
            if self.will_groove:
                self.file.write("groove close\n")
                self.will_groove = False

            if (not self.alpha_function) or self.config[f"m{self.now_tank}Tom{tank}"]:
                self.clean_dry(height, layer)

            self.file.write("tank %d\n" % tank)
            self.file.write("glass -5.00 %.2f\n" % self.config["z_speed_h"])
            self.file.write("glass 0.00 %.2f\n" % self.config["z_speed_l"])
            self.move_plate(self.config["pre_z_height"] + height)
            self.move_plate(height, self.config["z_speed_l"])
            self.now_tank = tank
        else:
            self.file.write("glass 0.00\n")
            if self.now_tank < len(self.slide) and self.slide[self.now_tank]:
                self.move_plate(height + self.config["slider_height"])
                self.file.write("slide %d\n" % self.now_tank)
            else:
                self.move_plate(height + self.config["back_distance"])
            self.move_plate(height)

        self.file.write("wait 0.5\n")
        exposure_time = (
            self.config[f"bottom_exposure_time_{self.now_tank}"]
            if layer <= self.config["bottom_layers"]
            else self.config[f"standard_exposure_time_{self.now_tank}"]
        )
        exposure_current = (
            self.config[f"bottom_exposure_current_{self.now_tank}"]
            if layer <= self.config["bottom_layers"]
            else self.config[f"standard_exposure_current_{self.now_tank}"]
        )

        if self.alpha_function:
            self._write_alpha_enhance(index, tank, layer, dst_img)

        if self.beta_function:
            self.file.write("proj %s %.2f %d\n" % (f"{layer}_{tank}_1.png", exposure_time, exposure_current))
            self.file.write("wait %.2f\n" % self.before_exposure_time)
            self.file.write("proj %s %.2f %d\n" % (f"{layer}_{tank}_2.png", exposure_time, exposure_current))
        else:
            self.file.write("proj %s %.2f %d\n" % (dst_img_name, exposure_time, exposure_current))

        self.file.write("wait %.2f\n" % self.after_exposure_time)

    def _write_alpha_enhance(self, index, tank, layer, dst_img):
        for offset in (1, 2):
            previous_index = index - offset
            if previous_index < 0:
                continue
            previous_tank, previous_layer = self.print_sequence[previous_index]
            if previous_tank == tank or layer != previous_layer + 1:
                continue

            pre_img = self._read_layer_image(previous_tank, previous_layer)
            en_img = cv2.bitwise_and(pre_img, dst_img)
            kernel = np.ones((3, 3), np.uint8)
            en_img = cv2.erode(en_img, kernel, iterations=1)
            if cv2.countNonZero(en_img) <= 0:
                continue

            enhance_name = f"{layer}_{tank}_enhance.png"
            _write_image(self.output_dir / enhance_name, en_img)
            self.file.write(
                "proj %s %.2f %d\n"
                % (
                    enhance_name,
                    self.config[f"m{previous_tank}Tom{tank}_time"],
                    self.config[f"m{previous_tank}Tom{tank}_current"],
                )
            )


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Merge per-material PNG slices into projector images and run.gcode."
    )
    parser.add_argument("paths", nargs="*", help="Optional positional form: CONFIG OUTPUT")
    parser.add_argument("-c", "--config", help="Path to config.yaml")
    parser.add_argument("-o", "--output", help="Output folder for merged PNG files and run.gcode")
    parser.add_argument("--traceback", action="store_true", help="Print a Python traceback on failure")
    args = parser.parse_args(argv)

    config = args.config
    output = args.output

    if not config and args.paths:
        config = args.paths[0]
    if not output and len(args.paths) >= 2:
        output = args.paths[1]

    if not config:
        parser.error("missing config path")

    config_path = Path(config).expanduser()
    if config_path.is_dir():
        config_path = config_path / "config.yaml"

    if not output:
        output = str(config_path.parent)

    return config_path, Path(output).expanduser(), args.traceback


def main(argv=None):
    config_path, output_dir, show_traceback = parse_args(argv or sys.argv[1:])
    try:
        change_times, run_time = SliceMerger(config_path, output_dir).run()
    except Exception as exc:
        if show_traceback:
            traceback.print_exc()
        else:
            print(f"slice merge failed: {exc}", file=sys.stderr)
        return 1

    print(f"change_times={change_times} run_time={run_time} output={output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
