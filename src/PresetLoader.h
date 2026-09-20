#pragma once

#include "PresetLibrary.h"

#include <QString>

// Loads the machine + material preset library.
//
// Supports the lightweight indented "key: value" format used by
// `配置参数示例.txt` / `config/machine_material_presets.yaml`:
//
//   device_name_list: M2-001, M2-002
//   M2-001:
//       projector_width: 3840
//       current_map: 10, 11, 12
//       strength_map: 0.3, 2.4, 3.6
//       material_num_limit: 3
//   material_name_list: hard1, soft1
//   hard1:
//       color: "#2ca0dc"
//       bottom_exposure_time: 7.0
//       bottom_exposure_current: 15
//
// Limitations (documented intentionally; this is NOT a full YAML parser):
//   - Only two indentation levels (top-level keys and one nested level).
//   - Lists are comma-separated on a single line.
//   - Full-line comments starting with '#' are ignored; values may be quoted.
//   - Block names must appear in device_name_list / material_name_list.
class PresetLoader {
public:
    static bool load(const QString& path, PresetLibrary* outLibrary, QString* errorMessage);
};
