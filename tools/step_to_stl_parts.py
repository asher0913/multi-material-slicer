#!/usr/bin/env python3
"""
Convert a STEP/STP file into one STL per solid plus a manifest.json.

The manifest preserves the assembly hierarchy: each node has a "path" list
from root to the node. Nodes representing sub-assemblies have no "stl"
field; leaf solid nodes have a "stl" file reference.

The GUI interprets "path" to build a multi-level assembly tree where
sub-assemblies can be transformed together and each leaf can have an
independent material.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


def decode_step_string(text: str) -> str:
    """Decode common STEP escaped unicode fragments such as \\X2\\5B9E4F53\\X0\\."""

    def repl(match: re.Match[str]) -> str:
        hex_text = re.sub(r"\s+", "", match.group(1))
        try:
            return bytes.fromhex(hex_text).decode("utf-16-be")
        except Exception:
            return match.group(0)

    return re.sub(r"\\X2\\([0-9A-Fa-f]+)\\X0\\", repl, text)


def _preferred_names_from_step(step_path: Path) -> list[str]:
    try:
        data = step_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    names: list[str] = []
    for raw_name in re.findall(r"MANIFOLD_SOLID_BREP\('([^']*)'", data):
        decoded = decode_step_string(raw_name).strip()
        names.append(decoded or f"SOLID-{len(names) + 1}")
    return names


def safe_filename(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^\w.\-\u4e00-\u9fff]+", "_", name, flags=re.UNICODE).strip("._")
    return cleaned or fallback


# ---------------------------------------------------------------------------
# Flat converter (original behaviour, kept for fallback)
# ---------------------------------------------------------------------------

def _convert_flat(input_path: Path, output_dir: Path, linear_deflection: float) -> dict[str, Any]:
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.STEPControl import STEPControl_Reader
    from OCP.StlAPI import StlAPI_Writer
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer

    reader = STEPControl_Reader()
    status = reader.ReadFile(str(input_path))
    if status != IFSelect_RetDone:
        raise RuntimeError(f"无法读取 STEP 文件: {input_path}")

    transferred = reader.TransferRoots()
    if transferred <= 0:
        raise RuntimeError("STEP 文件没有可转换的根形状。")

    shape = reader.OneShape()
    BRepMesh_IncrementalMesh(shape, linear_deflection).Perform()

    output_dir.mkdir(parents=True, exist_ok=True)
    writer = StlAPI_Writer()
    try:
        writer.SetASCIIMode(False)
    except AttributeError:
        pass

    preferred_names = _preferred_names_from_step(input_path)
    assembly_name = decode_step_string(input_path.stem) or input_path.name
    parts: list[dict[str, Any]] = []
    explorer = TopExp_Explorer(shape, TopAbs_SOLID)
    index = 1
    while explorer.More():
        solid = explorer.Current()
        display_name = preferred_names[index - 1] if index - 1 < len(preferred_names) else f"SOLID-{index}"
        stl_name = f"{index:03d}_{safe_filename(display_name, f'SOLID_{index}')}.stl"
        stl_path = output_dir / stl_name
        BRepMesh_IncrementalMesh(solid, linear_deflection).Perform()
        result = writer.Write(solid, str(stl_path))
        if result is False or not stl_path.exists():
            raise RuntimeError(f"无法写出 STL: {stl_path}")
        parts.append(
            {
                "name": display_name,
                "stl": stl_name,
                "path": [assembly_name, display_name],
            }
        )
        index += 1
        explorer.Next()

    if not parts:
        raise RuntimeError("STEP 文件没有找到可导出的 SOLID 实体。")

    return {"assembly": assembly_name, "parts": parts}


# ---------------------------------------------------------------------------
# Hierarchical converter – uses XCAF to preserve the assembly tree
# ---------------------------------------------------------------------------

def _convert_hierarchical(input_path: Path, output_dir: Path, linear_deflection: float) -> dict[str, Any]:
    """
    Read the STEP file with STEPCAFControl_Reader so we can walk the
    product/assembly tree.  Leaf shape nodes are exported as STL files;
    sub-assembly nodes produce container entries without a ``stl`` field.

    The manifest uses a flat ``parts`` list where each part carries its
    full ``path`` from the assembly root.  The GUI rebuilds the tree
    from those paths.
    """
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.STEPCAFControl import STEPCAFControl_Reader
    from OCP.StlAPI import StlAPI_Writer
    from OCP.TCollection import TCollection_ExtendedString
    from OCP.TDataStd import TDataStd_Name
    from OCP.TDF import TDF_Label, TDF_LabelSequence
    from OCP.TDocStd import TDocStd_Document
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopLoc import TopLoc_Location
    from OCP.XCAFApp import XCAFApp_Application
    from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ShapeTool

    # ---------- Step 1 – read STEP into an XCAF document ----------
    app = XCAFApp_Application.GetApplication_s()
    doc = TDocStd_Document(TCollection_ExtendedString("step"))
    app.InitDocument(doc)

    reader = STEPCAFControl_Reader()
    reader.SetColorMode(False)
    reader.SetLayerMode(False)
    reader.SetNameMode(True)
    status = reader.ReadFile(str(input_path))
    if status != IFSelect_RetDone:
        raise RuntimeError(f"无法读取 STEP 文件: {input_path}")

    if not reader.Transfer(doc):
        raise RuntimeError("无法将 STEP 数据转移到 XCAF 文档。")

    shape_tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    if shape_tool is None:
        raise RuntimeError("XCAF shape tool 不可用。")

    # ---------- Step 2 – collect leaf shapes with their full paths ----------
    output_dir.mkdir(parents=True, exist_ok=True)
    writer = StlAPI_Writer()
    try:
        writer.SetASCIIMode(False)
    except AttributeError:
        pass

    default_assembly_name = decode_step_string(input_path.stem) or input_path.name
    preferred_names = _preferred_names_from_step(input_path)
    parts: list[dict[str, Any]] = []
    stl_index = [0]  # mutable counter across recursion

    def _label_name(label: TDF_Label, fallback: str) -> str:
        attr = TDataStd_Name()
        try:
            if label.FindAttribute(TDataStd_Name.GetID_s(), attr):
                text = decode_step_string(str(attr.Get().ToExtString())).strip()
                if text:
                    return text
        except Exception:
            pass
        return fallback

    def _is_useful_name(name: str) -> bool:
        stripped = name.strip()
        return bool(stripped) and not stripped.isdigit()

    def _display_name(component_label: TDF_Label, referred_label: TDF_Label, fallback: str) -> str:
        component_name = _label_name(component_label, fallback)
        referred_name = _label_name(referred_label, fallback)
        if _is_useful_name(component_name):
            return component_name
        if _is_useful_name(referred_name):
            return referred_name
        return fallback

    def _solid_fallback_name() -> str:
        if stl_index[0] < len(preferred_names):
            return preferred_names[stl_index[0]] or f"SOLID-{stl_index[0] + 1}"
        return f"SOLID-{stl_index[0] + 1}"

    def _export_shape(shape: Any, display_name: str, parent_path: list[str]) -> None:
        if shape is None or shape.IsNull():
            return

        BRepMesh_IncrementalMesh(shape, linear_deflection).Perform()
        solids_found = 0
        exp = TopExp_Explorer(shape, TopAbs_SOLID)
        while exp.More():
            solid_shape = exp.Current()
            part_name = display_name if solids_found == 0 else f"{display_name}_{solids_found + 1}"
            stl_index[0] += 1
            stl_name = f"{stl_index[0]:03d}_{safe_filename(part_name, f'SOLID_{stl_index[0]}')}.stl"
            stl_path = output_dir / stl_name

            BRepMesh_IncrementalMesh(solid_shape, linear_deflection).Perform()
            result = writer.Write(solid_shape, str(stl_path))
            if result is False or not stl_path.exists():
                raise RuntimeError(f"无法写出 STL: {stl_path}")

            parts.append({"name": part_name, "stl": stl_name, "path": parent_path + [part_name]})
            solids_found += 1
            exp.Next()

        if solids_found == 0:
            part_name = display_name or _solid_fallback_name()
            stl_index[0] += 1
            stl_name = f"{stl_index[0]:03d}_{safe_filename(part_name, f'SOLID_{stl_index[0]}')}.stl"
            stl_path = output_dir / stl_name

            BRepMesh_IncrementalMesh(shape, linear_deflection).Perform()
            result = writer.Write(shape, str(stl_path))
            if result is False or not stl_path.exists():
                raise RuntimeError(f"无法写出 STL: {stl_path}")

            parts.append({"name": part_name, "stl": stl_name, "path": parent_path + [part_name]})

    def _walk_assembly(label: TDF_Label, parent_path: list[str], parent_location: TopLoc_Location) -> None:
        """Recursively walk XCAF assembly components."""
        components = TDF_LabelSequence()
        if not XCAFDoc_ShapeTool.GetComponents_s(label, components, False) or components.Length() == 0:
            shape = XCAFDoc_ShapeTool.GetShape_s(label)
            _export_shape(shape.Moved(parent_location), _label_name(label, _solid_fallback_name()), parent_path)
            return

        for i in range(components.Length()):
            component_label = components.Value(i + 1)
            referred_label = TDF_Label()
            has_referred = XCAFDoc_ShapeTool.GetReferredShape_s(component_label, referred_label)
            target_label = referred_label if has_referred else component_label
            fallback = f"Node-{len(parts) + 1}"
            node_name = _display_name(component_label, target_label, fallback)
            component_location = parent_location.Multiplied(XCAFDoc_ShapeTool.GetLocation_s(component_label))

            if XCAFDoc_ShapeTool.IsAssembly_s(target_label):
                current_path = parent_path + [node_name]
                parts.append({"name": node_name, "path": current_path})
                _walk_assembly(target_label, current_path, component_location)
            else:
                shape = XCAFDoc_ShapeTool.GetShape_s(target_label)
                _export_shape(shape.Moved(component_location), node_name, parent_path)

    # ---------- Step 3 – walk top-level free shapes ----------
    top_labels = TDF_LabelSequence()
    shape_tool.GetFreeShapes(top_labels)

    if top_labels.Length() == 0:
        # Fall back: if XCAF gives nothing useful, use the flat exporter
        raise RuntimeError("XCAF: no free shapes")

    if top_labels.Length() == 1:
        root_label = top_labels.Value(1)
        assembly_name = _label_name(root_label, default_assembly_name)
        if XCAFDoc_ShapeTool.IsAssembly_s(root_label):
            _walk_assembly(root_label, [assembly_name], TopLoc_Location())
        else:
            shape = XCAFDoc_ShapeTool.GetShape_s(root_label)
            _export_shape(shape, _label_name(root_label, _solid_fallback_name()), [assembly_name])
    else:
        assembly_name = default_assembly_name
        for i in range(top_labels.Length()):
            label = top_labels.Value(i + 1)
            node_name = _label_name(label, f"Root-{i + 1}")
            if XCAFDoc_ShapeTool.IsAssembly_s(label):
                current_path = [assembly_name, node_name]
                parts.append({"name": node_name, "path": current_path})
                _walk_assembly(label, current_path, TopLoc_Location())
            else:
                shape = XCAFDoc_ShapeTool.GetShape_s(label)
                _export_shape(shape, node_name, [assembly_name])

    if not parts:
        raise RuntimeError("STEP 文件没有找到可导出的实体。")

    return {"assembly": assembly_name, "parts": parts}


def _cleanup_redundant_hierarchy(parts: list[dict[str, Any]]) -> None:
    """
    If a sub-assembly node has exactly one child, merge them so the tree
    does not contain unnecessary intermediate levels.
    """
    # Build a lookup of direct children per path
    children_by_parent: dict[tuple[str, ...], list[int]] = {}
    for idx, part in enumerate(parts):
        path = tuple(part["path"])
        if len(path) < 2:
            continue
        parent = path[:-1]
        children_by_parent.setdefault(parent, []).append(idx)

    changed = True
    while changed:
        changed = False
        for parent, child_indices in list(children_by_parent.items()):
            if len(child_indices) != 1:
                continue
            child_idx = child_indices[0]
            child = parts[child_idx]
            if "stl" in child:
                # Single solid child → promote it, removing the container
                child["path"] = list(parent) + [child["name"]]
                # Remove the container entry from every index tracking
                # Build reverse lookup for the container
                # (The container has path=parent, find its index)
                for candidate_idx, candidate in enumerate(parts):
                    if tuple(candidate["path"]) == parent:
                        del parts[candidate_idx]
                        changed = True
                        break
                if changed:
                    break
            else:
                # Single sub-assembly child → promote grandchildren
                pass  # keep for now, the GUI handles this cleanly


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

def convert_with_ocp(input_path: Path, output_dir: Path, linear_deflection: float) -> dict[str, Any]:
    """
    Try hierarchical conversion first; fall back to flat on failure.
    """
    try:
        result = _convert_hierarchical(input_path, output_dir, linear_deflection)
        if result["parts"]:
            return result
    except Exception:
        pass  # fall through to flat converter

    return _convert_flat(input_path, output_dir, linear_deflection)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Convert STEP solids to STL parts.")
    parser.add_argument("--input", required=True, help="Path to .step/.stp file")
    parser.add_argument("--output", required=True, help="Output directory")
    parser.add_argument("--linear-deflection", type=float, default=0.08, help="OCC meshing deflection in mm")
    args = parser.parse_args(argv)

    input_path = Path(args.input).expanduser().resolve()
    output_dir = Path(args.output).expanduser().resolve()
    if not input_path.exists():
        print(f"输入 STEP 文件不存在: {input_path}", file=sys.stderr)
        return 2

    try:
        manifest = convert_with_ocp(input_path, output_dir, args.linear_deflection)
        manifest_path = output_dir / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        solid_count = sum(1 for part in manifest["parts"] if part.get("stl"))
        print(f"STEP 转换完成: {solid_count} 个实体 -> {output_dir}")
        return 0
    except Exception as exc:
        print(f"STEP 转换失败: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
