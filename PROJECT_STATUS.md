# RealWaterSimulator — Project Status & Developer Handoff

> **Purpose of this document:** a single, complete snapshot of the repository so that
> anyone (or any AI agent) can clone it and continue implementing features, fixes, and
> patches without re-discovering how things work. It documents the architecture, the
> exact current state, every subsystem, the known issues, and the conventions to follow.
>
> **Last updated:** reflects branch `clean-deploy-v1` — the current deployment
> branch. The tree is **clean / fully committed** (see [§2 Git State](#2-git-state)).

---

## 1. What This Project Is

A real-time, GPU-accelerated **ocean / water simulator** in **C++17 + OpenGL 4.6**, built
on GLFW + GLAD + GLM + STB + cgltf + Dear ImGui (all vendored). It started as an FFT ocean and has grown into
a small scene: a crisp FFT sea inside a ring of photoreal coastal cliffs, with GPU rain,
interactive ripples, loadable glTF models, and moving vehicles (jet-ski / yacht / big-ship)
that float, steer, and leave wakes.

**Runs on Linux and Windows.** Same OpenGL 4.6 + C++17 code; CMake handles both. Developed
and tested on Linux (NVIDIA RTX 3090, driver 535, OpenGL 4.6). See `LINUX_BUILD.md` for
the Linux dependency/build guide.

### Current scene (what you see when you run it)
- A bounded **512-resolution FFT ocean** (1024 m patch) fixed in world space.
- A **ring of 8 coastal-cliff instances** (real Poly Haven scan) enclosing the water like a lagoon.
- A **foreground rock** (Poly Haven marble cliff) floating in the bay (toggleable in the UI).
- **Three vehicles** (jet-ski, yacht, big-ship) at random positions, cruising and turning at the
  bay edge with boids-style steering, collision avoidance, buoyancy bob, and per-boat wakes.
- **Rain** (fully GPU) with splashes and expanding ripple rings on the water.
- Full **PBR-ish water shading** (reflection + refraction + subsurface scatter + sun glitter + foam).

---

## 2. Git State

- **Current branch:** `clean-deploy-v1` (the deployment branch this doc tracks)
- **Remote:** `origin` → `git@github.com:ProGitEren/RealWaterSimulatorOpenGL.git`
- **Tree status:** **clean** — all feature work is committed and pushed.
- **Other branches:** `main`; `deploy-rt` (adds analytic ray-traced reflections, see below);
  `eren-dev-stockham-fft-pbr` (older FFT/PBR line of work).

The entire scene — GPU FFT ocean, GPU rain, glTF model loader, coastal-cliff ring, the three
vehicles with buoyancy + per-boat wakes, the full ImGui control panel — is **committed**. There
is no at-risk uncommitted work; just build (§3) and run.

### Ray tracing lives on a separate branch
`clean-deploy-v1` deliberately has **no ray tracing**. Analytic ray-traced reflections (boats as
OBBs + the rock as an ellipsoid, intersected in the water fragment shader) live only on the
**`deploy-rt`** branch. Keep them there unless a future merge is explicitly requested.

### `.gitignore`
Correct and in effect — `build*/`, object/binary files, `frames/`, `*.mp4`, and IDE/OS cruft are
ignored. Build into `build_linux/` (Linux) or `build_win/` (Windows); neither is tracked.

---

## 3. Build & Run

### Linux (primary)
```bash
# one-time deps (Ubuntu/Debian) — see LINUX_BUILD.md for other distros
sudo apt-get install -y build-essential cmake libgl1-mesa-dev libglu1-mesa-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev xorg-dev

# build into build_linux/ (git-ignored; keep Linux/Windows build dirs separate)
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

> Configure fresh into `build_win/` (git-ignored). Don't share a build directory between
> machines — a configure bakes in absolute paths and a specific CMake/compiler version.

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
│   ├── textures/skybox/        Cubemap faces (sky_1/2/3, environment_1 variants)
│   └── models/                 glTF models (rocks, cliff, jet-ski, yacht, big-ship)
├── external/                   Vendored: glad, glfw, glm, stb, cgltf, imgui (Dear ImGui)
└── src/
    ├── main.cpp                Entry point: scene setup, render loop, input, UI, vehicles
    ├── core/                   Window (GLFW), Camera (fly cam)
    ├── graphics/               Shader, Mesh, Model (glTF), Texture, RockGenerator
    ├── ocean/                  GPUFFTOcean (FFT + CPU height readback), OceanMesh, GPUDisturbance
    └── water/                  GPURain (fully GPU rain), BoatPhysics (6-DOF buoyancy)
```
These are exactly the `.cpp` files in `CMakeLists.txt`'s `SOURCES` list. Object/cliff/vehicle
shading uses `object.{vert,frag}`; rain uses `rain_update.comp` + `rain_gpu.*` + `rain_splash.*`;
the water surface is `standard.{vert,frag}`.

> **No foam particle system in this build.** The white churn around boats comes from the water
> shader's FFT-Jacobian whitecaps plus the disturbance ripples — the old `FoamMap` / `WakeFoam`
> sprite systems and the procedural `Terrain` system have been **removed** (the real cliff scans
> replaced terrain; the shader foam replaced the sprites). `RockGenerator` remains only as a
> procedural fallback if the rock glTF fails to load.

---

## 5. Architecture & Data Flow

### Render loop (in `main.cpp::main()`), per frame:
1. **Input** — keyboard/mouse; toggles (wireframe, vehicle pause), live ocean tuning.
2. **Fixed-timestep physics** (`kFixedDt = 1/60`, accumulator pattern):
   - `ocean.update()` — runs the entire GPU FFT pipeline (see §6)
   - `disturbance.update()` — propagate interactive (C-key) + boat-wake waves
   - **vehicle navigation** — advance each vehicle along its heading; boids-style steering
     (boundary + separation + rock avoidance) + hard collision clamps; inject the stern wake
3. **`rainSystem.update()`** once per frame (outside the substep loop) — one GPU compute dispatch
   advances every drop + spawns throttled ripple rings (see §10).
4. **`ocean.readbackDisplacement()`** — async PBO copy of GPU wave heights to the CPU.
5. **`BoatPhysics::step()`** per vehicle — solve heave/pitch/roll from the sampled heights.
6. **Render**:
   - Bind ocean textures to units 1/2/3, skybox cubemap to unit 0.
   - Draw **ocean** (`standard.vert/frag`) — displaced FFT grid, full water shading.
   - Draw **solid objects** (`object.vert/frag`): rock (toggleable), cliff ring, vehicles.
   - Draw **skybox** (depth-trick), then **rain** (streaks + splashes), then the **ImGui** panel.
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
**Object shaders must gamma-encode (`pow(c, 1/2.2)`) at the end** or everything
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

Defined in `main.cpp`. A `Vehicle` struct (key fields):
```cpp
struct Vehicle { Model* model; glm::vec3 pos; float heading; float speed;
                 float scale; float yOffset; float modelYaw; float wakeWidth;
                 float hullLength; float wakeScale; /* + turnVel, bankAngle, BoatPhysics phys */ };
```
- Placed at **random positions** in the bay, random heading. Each has its own speed/scale.
- **Navigation** (fixed-timestep loop): advance along `heading`, then steer with a simplified
  **boids** model — boundary return (past `0.7 × kSoftRadius`, `kSoftRadius = 300`), separation
  from the other boats, and **central-rock avoidance** — turned into a rate-limited heading change
  (`boatTurnRate ≈ 0.45 rad/s`, sharper near the edge) plus a gentle sinusoidal wander.
- **Hard collision net** (after steering): clamps guarantee **no boat-boat overlap** and **no
  phasing** through the cliffs (`kHullLimit = 400`) or the rock (`kRockHard = 45`). The turn
  radii/keep-distances are tuned so boats never get close enough to the cliffs to need it.
- **Banking:** boats lean into turns (roll ∝ yaw-rate × speed), eased smoothly.
- **`P` key toggles movement** on/off (`vehiclesMoving`, debounced). Prints state.
- **`modelYaw`** corrects each model's authored-forward axis vs travel direction:
  jet-ski `-π/2`, yacht `+π`, big-ship `0`. **Adjust this if a swapped model faces wrong.**
- **`wakeWidth`** = hull half-width (m) → drives the buoyancy beam AND the wake footprint (§ below).
- **`wakeScale`** = per-boat wake amplitude multiplier (ship = 1.0 reference) — see Wakes below.

Current config (the `vehicles` vector):
```
jetski : speed 16, scale 120,  yOffset 1.5, modelYaw -π/2, wakeWidth 4,  hullLen 5,   wakeScale 0.22
yacht  : speed  8, scale 0.03, yOffset 0.0, modelYaw  +π,  wakeWidth 14, hullLen 90,  wakeScale 0.70
bigShip: speed  5, scale 0.02, yOffset 0.0, modelYaw   0,  wakeWidth 20, hullLen 110, wakeScale 1.00
```

### Buoyancy (vehicles float, bob, and tilt) — DONE
**Force-based 6-DOF buoyancy** (`src/water/BoatPhysics.{h,cpp}`). Each vehicle owns a
`BoatPhysics` that solves heave + pitch + roll from per-facet Archimedes forces:
- The hull is a grid of facets (facetsX × facetsZ) in the boat's local frame.
- Each frame: transform facets to world, look up `sampleSurfaceHeight` under each, and for
  submerged facets add an up-force ∝ submerged depth; the **sum** drives heave, and the
  **lever-arm torques** (fore/aft → pitch, port/starboard → roll) drive rotation. Damped
  integration (`linearDamp`/`angularDamp`) keeps it stable; tilt is clamped to ±0.6 rad.
- **Navigation (XZ + yaw) stays scripted**; only the vertical + tilt are dynamic. So boats
  follow their path but physically bob/pitch/roll on the swell.
- A long ship's facets span many wavelengths, so short chop averages out — big ships ride
  smoothly, small ones bob lively. ImGui: float strength, heave damping, tilt damping.
- **Critical detail:** sampling uses `ocean.sampleSurfaceHeight()` (choppiness-correct —
  inverts the FFT horizontal displacement), NOT `sampleOceanHeight`, so hulls sit on the wave
  they're actually on even as waves grow — see §6 and §12.

> History: this replaced an earlier kinematic snap-to-surface (single point → multi-point +
> low-pass). The force model gives emergent heave/pitch/roll. Tunable per vehicle.

### Wakes — DONE (per-boat scaled ripple wake)
Each fixed step a moving vehicle injects a ripple into the `GPUDisturbance` height field. Three
things make the wake read correctly per boat from a **single** `wakeStrength` ImGui slider:
1. **Injected behind the transom, at the visible hull centre.** The injection point is the boat's
   AABB centre (`Model::localCenter()`, so meshes that sit off their glTF origin don't throw the
   wake to one side) shifted back to the stern **plus** ~0.6× the footprint radius, so the swell
   **trails** the boat as a separation wake instead of welling up underneath and "submerging" it.
2. **Footprint scales with the hull's beam.** `GPUDisturbance::disturb()` takes a per-call
   `sigmaTexels`; each boat's is `clamp(wakeWidth / texelM, 1.5, 5.0)` (texel ≈ 4 m) → a tight
   ~6 m ripple for the jet-ski, a broad ~20 m swell for the ship.
3. **Amplitude scales per boat.** `amplitude = wakeStrength × wakeScale × min(1, speed/12)`. The
   ship (`wakeScale 1.0`) keeps the reference strength; the jet-ski (`0.22`) drops a small ripple
   instead of being engulfed.

> **No foam particle system.** The white churn is the water shader's FFT-Jacobian whitecaps +
> the disturbance ripples (the old `FoamMap` / `WakeFoam` systems were removed).

---

## 9. Coastal Cliff Ring

### Cliff ring (active — in `main.cpp`)
8 instances of `coastal_cliff_04` arranged around the bay as a **uniform ring**: the 4 edge
pieces sit at radius `kEdge = 470` on the axes, and the 4 corner pieces at the **same radius**
(`kCorner = kEdge × 0.7071 ≈ 332` axial → radius 470). Making the corners share the edge radius
closed the diagonal **void** that used to show between water and cliffs (the corners previously
bulged out to radius ~665). All sit just inside the ±512 water edge so land **covers the water
tile edge** (hides the seam). Each is rotated to face inward via `faceIn(px,pz) = atan2(-px,-pz)`.
Base scale `s = 9` (~780 m wide), stretched taller in Y (`s × 1.5 − 1.7`). The heavy overlap of
8 × 780 m pieces around a ~470 m ring gives a continuous coastline.

### Procedural terrain — REMOVED
An earlier noise-displaced terrain (`Terrain` + `terrain.vert/frag`, FBM + island falloff +
triplanar PBR) was **removed** — the real Poly Haven cliff scans look far better (procedural
shapes read as blobs). `RockGenerator.{h,cpp}` remains **only** as a procedural fallback used if
the foreground rock's glTF fails to load.

---

## 10. Water Interaction (implemented)

- **Rain** (`water/GPURain`, **fully GPU**): up to 120 K drops live in an SSBO, advanced by one
  `rain_update.comp` compute dispatch/frame; streaks + splash columns draw **attributelessly**
  from the SSBO (2 instanced draws, no CPU per-drop loop, no vertex upload). **Expanding ripple
  rings** are the only CPU piece — a throttled spawner (~`480 / lifetime` rings/s) feeds a 512-cap
  SSBO (binding 4) that `standard.frag` reads to perturb the water normal. See §13/TECH-OVERVIEW.
- **Interactive disturbance** (`ocean/GPUDisturbance`): press **C** to fire a wave ~40 m in
  front of the camera. A GPU wave-equation height field that propagates and feeds the water
  normals; also driven per-frame by the vehicle stern wakes.
- **Ocean → object (buoyancy):** vehicles float, bob, and tilt to the surface — see §8.
- **Object → ocean (wakes):** moving vehicles inject per-boat-scaled ripples (`GPUDisturbance`) —
  see §8. There is no foam particle system; foam is the shader's FFT-Jacobian whitecaps.

### Ocean height sampling (CPU readback) — `GPUFFTOcean`
The displacement texture is read back to the CPU once per render frame via a **double-buffered
PBO** (`readbackDisplacement()`). A synchronous `glGetTexImage` stalls on the in-flight FFT
(~15 ms → ~55 fps); the PBO ping-pong copies async and maps *last* frame's buffer, so the
height data is 1 frame stale (negligible for buoyancy) with no stall. Sampling API:
- `sampleDisplacement(x,z)` → bilinear `{dx, height, dz}` at a world XZ.
- `sampleOceanHeight/Normal(x,z)` → naive height/slope **at** that XZ.
- `sampleSurfaceHeight/Normal(x,z)` → **choppiness-correct** (inverts horizontal displacement
  via fixed-point iteration). **Buoyancy uses these** so hulls track the right wave.

---

## 11. Suggested Next Steps (roadmap)

The core **dynamic water ↔ object interaction** is implemented (force-based buoyancy + per-boat
ripple wakes) and the scene is closed (uniform cliff ring, no corner void). Remaining polish /
next features:

1. **Texturing the yacht/ship** — they shipped with no textures (flat base colours). Find
   textured models or author materials. Add **embedded-`.glb`-texture** support in `Model.cpp`
   for self-contained Sketchfab exports (loader currently handles external URIs only).
2. **Foam** — there is no foam particle system; the look is shader whitecaps + disturbance
   ripples. A dedicated bow-spray / persistent trail (e.g. a world-space foam coverage texture)
   could be added back if a richer wake is wanted.
3. **Merge ray tracing** — analytic ray-traced reflections already exist on `deploy-rt`; a future
   merge could bring them to `clean-deploy-v1` (kept separate for now).
4. **Buoyancy realism** — already force-based 6-DOF; could add more inertia/lag tuning per hull.

---

## 12. Known Issues / Caveats

- **All work is committed** on `clean-deploy-v1` (tree is clean — see §2).
- **Cliff ring is closed** — the corner void was fixed by making the corner pieces share the edge
  radius (§9). No remaining diagonal gaps at eye level or elevated views.
- **Buoyancy is force-based 6-DOF** (heave/pitch/roll from per-facet Archimedes forces, §8).
  It is choppiness-corrected so hulls track the right wave even as waves grow.
- **Yacht & big-ship are untextured** (flat base-colour) — a model limitation.
- **Embedded-texture `.glb`** files won't show textures (loader handles external URIs only).
- A faint **FFT seam** can appear on the water from high/far camera angles (mostly hidden by
  cliffs and not visible at eye level).
- **Ray tracing is on `deploy-rt` only** — `clean-deploy-v1` intentionally has none (§2).
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

### On-screen ImGui panel ("Controls" window)
A Dear ImGui panel shows **FPS + ocean readback ms** and live sliders grouped into collapsing
sections (input gated by `io.WantCaptureMouse/Keyboard` so the panel doesn't drive the camera):
- **Rain** — spawn rate (0–50), fall speed, drop size, opacity, splash height, ripple lifetime
  (0.5–5 s), ring speed, ring strength, C-key splash strength.
- **Water** — wind speed, wave height, choppiness, horizontal displace, time scale, sea maturity,
  overall amplitude, mid-wave detail, depth-tint falloff, deep/shallow colours, boat wake strength.
- **Lighting** — reflection strength, horizon/scatter/sun colours, sun azimuth/elevation, sun
  glint/glitter, HDR exposure, and a **sky selector** (`sky_1/2/3`, `environment_1`).
- **Vehicles** — a per-boat **speed** slider for each of the three vehicles.
- **Scene** — a **"Central rock"** checkbox to show/hide the lone rock (also frees its collision
  space when off, so boats pass through it).

> The capillary slider and the below-horizon-blend slider were intentionally removed (the former
> was a no-op at this grid resolution; the latter is now a fixed 0.7 in `standard.frag`).

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
   `src/ocean/GPUFFTOcean.cpp` (the FFT dispatch + CPU height readback).
2. Build in `build_linux/` and run to see the current scene (§3). Note the new deps: **Dear
   ImGui** and `glm/gtc/quaternion.hpp` are used — a fresh `cmake` reconfigure picks them up.
3. The core interaction is **done**: force-based buoyancy (choppiness-correct, §8) and per-boat
   scaled ripple wakes (§8). Rain is fully GPU (§10). Next features are in §11 (texturing the
   ship/yacht, optional foam/bow-spray, merging the `deploy-rt` ray tracing).
4. **Buoyancy gotcha:** always sample `ocean.sampleSurfaceHeight/Normal` (choppiness-correct),
   never `sampleOceanHeight` directly — the latter makes hulls drift beside the wave (§6/§8).
5. When touching the FFT, re-read §6 carefully — the data layout, normalization, ping-pong
   parity, and the UV-tiling gotcha are easy to break.
