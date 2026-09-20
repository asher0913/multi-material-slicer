# Architecture

## Processing pipeline

The desktop application owns project state, model transforms, material
assignments, and workflow orchestration. CPU-heavy export and backend work run
outside the UI event loop so rendering and cancellation remain responsive.

1. `StlMesh` parses ASCII or binary STL into a shared triangle mesh. STEP input
   is converted into per-solid STL files and a hierarchy manifest by
   `step_to_stl_parts.py`.
2. `MainWindow` reconstructs the assembly tree and stores local transforms on
   assembly and leaf nodes. World transforms are composed through the parent
   chain.
3. `OpenGLView` uploads geometry to VBOs and renders the interactive preview.
4. `SliceExporter` intersects transformed triangles with each layer plane and
   rasterizes one monochrome mask directory per material.
5. `ConfigWriter` serializes printer, material, exposure, and transition
   settings to `config.yaml`.
6. `SliceWorker` launches the Python backend with a timeout and cancellation
   path. The backend plans a low-change material sequence, merges masks, and
   emits `run.gcode`.

## Trust boundaries

Model files and YAML configurations are untrusted inputs. The importer limits
file size, validates triangle counts before allocation, rejects non-finite
coordinates, and reports malformed data without continuing with a partial
mesh. Preset parsing validates numeric values before updating application
state.

The Python backend writes only below the user-selected output directory. Build
and packaging artifacts are kept in ignored project-local directories.

## Concurrency and failure handling

Export runs in a worker thread. Progress and completion are returned through
Qt signals, while cancellation is represented explicitly rather than by
terminating the UI thread. External helper processes have a bounded runtime.
Failures are surfaced in the application log and leave the source models
unchanged.

## Design trade-offs

- The in-house slicer keeps the prototype self-contained but does not attempt
  every repair and support-generation feature of a production slicer.
- STEP conversion is isolated behind a JSON manifest so the OpenCascade helper
  can evolve independently of the GUI.
- PNG masks are easy to inspect and debug, at the cost of more disk I/O than a
  streaming binary format.
- Machine commands are deliberately configuration-driven, because motion and
  exposure semantics vary between printer controllers.
