# RealWaterSimulator — Project Status & Developer Handoff

> **Purpose of this document:** a single, complete snapshot of the repository so that
> anyone (or any AI agent) can clone it and continue implementing features, fixes, and
> patches without re-discovering how things work. It documents the architecture, the
> exact current state, every subsystem, the known issues, and the conventions to follow.
>
> **Last updated:** reflects the working tree on branch `eren-dev-stockham-fft-pbr`
> with a large body of **uncommitted** work (see [§2 Git State](#2-git-state)).

---

## 1. What This Project Is

A real-time, GPU-accelerated **ocean / water simulator** in **C++17 + OpenGL 4.6**, built
on GLFW + GLAD + GLM + STB (all vendored). It started as an FFT ocean and has grown into
a small scene: a crisp FFT sea inside a ring of photoreal coastal cliffs, with rain,
interactive ripples, loadable glTF models, procedural terrain, and moving vehicles
(jet-ski / yacht / big-ship).

**Runs on Linux and Windows.** Same OpenGL 4.6 + C++17 code; CMake handles both. Developed
and tested on Linux (NVIDIA RTX 3090, driver 535, OpenGL 4.6). See `LINUX_BUILD.md` for
the Linux dependency/build guide.

### Current scene (what you see when you run it)
- A bounded **512-resolution FFT ocean** (1024 m patch) fixed in world space.
- A **ring of 8 coastal-cliff instances** (real Poly Haven scan) enclosing the water like a lagoon.
- A **foreground rock** (Poly Haven marble cliff) floating in the bay.
- **Three vehicles** (jet-ski, yacht, big-ship) at random positions, cruising and turning at the bay edge.
- **Rain** with splashes and expanding ripple rings on the water.
- Full **PBR-ish water shading** (reflection + refraction + subsurface scatter + sun glitter + foam).

---

## 2. Git State

- **Current branch:** `eren-dev-stockham-fft-pbr`
- **Remote:** `origin` → `https://github.com/ProGitEren/RealWaterSimulatorOpenGL.git`
- **Last commit:** `815af16 Rewrite ocean FFT to Stockham (true 512) + PBR water shading`
- **Main branch:** `main`

### ⚠️ There is a LOT of uncommitted work in the tree
Everything below the FFT/PBR commit (the entire model-loading system, terrain, cliffs,
vehicles, gamma fixes, fixed water, faster camera) is **uncommitted**. Before continuing,
**commit it** so it is not at risk. Suggested: stage source/shaders/models, **not** build
artifacts.

**Modified (tracked):** `CMakeLists.txt`, `src/main.cpp`, `src/core/Camera.cpp`,
`src/ocean/OceanMesh.cpp`, `build/output.mp4` (do not commit the mp4).

**Untracked (new — all should be committed except build dirs):**
- `src/graphics/Mesh.{h,cpp}`, `Model.{h,cpp}`, `Texture.{h,cpp}`, `Terrain.{h,cpp}`, `RockGenerator.{h,cpp}`
- `assets/shaders/object.{vert,frag}`, `terrain.{vert,frag}`
- `assets/models/` (the glTF assets), `assets/textures/terrain/`
- `external/cgltf/` (vendored single-header glTF parser)
- `build_linux/` ← **do NOT commit** (build output)

### `.gitignore` — FIXED (was broken)
The committed `.gitignore` previously had literal quotes around each entry (`"build/"` etc.),
which git treated literally, so the rules did nothing — that's why `build/` (774 files) got
tracked and `build_linux/` was not ignored. It has been **rewritten** with correct, unquoted
patterns covering `build*/`, object/binary files, `frames/`, `*.mp4`, and IDE/OS cruft.

> The already-tracked `build/` artifacts (774 files) are still in the index from before the
> fix. To untrack them without deleting your local copy:
> ```bash
> git rm -r --cached build/ && git commit -m "Untrack stale build/ artifacts"
> ```
> The single-line push command in this repo's handoff explicitly excludes them regardless.

---

## 3. Build & Run

### Linux (primary)
```bash
# one-time deps (Ubuntu/Debian) — see LINUX_BUILD.md for other distros
sudo apt-get install -y build-essential cmake libgl1-mesa-dev libglu1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev xorg-dev

# build (use a separate dir so it doesn't collide with the tracked Windows build/)
mkdir -p build_linux && cd build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# MUST run from inside the build dir so the relative ../assets/ paths resolve
./RealWaterSimulator
```

**Important:** all asset paths in the code are **relative** (`../assets/...`). The binary
*must* be launched from inside its build directory. If you add a new shader/model, use the
same `../assets/...` convention.

### Windows 11
```bat
:: from a "Developer Command Prompt for VS" (or any shell with CMake + VS toolchain)
git clone <repo-url> RealWaterSimulator
cd RealWaterSimulator
cmake -S . -B build_win -DCMAKE_BUILD_TYPE=Release
cmake --build build_win --config Release

:: run from the build dir so ../assets/ resolves (CMake puts the .exe in build_win/)
cd build_win
RealWaterSimulator.exe
```
Or open the folder in Visual Studio 2019/2022 (it has native CMake support) and build the
`RealWaterSimulator` target. Requires an OpenGL 4.6 GPU + up-to-date GPU driver.

> **Do not reuse the tracked `build/` directory** — it holds stale artifacts from a previous
> machine's configure (different absolute paths, different CMake/compiler version). Always
> configure fresh into `build_win/`.

See [§3a Windows 11 Portability Assessment](#3a-windows-11-portability-assessment) for a
detailed, pinpointed review of Windows-specific risks.

---

## 3a. Windows 11 Portability Assessment

The codebase is written to be cross-platform (core OpenGL 4.6 + standard C++17). A full scan
was done; the verdict and the specific risk points are below. **Most likely it builds and runs
on Windows 11 as-is**, but here are the exact things to check/fix if it doesn't, ordered by
likelihood.

### ✅ Already verified clean (no action needed)
- **No POSIX-only code in `src/`** — no `unistd.h`, `dlopen`, `dirent.h`, `sys/stat.h`,
  `pthread`, `__attribute__`, `alloca`, or VLAs.
- **No `M_PI` reliance** — the code defines its own `kPi` / `kPiF` constants (MSVC doesn't
  provide `M_PI` without `_USE_MATH_DEFINES`).
- **`Window.cpp`** uses standard GLFW + GLAD context creation, with an `#ifdef __APPLE__`
  guard. Requests a 4.6 core profile — fine on Windows with a modern driver.
- **GLFW 3.4.0** (vendored, `external/glfw/`) has first-class Windows support and builds from
  source via `add_subdirectory` — it auto-links `gdi32`/`user32` etc. on Windows.
- **GLAD, GLM, STB, cgltf** are all platform-neutral (headers / single C file).
- **CMake** is correct: the Linux-only `dl`/`pthread` linking and `-Wall` flags are guarded by
  `if(UNIX AND NOT APPLE)`, so MSVC won't see them.
- The two fixes below (FIX-1, FIX-2) were already applied to remove latent Windows risks.

### Risk points — easy to pinpoint if something breaks

**[RESOLVED] FIX-1 — `<cstdlib>` for `rand()`/`RAND_MAX` in `main.cpp`.**
`main.cpp` uses `rand()` but only got `<cstdlib>` transitively via other headers on GCC. MSVC
may not include it transitively → "`rand` is not defined". **Fixed:** `#include <cstdlib>` was
added to `main.cpp`. (If you see a `rand`-related compile error on Windows, confirm this
include is present.)

**[RESOLVED] FIX-2 — `frames/` directory not created before recording.**
Pressing **R** writes `frames/frame_N.png` via `stbi_write_png`, which **silently fails** if
the directory doesn't exist (true on Linux and Windows on a fresh clone). **Fixed:** the R
handler now calls `std::filesystem::create_directories("frames")`. (`<filesystem>` was added
to `main.cpp`.)

**RISK-A — `<filesystem>` linkage (low).** MSVC 2019+/GCC 9+ provide `<filesystem>` with no
extra linking. Only very old toolchains need `-lstdc++fs` (GCC < 9). On Windows with a recent
VS this is a non-issue. *Pinpoint:* a linker error mentioning `std::filesystem` → upgrade VS
or add the lib.

**RISK-B — case-sensitive includes (low).** Windows filesystems are case-insensitive, so a
mismatched `#include` case won't fail on Windows but *would* on Linux. All current includes
match file case, so this is fine in both directions today — just keep new includes
case-exact.

**RISK-C — OpenGL 4.6 driver requirement (environmental, not code).** On Windows the OpenGL
version comes from the **GPU vendor driver**, not the OS. Intel iGPUs in particular may cap at
4.5/4.6 depending on driver. *Pinpoint:* if the FFT looks broken or shaders fail to compile,
check the printed `OpenGL version:` line on startup — it must report 4.6.

**RISK-D — high-DPI / window scaling (cosmetic).** Windows 11 display scaling can make the
fixed 1024×768 window render at an unexpected size; not a failure, just visual. GLFW handles
this reasonably; only revisit if the window looks wrong.

**RISK-E — MSVC stricter warnings-as-errors (only if you enable `/WX`).** The project does not
set `/WX`; default MSVC builds will compile. If a stricter CI enables it, expect the usual
narrowing-conversion / unused-variable warnings (none are currently errors).

### Bottom line
No known **blocking** Windows issues remain after FIX-1/FIX-2. Expect a clean
`cmake --build` on Windows 11 with VS 2019/2022 and a current GPU driver. If it fails, the
error will almost certainly map to one of RISK-A…C above.

### Requirements
- GPU + driver with **OpenGL 4.6** (compute shaders, SSBO, image load/store).
- GCC 9+ / Clang 10+ / MSVC. CMake 3.15+.

---

## 4. Repository Layout

```
RealWaterSimulatorOpenGL/
├── CMakeLists.txt              Cross-platform build (lists every src/*.cpp + glad.c)
├── LINUX_BUILD.md              Linux dependency/build/troubleshooting guide
├── PROJECT_STATUS.md           <-- this file
├── assets/
│   ├── shaders/                GLSL (loaded at runtime via ../assets/shaders/...)
│   ├── textures/skybox/        Cubemap faces (+ sky_1/2/3, environment_1 variants)
│   ├── textures/terrain/       Tiling terrain textures (rock_diff.jpg, rock_nor.jpg)
│   └── models/                 glTF models (rocks, cliff, jet-ski, yacht, big-ship)
├── external/                   Vendored, header/compiled-in: glad, glfw, glm, stb, cgltf
└── src/
    ├── main.cpp                Entry point: scene setup, render loop, input, vehicles
    ├── core/                   Window (GLFW), Camera (fly cam)
    ├── graphics/               Shader, Mesh, Model (glTF), Texture, Terrain, RockGenerator
    ├── ocean/                  GPUFFTOcean, OceanMesh, GPUDisturbance
    └── water/                  WaterSimulation (CPU grid), RainSystem
```

### Backup / dead files (ignore unless archaeology needed)
`src/main_v1.cpp … main_v8.cpp`, `src/main_gerstner_backup.cpp`, `main_pre_gpu_fft_backup.cpp`,
and `assets/shaders/standard_gerstner_backup.*`, `standard_pre_gpu_fft.*` are **old versions**,
not built. The active `Terrain` system is also currently **unused** in `main.cpp` (the
cliffs replaced it) but kept for reference.

---

## 5. Architecture & Data Flow

### Render loop (in `main.cpp::main()`), per frame:
1. **Input** — keyboard/mouse; toggles (wireframe, vehicle pause), live ocean tuning.
2. **Fixed-timestep physics** (`kFixedDt = 1/60`, accumulator pattern):
   - `water.update()` — CPU ripple grid
   - `rainSystem.update()` — spawn/age raindrops, splashes, ripples
   - `ocean.update()` — runs the entire GPU FFT pipeline (see §6)
   - `disturbance.update()` — propagate interactive (C-key) waves
   - **vehicle movement** — advance each vehicle along its heading; steer back at bay bound
3. **Render**:
   - Bind ocean textures to units 1/2/3, skybox cubemap to unit 0.
   - Draw **ocean** (`standard.vert/frag`) — displaced FFT grid, full water shading.
   - Draw **solid objects** (`object.vert/frag`): rock, cliff ring, vehicles.
   - Draw **skybox** (depth-trick), then **rain** (transparent, after skybox).
   - Optional frame recording to `frames/*.png` (R key).

### Texture unit allocation (important — avoid clashes when adding features)
| Unit | Used for |
|---|---|
| 0 | skybox cubemap |
| 1 | ocean displacement map |
| 2 | ocean normal map |
| 3 | disturbance height map |
| 4 | SSBO binding point for rain ripples (`std430 binding = 4`) — note: this is an SSBO *binding*, separate from texture units |
| 4–7 | object material maps (albedo/normal/roughness/AO) in `Model::draw` |
| 4–9 | terrain triplanar maps in the (currently unused) terrain path |

> When adding objects, follow `Model::draw`'s convention (material maps on units 4–7).

---

## 6. The Ocean FFT (most important subsystem)

**This is a Stockham ping-pong FFT running at true 512.** It was rewritten from an older
single-workgroup shared-memory FFT that was hard-capped at 256.

### Why Stockham
The old FFT did the whole 1D transform inside one GPU workgroup using `shared[256]`
arrays → impossible to exceed 256. The Stockham formulation does **one butterfly stage per
dispatch**, ping-ponging between two image textures across `log2(N)` CPU-driven passes — no
shared memory, no size cap.

### Pipeline (per `GPUFFTOcean::update`), all compute shaders:
```
initial spectrum (once)  ── fft_initial_spectrum.comp   (IFREMER directional spectrum)
        │
   phase update           ── fft_phase.comp              (time evolution)
        │
   spectrum               ── fft_spectrum.comp           (h(k,t) + choppiness dx/dz)
        │
   HORIZONTAL FFT  ×log2(N) passes ── fft_horizontal.comp  (Stockham, ping-pong)
        │
   VERTICAL FFT    ×log2(N) passes ── fft_vertical.comp    (Stockham, ping-pong)
        │
   displacement           ── fft_displacement.comp       (1/N normalize + height/horiz scale)
        │
   normals + foam         ── fft_normal.comp             (finite-diff normals, Jacobian foam)
        └─> displacementTexture (rgba32f), normalTexture (rgba16f, .a = foam)
```

### Key facts you must know to modify it
- **Resolution:** `kOceanResolution = 512` in `main.cpp`. The Stockham FFT supports any
  power-of-two up to `kMaxResolution = 1024` (in `GPUFFTOcean.cpp`).
- **Data layout across the two FFT textures (preserved throughout):**
  - Texture A (`rgba32f`): `xy` = complex dx, `zw` = complex height (h)
  - Texture B (`rg32f`): `xy` = complex dz
- **Ping-pong bookkeeping:** the dispatch loop in `GPUFFTOcean::update()` alternates between
  the `intermediate*` and `spatial*` texture pairs; `m_fftFinalA/B` track where the result
  landed (parity of `log2(N)`), and the displacement stage binds those.
- **Normalization:** Stockham does **not** normalize per pass, so `fft_displacement.comp`
  applies `1/N` then multiplies by `heightScale` / `horizontalScale`.
- **Amplitude tuning** (in `GPUFFTOcean.cpp` constructor): `m_heightScale = 1.0`,
  `m_horizontalScale = 0.08`, `m_timeScale = 2.0`. Waves spiky → lower these; flat → raise.
- **Mesh:** `OceanMesh` is `kOceanMeshRes = 1024` @ `kOceanMeshTile = 1.0` (1024 m, 1 m
  vertex spacing). 1 m spacing is required so the geometry can actually resolve the 512 FFT
  detail (2 m smeared it).
- **GOTCHA — UV tiling:** `standard.vert` sets `OceanUV = worldPos.xz/oceanSize + 0.5`
  with **no `fract()` and no scale multiplier**. A `>1` tex-coord scale broke tileability
  at the FFT patch edge and produced a **bright seam line**. Do not reintroduce it.
- The IFREMER spectrum in `fft_initial_spectrum.comp` matches the reference repo
  `achalpandeyy/OceanFFT`. There is **no** high-frequency geometry cutoff (removing it is
  what restored crisp capillary detail at 512).

### Water shading (`standard.frag`)
Full physically-based ocean model, composited in HDR then tonemapped (`1 - exp(-c*exposure)`):
real **cubemap reflection** (with horizon-tint fallback), **refraction** (depth-tinted body
deep-navy → teal), **subsurface scatter** (teal glow on sun-backlit crests), **Fresnel**,
sun **glint + glitter**, sun-shaded **foam** (from normal-map alpha), and distance haze.
`kExposure`, `kScatterColor`, `kDeep`/`kShallow`, glint/glitter multipliers are the knobs.

---

## 7. Model / Asset System (glTF)

### Loader: `src/graphics/Model.{h,cpp}` (uses `external/cgltf/cgltf.h`)
- Loads `.gltf`/`.glb` via **cgltf** (single-header, MIT, vendored like stb).
- **Walks the glTF node hierarchy** and **bakes each node's world transform into the
  vertices** (with inverse-transpose for normals). This is essential: the yacht (23 meshes)
  and big-ship (62 meshes) are multi-part models positioned by node transforms — without
  this they collapse to a jumble at the origin.
- Extracts per-mesh material: albedo/roughness textures (`metallic_roughness`), normal,
  occlusion, **and** `base_color_factor` (used as a flat colour when there is no albedo
  texture — e.g. the untextured yacht/ship).
- External texture URIs only (Poly Haven / typical exports). **Embedded textures in `.glb`
  are NOT yet supported** — add this if you import a self-contained `.glb` whose textures
  don't show.
- Transform API: `setPosition`, `setScale(float)` / `setScale(vec3)` (non-uniform),
  `setRotationY(radians)`. `draw(shader)` sets `model` uniform and binds materials.

### `Mesh.{h,cpp}`
RAII VAO/VBO/EBO wrapper (movable, non-copyable). Vertex = `{vec3 position, vec3 normal,
vec2 uv}`. Holds a `Material` (texture ids + `baseColor`).

### `Texture.{h,cpp}`
`loadTexture2D(path, srgb)` via stb. **sRGB matters:** colour/albedo maps load with
`srgb=true` (GPU returns linear); data maps (normal/roughness/AO) with `srgb=false`.
**Object/terrain shaders must gamma-encode (`pow(c, 1/2.2)`) at the end** or everything
renders dark/muddy — this was a real bug that was fixed.

### `object.vert/frag`
Lit shader for solid models. Samples albedo/normal/roughness/AO when present, else uses
`baseColor`. Does **derivative-based normal mapping** (no tangent attribute needed).
Uses inverse-transpose normal matrix (supports non-uniform scale). **Gamma-encodes** output.

### Available models (`assets/models/`)
| Folder | File | Notes |
|---|---|---|
| `rock_marble_cliff_05/` | `marble_cliff_05_4k.gltf` | textured; used as foreground rock |
| `rock_marble_cliff_06/` | `marble_cliff_06_4k.gltf` | textured; spare |
| `rock_riverbed_rock/` | `dry_riverbed_rock_4k.gltf` | textured; spare |
| `mountain_terrain/` | `coastal_cliff_04_4k.gltf` | textured; ~87 m cliff strip → the cliff ring |
| `jet-ski/` | `scene.gltf` | textured; **node matrix bakes a 0.01 FBX scale** → needs large scale multiplier (~120) |
| `yacht/` | `scene.gltf` | **no textures** (uses base colours); 23 meshes; raw ~3068 units |
| `big-ship/` | `scene.gltf` | **no textures**; 62 meshes; raw ~5159 units |

> **Model gotchas:** scales and orientations vary wildly per model. Scale is tuned per
> object in `main.cpp`. Orientation is corrected with a per-model `modelYaw` offset (see §8).

---

## 8. Vehicles (jet-ski / yacht / big-ship)

Defined in `main.cpp`. A `Vehicle` struct:
```cpp
struct Vehicle { Model* model; glm::vec3 pos; float heading; float speed;
                 float scale; float yOffset; float modelYaw; };
```
- Placed at **random positions** in the bay, random heading. Each has its own speed/scale.
- **Movement** (in the fixed-timestep loop): advance along `heading`; if past
  `kBayBound = 430`, steer heading toward origin to stay on the water.
- **`P` key toggles movement** on/off (`vehiclesMoving`, debounced). Prints state.
- **`modelYaw`** corrects each model's authored-forward axis vs travel direction:
  - jet-ski `-π/2` (it faced 90° right), yacht `+π` (reversed), big-ship `0` (correct).
  - **If a vehicle still faces wrong after a model change, this is the field to adjust.**

Current config (the `vehicles` vector):
```
jetski : speed 16, scale 120,  yOffset 1.5, modelYaw -π/2
yacht  : speed  8, scale 0.03, yOffset 0.0, modelYaw  +π
bigShip: speed  5, scale 0.02, yOffset 0.0, modelYaw   0
```

> **Not yet done:** vehicles sit at a **fixed Y** — they do **not** bob/tilt with the
> waves (that's buoyancy, see §11). They also don't yet create wakes.

---

## 9. Coastal Cliff Ring & Terrain

### Cliff ring (active — in `main.cpp`)
8 instances of `coastal_cliff_04` arranged around the bay. Edge pieces at `kEdge = 470`,
corners at `kCorner = 470` (just inside the ±512 water edge so land **covers the water tile
edge** — hides the seam). Each rotated to face inward via `faceIn(px,pz) = atan2(-px,-pz) +
kFaceOffset`. `kFaceOffset = 0` (flip to π if a future cliff model faces outward).
Base scale `s = 9` (~780 m wide), stretched taller in Y (`s*1.5–1.7`).

### Procedural terrain (`src/graphics/Terrain.{h,cpp}` + `terrain.vert/frag`) — CURRENTLY UNUSED
A noise-displaced grid (FBM + radial island falloff) that makes procedural mountains. The
fragment shader supports **triplanar PBR** (rock/grass/snow, blended by slope/height) with a
procedural fallback. It was replaced by the real cliff models (procedural shapes read as
blobs), but is kept for reuse. `terrain/rock_diff.jpg` + `rock_nor.jpg` exist; grass/snow
do not (would auto-activate if added). `RockGenerator.{h,cpp}` similarly makes a procedural
boulder mesh and is unused but kept.

---

## 10. Water Interaction Already Present

- **Rain** (`water/RainSystem`): falling streaks, central splash jets, and **expanding
  ripple ring** clusters rendered in `standard.frag` (SSBO of ripple centres+age, binding 4).
  Ripples fade via `smoothstep` tail-off (tuned to not "pop" off).
- **Interactive disturbance** (`ocean/GPUDisturbance`): press **C** to fire a wave ~40 m in
  front of the camera. A GPU height field that propagates and feeds the water normals. **This
  system is the natural basis for boat wakes** (see §11).
- **CPU water grid** (`water/WaterSimulation`): a separate physics grid driving rain ripples.

---

## 11. Suggested Next Steps (roadmap)

The originally-requested end goal is **dynamic water ↔ object interaction**. With the model
system and `GPUDisturbance` in place, the natural next features are:

1. **Buoyancy (water → objects)** — sample the FFT/ocean height under each vehicle and set
   its Y to float; tilt it to the local wave normal so it bobs and rolls. Needs a
   `sampleOceanHeight(x,z)` helper (read back displacement, or recompute from the same data).
2. **Wakes (objects → water)** — a moving vehicle injects disturbances along its path via the
   existing `GPUDisturbance::disturb()` (reuse the C-key mechanism per frame at the hull).
3. **Texturing the yacht/ship** — they shipped with no textures; either find textured models
   or author simple materials. Add **embedded-`.glb`-texture** support in `Model.cpp` for
   self-contained Sketchfab exports.
4. **Polish:** seal the last corner gaps in the cliff ring; optional water-edge fade.

---

## 12. Known Issues / Caveats

- **Uncommitted work** — the entire scene/model/vehicle system is not committed (see §2).
- **`.gitignore`** — now fixed (was broken with quoted entries). The 774 stale `build/`
  files are still tracked in the index from before the fix; untrack with
  `git rm -r --cached build/` (see §2).
- **Water tile edge** is hidden behind the cliff ring, but tiny **corner gaps** remain
  visible from a top-down view.
- **Vehicles don't float/bob** yet (fixed Y) and have **no wakes** (next feature — §11).
- **Yacht & big-ship are untextured** (flat base-colour) — a model limitation.
- **Embedded-texture `.glb`** files won't show textures (loader handles external URIs only).
- `assets/models/test_avocado.glb` is an **unused loader test asset** (Khronos sample) — safe
  to delete; nothing references it.
- A faint **FFT seam** can appear on the water from high/far camera angles (mostly hidden by
  cliffs and not visible at eye level).
- **Windows 11:** assessed as buildable as-is after FIX-1/FIX-2 (see §3a for the full,
  pinpointed assessment of remaining low-risk items).

---

## 13. Controls (key bindings)

| Key | Action |
|---|---|
| W / A / S / D | Move camera (speed `MovementSpeed = 70` m/s, in `Camera.cpp`) |
| Space / Left-Shift | Move up / down |
| Mouse | Look around |
| **P** | **Toggle vehicle movement (stop / move)** |
| C | Fire interactive wave disturbance ~40 m ahead |
| T or V | Toggle wireframe (ocean) |
| K / L | Wind speed +/- 0.5 (range 5–10) |
| N / M | Choppiness up / down |
| Up / Down | Wave height scale +/- |
| Left / Right | Time scale -/+ |
| R | Start recording frames to `frames/*.png` (600 frames) |
| Esc | Exit |

### Recording → video
Frames save to `frames/frame_%d.png` (start at 0), captured at the 60 FPS fixed step:
```bash
ffmpeg -framerate 60 -start_number 0 -i frames/frame_%d.png -c:v libx264 -pix_fmt yuv420p -crf 18 output.mp4
```

---

## 14. Conventions (match these when adding code)

- **Style:** RAII classes (ctor builds GL objects, dtor deletes); `draw(const Shader&, ...)`
  methods; `m_`-prefixed members; `k`-prefixed constants; comments explain *why*, not *what*.
- **Assets:** loaded at runtime with **relative `../assets/...` paths** — run from the build dir.
- **New source files:** add the `.cpp` to the `SOURCES` list in `CMakeLists.txt`.
- **Single-header libs:** vendored under `external/` (include path is `external/`), with the
  `#define ..._IMPLEMENTATION` placed in exactly one `.cpp` (stb impls are in `main.cpp`;
  `CGLTF_IMPLEMENTATION` is in `Model.cpp`).
- **Shaders:** GLSL `#version 460 core`. Texture-sampled colour maps need **gamma encoding**
  at the end of the fragment shader; data maps stay linear.
- **Verify visually:** the project is graphical — when changing visuals, build and capture a
  screenshot (on Linux: launch on the X display, find the window with `xwininfo -root -tree |
  grep "Real-Time Water Simulator"`, capture with `xwd -id <id>` and convert via `ffmpeg`).

---

## 15. Quick-Start for an AI Agent Continuing This Work

1. Read this file end-to-end, then skim `src/main.cpp` (scene setup + render loop) and
   `src/ocean/GPUFFTOcean.cpp` (the FFT dispatch).
2. **Commit the existing tree first** (and fix `.gitignore`) — see §2.
3. Build in `build_linux/` and run to see the current scene (§3).
4. For the next feature, start with **buoyancy** (§11.1) — it's the highest-impact piece of
   the originally-intended "dynamic water interaction," and all prerequisites (model system,
   ocean height data) already exist.
5. When touching the FFT, re-read §6 carefully — the data layout, normalization, ping-pong
   parity, and the UV-tiling gotcha are easy to break.
