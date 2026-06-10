# RealWaterSimulator — Technical Overview (A→Z)

A complete, presentation-ready breakdown of **how every part of the project works**, the exact
files to look at, the important parameters, and the key numbers (grid sizes, resolutions,
performance). Written so the team can answer any "how does X work?" question with confidence.

- **Language / API:** C++17, OpenGL 4.6 (core profile, compute shaders + SSBO + image load/store)
- **Libraries (all vendored in `external/`):** GLFW (window/input), GLAD (GL loader), GLM (math),
  STB (image load/write), cgltf (glTF parser), Dear ImGui (UI panel)
- **Platforms:** Linux (primary) + Windows 11. Build with CMake.
- **Reference GPU:** NVIDIA RTX 3090, ~112 FPS at the default settings.

---

## 0. The big picture — folder map (where to look for each part)

```
RealWaterSimulatorOpenGL/
├── src/
│   ├── main.cpp ........................ ENTRY POINT: scene setup + render loop + input + UI
│   ├── core/
│   │   ├── Window.{h,cpp} ............... GLFW window + OpenGL 4.6 context + GLAD init
│   │   └── Camera.{h,cpp} .............. fly camera (view matrix, WASD, mouse look)
│   ├── graphics/
│   │   ├── Shader.{h,cpp} ............... compile/link GLSL (vert+frag OR compute); setUniform helpers
│   │   ├── Mesh.{h,cpp} ................. VAO/VBO/EBO wrapper (pos/normal/uv vertices)
│   │   ├── Model.{h,cpp} ................ glTF LOADER (cgltf): nodes→meshes+materials, transform, draw
│   │   ├── Texture.{h,cpp} .............. stb_image 2D texture loader (sRGB-aware)
│   │   └── RockGenerator.{h,cpp} ....... procedural rock mesh (noise-displaced icosphere) — fallback
│   ├── ocean/
│   │   ├── GPUFFTOcean.{h,cpp} .......... THE FFT OCEAN: spectrum→FFT→displacement→normals + CPU readback
│   │   ├── OceanMesh.{h,cpp} ............ the 1024×1024 grid the ocean is drawn on
│   │   └── GPUDisturbance.{h,cpp} ....... interactive wave ripples (C-key + boat wakes), GPU wave equation
│   └── water/
│       ├── GPURain.{h,cpp} ............. GPU rain: drops in an SSBO, compute update, instanced draw
│       └── BoatPhysics.{h,cpp} .......... 6-DOF buoyancy (heave/pitch/roll from wave sampling)
├── assets/
│   ├── shaders/ ........................ all GLSL (see per-section lists below)
│   ├── textures/skybox/ ................ 4 cubemap sets: sky_1, sky_2, sky_3, environment_1
│   └── models/ ......................... glTF models: rock, cliff, jet-ski, yacht, big-ship (~296 MB)
├── external/ ........................... glad, glfw, glm, stb, cgltf, imgui (vendored)
└── CMakeLists.txt ...................... cross-platform build
```

### The render loop, one frame (in `main.cpp`)
1. **Input** — keyboard/mouse; left-click = first-person capture; ImGui panel.
2. **Fixed-timestep physics** (`kFixedDt = 1/60`, accumulator pattern): per substep
   `ocean.update()` (full GPU FFT) → `disturbance.update()` → vehicle navigation/steering + stern
   wake injection.
3. **`rainSystem.update()`** — once per frame (outside the substep loop): one GPU compute dispatch
   advances all drops + spawns throttled ripple rings.
4. **`ocean.readbackDisplacement()`** — copy the GPU wave heights to the CPU (once/frame).
5. **`BoatPhysics::step()`** per vehicle — solve heave/pitch/roll from the sampled heights.
6. **Render:** ocean → solid objects (rock, cliffs, vehicles) → skybox → rain (streaks+splashes) → ImGui.

### Texture-unit map (how data reaches the shaders)
| Unit | Bound texture | Used by |
|---|---|---|
| 0 | skybox cubemap | ocean reflection, object ambient, skybox |
| 1 | ocean **displacement** map (RGBA32F) | `standard.vert` (moves vertices) |
| 2 | ocean **normal + foam** map (RGBA16F) | `standard.frag` (lighting + whitecaps) |
| 3 | **disturbance** height map (R32F) | `standard.vert/frag` (C-key + boat wakes) |
| 4 | SSBO binding (rain ripples) | `standard.frag` (ripple rings) |
| 4–7 | object material maps (albedo/normal/rough/AO) | `object.frag` |

---

## 1. FFT WATER SIMULATION (the core)

**What it is:** an **oceanographic FFT ocean** — the surface is the sum of thousands of sine
waves whose amplitudes come from a real wind-wave **spectrum**, evolved in time and turned into a
height/displacement field by an **Inverse FFT** every frame on the GPU.

### Where to look
- **C++ driver:** `src/ocean/GPUFFTOcean.{h,cpp}` — owns all textures, runs the compute passes,
  reads the result back to the CPU.
- **Compute shaders** (`assets/shaders/`), in pipeline order:
  1. `fft_initial_spectrum.comp` — builds the **initial spectrum** `h0(k)` (run once, or on a
     spectrum-param change).
  2. `fft_phase.comp` — advances each wave's **phase** by its dispersion frequency (per frame).
  3. `fft_spectrum.comp` — combines `h0` + phase into the **current spectrum** `h(k,t)`, plus the
     horizontal-displacement spectra `dx`, `dz` (choppiness).
  4. `fft_horizontal.comp` + `fft_vertical.comp` — the **Stockham FFT** (2D IFFT = rows then cols).
  5. `fft_displacement.comp` — packs the spatial result into the **displacement texture**
     `(dx, height, dz)`.
  6. `fft_normal.comp` — computes **surface normals** + **foam** (Jacobian) from the displacement.
- **Mesh it's drawn on:** `src/ocean/OceanMesh.{h,cpp}`. **Vertex displacement:**
  `assets/shaders/standard.vert`.

### How it works, step by step
1. **Spectrum (`fft_initial_spectrum.comp`).** Uses the **IFREMER / Elfouhaily unified
   directional spectrum** (same model as the reference repo *achalpandeyy/OceanFFT*). For each 2D
   wave-vector `k` it computes an amplitude from wind speed + a directional spreading term, then
   multiplies by Gaussian random noise → `h0(k)`. **Key parameters (uniforms):**
   - `windSpeed` (U10) — drives the whole sea state. Bigger wind = larger, longer waves.
   - `windDirection` — waves travel mostly along the wind.
   - `OMEGA` (= **sea maturity** slider, 0.84–5) — inverse wave age; lower = fully developed swell.
   - `SPECTRUM_BOOST` (= **overall amplitude** slider, 50–3000) — global wave-energy multiplier.
2. **Phase (`fft_phase.comp`).** Each wave oscillates at frequency `ω(k)` from the **dispersion
   relation** `ω = sqrt(g·k·(1 + (k/Km)²))` (deep-water gravity-capillary). Phase is advanced by
   `ω · dt · timeScale` each frame → this is what makes the ocean **move**.
3. **Current spectrum (`fft_spectrum.comp`).** `h(k,t) = h0(k)·e^{iωt} + conj(h0(-k))·e^{-iωt}`
   (keeps the field real). Also builds `dx`, `dz` = the **horizontal displacement** spectra scaled
   by `choppiness` — this is what sharpens round swells into **pointed crests**.
4. **Inverse FFT (`fft_horizontal.comp` → `fft_vertical.comp`).** A **Stockham radix-2 FFT**: each
   dispatch does one butterfly stage, **ping-ponging between two textures** across `log2(N)`
   passes, for rows then columns. (We use Stockham because the older single-workgroup FFT was
   capped at 256 by shared-memory size; Stockham removes that cap → **true 512**.)
   - **Data layout (3 complex sequences packed across 2 textures):**
     Texture A (RGBA32F): `xy` = dx, `zw` = height(h). Texture B (RG32F): `xy` = dz.
5. **Displacement (`fft_displacement.comp`).** Normalizes (`÷N`) and applies `heightScale`
   (vertical) + `horizontalScale` (sideways) → writes the **displacement texture**
   `(dx, height, dz)` in metres.
6. **Normals + foam (`fft_normal.comp`).** Reconstructs each vertex's displaced world position,
   takes finite differences of the 4 neighbours → **normal**. Computes the **Jacobian** of the
   horizontal displacement; where `J < 0` the surface is **folding over** (a breaking crest) →
   that's the **whitecap foam** (stored in the normal map's alpha channel).
7. **Drawing it (`standard.vert`).** The 1024² grid samples the displacement texture per-vertex
   (`OceanUV = worldXZ/oceanSize + 0.5`, `GL_REPEAT` tiling) and moves each vertex by
   `(dx, height, dz)`. The fragment shader (see §6) lights it.

### Key numbers
| Quantity | Value | Where | Why |
|---|---|---|---|
| **FFT resolution N** | **512 × 512** | `main.cpp` `kOceanResolution` | wave detail; Stockham allows up to 1024 |
| **Mesh grid** | **1024 × 1024 verts** | `main.cpp` `kOceanMeshRes` | render geometry density |
| **Ocean patch size** | **1024 m** (1 m/vertex) | `kOceanMeshRes × kOceanMeshTile` | 1 m spacing resolves the fine FFT waves |
| **FFT passes/frame** | 2 × log2(512) = **18** + spectrum/phase/disp/normal | `GPUFFTOcean::update` | the per-frame compute cost |
| **Compute local size** | 16×16 (spectrum/disp/normal), 256 (FFT butterfly) | shaders | GPU occupancy |
| **FFT textures** | 11 × 512² (mostly RGBA32F ≈ 4 MB each) | `GPUFFTOcean` | ~40 MB VRAM for the sim |

### The runtime parameters (ImGui → what each does)
| Parameter | Range | Effect |
|---|---|---|
| **wind speed** | 2–30 m/s | overall sea state — calm ripples → big storm swell (rebuilds spectrum) |
| **wind direction** | 0–360° | wave travel direction (also drives rain slant) |
| **wave height** (`heightScale`) | 0–6 | vertical amplitude multiplier |
| **choppiness** | 0–3 | horizontal pinch → pointed vs. rounded crests |
| **horizontal displace** (`horizontalScale`) | 0–1.2 (default **0.9**) | strength of the sideways crest pinch |
| **time scale** | 0–4 (default 2) | how fast the ocean animates |
| **sea maturity** (`OMEGA`) | 0.84–5 | young wind-sea vs. developed swell (rebuilds spectrum) |
| **overall amplitude** (`SPECTRUM_BOOST`) | 50–3000 | global wave energy (rebuilds spectrum) |

> "Rebuilds spectrum" = changing it re-runs `fft_initial_spectrum.comp` once (cheap).

### How floating objects read the wave height (CPU readback)
`GPUFFTOcean::readbackDisplacement()` copies the displacement texture to the CPU **once per
frame** using a **double-buffered Pixel Pack Buffer (PBO)**: it kicks off an async copy into PBO
`A` and reads the already-finished PBO `B` from last frame. A synchronous read would stall on the
in-flight FFT (~15 ms → ~55 FPS); the async PBO ping-pong keeps it ~free, the data is just 1 frame
stale (irrelevant for buoyancy). `sampleSurfaceHeight(x,z)` then bilinearly samples that CPU
mirror — with a **fixed-point inverse** of the horizontal displacement so boats sit *on* the wave,
not beside it.

---

## 2. RAIN (fully GPU-driven)

**What it is:** a **GPU particle system** — every raindrop lives in a GPU buffer (SSBO) and is
advanced by a **compute shader**; drops and splash columns are drawn **attributeless / instanced**
(the vertex shaders read the SSBO directly), so there is **no CPU per-drop loop and no per-frame
vertex upload**. Expanding **ripple rings** are still drawn into the water surface.

### Where to look
- **C++:** `src/water/GPURain.{h,cpp}` (owns the drop SSBO, dispatches the compute update, does the
  instanced draws).
- **Shaders:** `assets/shaders/rain_update.comp` (drop physics on the GPU),
  `rain_gpu.vert/frag` (streaks), `rain_splash.vert/frag` (splash columns).
- The **ripple rings** are drawn by the **water** shader `standard.frag` (lines ~83–122), fed by an
  SSBO of ripple centres+ages packed in `main.cpp::uploadRipples`.

### How it works
1. **Drop buffer.** A single SSBO holds up to **120,000 drops** (`2×vec4` each:
   `pos.xyz`+fall speed, and a splash timer + impact XZ + seed). Allocated once.
2. **GPU update (`rain_update.comp`).** Each frame, one compute dispatch advances **every** drop:
   fall by its speed, drift sideways by the wind, and on hitting the water (`y ≤ 0.5`) or drifting
   out of the 100 m disc → **respawn at the top** around the camera (random angle/radius via a GPU
   hash). On a water hit it also **records the impact XZ and starts a splash timer** in its own slot.
   No CPU loop touches the drops.
3. **Streak draw (`rain_gpu.vert`).** Attributeless: we draw `count × 2` vertices as `GL_LINES`;
   the vertex shader reads each drop from the SSBO and builds its streak (bright tip → faded tail,
   slanted along velocity). **One draw call, zero CPU vertex work.**
4. **Splash draw (`rain_splash.vert`).** A second instanced draw over the **same** SSBO renders a
   short **parabolic water column** at each drop whose splash timer is active (using the stored
   impact XZ). Inactive splashes emit a degenerate off-screen line (free).
5. **Ripple rings.** The only CPU-side piece: a **throttled spawner** in `GPURain::update` drops
   ~`480 / lifetime` rings per second around the camera into a `std::deque`, aged each frame and
   uploaded to the ripple SSBO (binding 4). `standard.frag` reads them and perturbs the water normal
   in **4 concentric rings** that expand (`uRingSpeed`) and fade over `uRippleLifetime`.

### Performance — why it's fast (important for the presentation)
- **Pure-GPU drops.** All drop motion is a single **compute dispatch**; rendering is **2 instanced
  draw calls** (streaks + splashes) reading the SSBO. **No CPU per-drop loop, no per-frame vertex
  buffer rebuild/upload.** Scales to tens of thousands of drops at ~zero CPU cost (the old CPU
  system rebuilt + re-uploaded a vertex array every frame and would choke the CPU at those counts).
- **Rain updates once per frame**, *outside* the fixed-timestep substep loop — drops are visual, so
  one dispatch over the whole frame's `dt` looks identical but avoids redundant dispatches on slow
  frames.
- **Per-pixel ripple early-out (`standard.frag`).** The ripple **rings are drawn by the water
  fragment shader**, looping over up to **512 ripples × 4 rings per fragment** (~1.2 B iterations/
  frame with the ocean full-screen). We **reject far ripples with a cheap squared-distance test
  *before* the `normalize()`/`sin()` and the inner ring loop**, reuse that `sqrt` for the direction,
  and skip ripples for fragments >350 m away. **Mathematically identical** (no visual change) — this
  is what unstuck weaker / Windows GPUs.
- **Ripple-budget throttling.** The ripple SSBO holds only **512** entries; a steady budget creates
  rings at `kRippleTarget(480) / lifetime`, so the field stays ≈ full at any setting and **cost is
  decoupled from rain intensity** — even at max spawn rate the ripple count (and shader cost) stays
  capped at 512.
- **Camera-local.** Drops only exist in a 100 m disc around the camera — never the whole 1024 m ocean.

### Key numbers / parameters
| Parameter | Default | Range | Effect |
|---|---|---|---|
| spawn rate | 50 | 0–500 | rain intensity (× ~24 = live drop count) |
| fall speed | 77 m/s | 20–140 | drop speed + streak length |
| drop size | 1.0 | 0.3–3 | streak length + thickness |
| opacity | 0.22 | 0–1 | streak transparency |
| splash height | 1.0 | 0–3 | splash-column height |
| ripple lifetime | 3 s | 0.5–5 | how long each ring lives |
| ring speed | 5 m/s | 0.5–15 | ring expansion rate |
| drop SSBO capacity | **120,000** | — | max drops the GPU buffer holds |
| ripple SSBO cap | **512** | — | hard ripple budget (binding 4) |
| per frame | **1 compute dispatch + 2 draw calls** | — | total rain GPU work |

---

## 3. SKY

**What it is:** a **cubemap skybox** — 6 textured faces of a cube drawn at infinite distance,
also used as the **environment reflection** for the water and objects.

### Where to look
- **Geometry + draw:** `main.cpp` (the `skyboxVertices[]` cube + the skybox draw block ~line 860).
- **Shaders:** `assets/shaders/skybox.vert`, `skybox.frag`.
- **Textures:** `assets/textures/skybox/` — 4 selectable sets (`sky_1`, `sky_2`, `sky_3`,
  `environment_1`), each a folder of 6 PNG faces (right/left/top/bottom/front/back).

### How it works
1. The 6 cubemap PNGs are loaded into a `GL_TEXTURE_CUBE_MAP` (`loadCubemap` in `main.cpp`).
2. **Vertex shader trick (`skybox.vert`):** the cube is drawn with the camera's **rotation only**
   (translation stripped: `mat3(view)`), and `gl_Position = pos.xyww` forces depth = 1.0 so the sky
   is always behind everything.
3. **Render order:** drawn **after** the opaque scene with `glDepthMask(FALSE)` + `GL_LEQUAL`, so it
   only fills pixels the scene didn't cover (efficient — no overdraw under the ocean).
4. **Reused as reflection:** the same cubemap is sampled by the water (`standard.frag`) and objects
   (`object.frag`) for realistic environment reflection/ambient. The UI **"sky" dropdown** swaps the
   active cubemap live.

### Key numbers
- 36 vertices (12 triangles), 1 draw call, depth-trick (no depth writes). Cubemap = 6 PNGs/set, 4
  sets selectable.

---

## 4. VEHICLES (jet-ski, yacht, big-ship) — movement + water interaction

**What it is:** three **glTF boat models** that **navigate** the bay (steering, collision
avoidance, banking) and **physically float** on the FFT waves via 6-DOF buoyancy, leaving **wakes**.

### Where to look
- **Setup + navigation + drawing:** `main.cpp` — the `Vehicle` struct (line ~231), the spawn/setup
  (~256), the per-frame navigation block (~566), the buoyancy step (~660), the draw block (~843).
- **Buoyancy physics:** `src/water/BoatPhysics.{h,cpp}`.
- **Wake (water interaction):** `src/ocean/GPUDisturbance.{h,cpp}` (boats inject ripples; see §8).
- **Model loading:** `src/graphics/Model.{h,cpp}` (see §7).

### A) Movement — what principle drives it
Navigation is **kinematic steering** (not a car/engine sim). Each frame, per boat:
1. **Advance** along its heading at its `speed`.
2. **Blend three steering urges** into a *desired* heading (a simplified **boids/flocking** model):
   - **(a) Boundary return** — pull back toward centre when past `0.7 × kSoftRadius (300 m)`.
   - **(b) Separation** — steer away from the other boats (size-aware keep-distance).
   - **(c) Rock avoidance** — steer around the central rock.
3. **Rate-limited turn** toward that heading (`boatTurnRate = 0.45 rad/s`, sharper near the edge) —
   so boats curve **gradually**, never snap. A gentle sinusoidal **wander** adds natural path curves.
4. **Hard safety net** — after steering, hard clamps guarantee no boat-boat overlap and no phasing
   through the cliffs (`kHullLimit = 400 m`) or the rock (`kRockHard = 45 m`).
5. **Banking** — boats **lean into turns** (visual roll ∝ yaw-rate × speed), eased smoothly.

### B) Water interaction — what principle
Two directions of interaction:
- **Water → boat (buoyancy):** see §5 below — the boat heaves/pitches/rolls on the waves.
- **Boat → water (wake):** each frame a moving boat calls `disturbance.disturb(sternXZ, amplitude,
  sigmaTexels)`, injecting a ripple into the GPU disturbance field (§8). The wake is **scaled per
  boat from one `wakeStrength` slider**: the injection point is the visible hull centre
  (`Model::localCenter()`) pushed **behind the transom** by ~0.6× the footprint so it trails as a
  separation wake (not welling up under the hull); the **footprint** tracks the beam
  (`sigmaTexels = clamp(wakeWidth/texelM, 1.5, 5.0)` → tight ripple for the jet-ski, broad swell
  for the ship); and the **amplitude** is `wakeStrength × wakeScale × min(1, speed/12)`
  (`wakeScale`: jet-ski 0.22, yacht 0.70, ship 1.0).
- **Foam:** the white churn around boats is the water shader's **whitecap foam** (FFT Jacobian)
  plus the disturbance ripples — there is **no separate foam particle system** in the current build.

### Per-vehicle numbers (`main.cpp` vehicles table)
| Vehicle | speed | scale | hull length | beam (2×wakeWidth) | wakeScale | notes |
|---|---|---|---|---|---|---|
| jet-ski | 16 m/s | 120 | 5 m | 8 m | 0.22 | tiny model (glTF bakes 0.01 scale) → big multiplier; snappy buoyancy |
| yacht | 8 m/s | 0.03 | 90 m | 28 m | 0.70 | model faces backward → `modelYaw = +π` |
| big-ship | 5 m/s | 0.02 | 110 m | 40 m | 1.00 | heaviest → slowest buoyancy response; wake reference |

Controls: **P** = pause/resume all vehicles. Per-boat speed sliders in the ImGui "Vehicles" panel.
The lone central rock can be toggled on/off in the ImGui **"Scene"** section (hiding it also frees
its collision space so boats pass through).

---

## 5. BUOYANCY (how boats float on the waves)

**What it is:** **force-based, smoothed 6-DOF buoyancy.** Navigation (X/Z + yaw) is scripted; the
physics solves the **vertical heave + pitch + roll** so the boat rides the swell realistically.

### Where to look
- `src/water/BoatPhysics.{h,cpp}` — the `step()` function is the whole model.
- Driven from `main.cpp` (~660) using `ocean.sampleSurfaceHeight()` as the height source.

### How it works (the principle)
The hull is approximated by a grid of **sample facets** (`facetsX × facetsZ`, default **5 × 3 =
15**) laid out over `length × beam`. Each frame:
1. Transform each facet into world space (with the current orientation).
2. **Sample the ocean surface height** under every facet.
3. **Heave:** a **critically-damped spring** pulls the boat's Y toward the **average** facet height
   (`stiffness = buoyancy × 3`, damped by `linearDamp`) — so it always floats at the surface and
   never sinks or jitters.
4. **Pitch / roll:** computed from the **surface slope across the hull** — the fore-vs-aft height
   gradient sets target pitch, port-vs-starboard sets target roll. Each is a damped spring toward
   that target (`angStiff = 6`, damped by `angularDamp`). Tilt is **clamped to ±0.6 rad** so a
   violent wave can't capsize the boat.
5. Orientation = `yaw × pitch × roll`; the boat's position.y + orientation are read back by the
   renderer.

> Why this design: a long ship's facets span **many wavelengths**, so short choppy waves **average
> out** → big ships ride smoothly while small boats bob lively — exactly like reality. (An earlier
> single-point sample made a 100 m ship bob like a cork; a naive raw-force sum made boats sink.)

### Key parameters
| Parameter | Default | Effect |
|---|---|---|
| `buoyancy` (float strength) | 3.0 | how firmly/high it floats |
| `linearDamp` (heave damping) | 2.0 (jet-ski 3.5, ship 1.6) | settle speed vs. bobbiness |
| `angularDamp` (tilt damping) | 2.8 (jet-ski 4.0, ship 2.4) | rock/roll steadiness |
| facets | 5 × 3 = 15 | hull sampling density |
| tilt clamp | ±0.6 rad (~34°) | anti-capsize |

---

## 6. WATER SHADING (how the surface looks)

**What it is:** a **physically-based, HDR ocean shader** — reflection + refraction + subsurface
scatter + sun glints + foam, composited in HDR and tonemapped. Modeled after the
*achalpandeyy/OceanFFT* HDR-Fresnel approach.

### Where to look
- `assets/shaders/standard.vert` (vertex displacement + UVs) and `standard.frag` (all the lighting).
- All the look/lighting uniforms are pushed from `main.cpp` (~804).

### How it works (the terms, in order)
1. **Normals.** Start from the FFT **normal map** (unit 2), add the **physics normal**, add
   **procedural mid-frequency detail** (`applyMediumWaveDetail` — a few animated sine slopes so the
   surface has fine ripples between FFT texels), add the **disturbance** slope (C-key/boat wakes),
   add the **rain ripple rings**.
2. **Fresnel** (Schlick, R0 = 0.02) — the master blend: steep slopes facing you stay dark ocean
   colour; slopes glancing the sky pick up bright sky → this contrast makes each wave "read."
3. **Reflection** — samples the **skybox cubemap** off the wave normal, **boosted to HDR** by
   `uReflectStrength`, with a horizon-tint fallback when the ray dips below the horizon.
4. **Refraction (body colour)** — depth-tinted: deep troughs = `uDeepColor` (dark navy), crests =
   `uShallowColor` (teal), blended by height via `uDepthFalloff`. Lifted by sun diffuse.
5. **Subsurface scatter** — `uScatterColor` glow on **sun-backlit crests** (the green "lit-from-
   within" look of real swells).
6. **Sun specular** — two lobes: a tight `pow(N·H, 1200)` **glint** + a broad `pow(N·H, 120)`
   **glitter** path (`uSunGlint`, `uSunGlitter`).
7. **Foam** — FFT-Jacobian whitecaps (from normal-map alpha), soft-edged, sun-shaded.
8. **Distance haze** → blend to horizon colour far away.
9. **HDR tonemap** — `1 − exp(−color × uExposure)` for crisp contrast (instead of a flat clamp).

### Important parameters (all live in the "Lighting" / "Water" ImGui sections)
| Parameter | Default | Role |
|---|---|---|
| deep / shallow colour | navy / teal | water body colour by depth |
| depth tint falloff | 0.02 | how fast troughs darken |
| mid-wave detail | 0.4 | procedural fine-ripple strength |
| reflection strength | 1.8 | sky reflection HDR boost |
| sun azimuth / elevation | 31° / 60° | **movable sun** direction |
| sun glint / glitter | 4.0 / 0.4 | sharp highlight / sparkle |
| HDR exposure | 1.4 | overall brightness/contrast |
| scatter colour | teal | backlit-crest glow |

---

## 7. MODELS — rocks, vehicles, cliffs (glTF loading & texturing)

### What is glTF?
**glTF** ("GL Transmission Format", `.gltf` + `.bin` + texture images, or a single `.glb`) is the
standard runtime 3D model format — it stores **meshes** (vertex positions, normals, UVs, indices), a
**node hierarchy** (each part's transform), and **PBR materials** (base-colour/normal/roughness/AO
textures). It's the "JPEG of 3D." We use it because it's modern, carries PBR materials, and has tons
of free CC0 assets (Poly Haven, Sketchfab).

### Where to look
- **Loader:** `src/graphics/Model.{h,cpp}` (uses `external/cgltf/cgltf.h`, a single-header parser).
- **Geometry container:** `src/graphics/Mesh.{h,cpp}`. **Textures:** `src/graphics/Texture.{h,cpp}`.
- **Shader:** `assets/shaders/object.vert`, `object.frag`.
- **Assets:** `assets/models/` (rock_marble_cliff_05, mountain_terrain/coastal_cliff_04, jet-ski,
  yacht, big-ship).

### How loading works (`Model::loadFromFile`)
1. `cgltf` parses the `.gltf` + loads its `.bin` buffers.
2. **Walk the node hierarchy** recursively, accumulating each node's world transform, and **bake
   that transform into the vertices** (`world = parent × local`). *This is essential* — the yacht
   (23 meshes) and big-ship (62 meshes) are multi-part models positioned by nodes; without baking
   the transforms they'd collapse into a jumble at the origin.
3. For each primitive: read position/normal/UV accessors → a `Mesh` (VAO/VBO/EBO), and read its
   material's **base-colour / metallic-roughness / normal / occlusion** textures (or the
   base-colour *factor* when there's no texture — e.g. the untextured yacht/ship render in flat
   colours).
4. `Model::draw()` sets the model matrix + binds the material maps (units 4–7) and draws each mesh.

### Texturing (`object.frag`)
PBR-ish: samples albedo, **normal map** (via a tangent-free **screen-space-derivative** TBN — no
tangent attribute needed), roughness (glTF packs it in the **G** channel), AO. Lights with sun
diffuse + skybox ambient + roughness-scaled specular, then **gamma-encodes** (`pow(c, 1/2.2)`) —
critical: albedo is uploaded as **sRGB** so the shader works in linear and must encode back, or
everything looks dark/muddy. Non-uniform scale uses an **inverse-transpose normal matrix**.

### The rock & cliffs specifically
- **Foreground rock:** `rock_marble_cliff_05` (Poly Haven scan, ~107 K triangles, full PBR
  textures), scaled 4×, placed at (60, −3, 30). A **procedural fallback** exists
  (`RockGenerator` — a noise-displaced icosphere) if the file is missing.
- **Mountains = coastal cliffs:** `coastal_cliff_04` (a real ~87 m Poly Haven cliff scan) placed as
  a **uniform ring of 8 instances** around the bay edge (`main.cpp` `mountains[]`), each scaled ~9×
  and **rotated to face inward** (`faceIn = atan2(-px,-pz)`), stretched taller in Y. All 8 sit at
  the **same radius 470 m** — the 4 edge pieces on the axes (`kEdge = 470`) and the 4 corners at
  `kCorner = kEdge × 0.7071 ≈ 332` axial — so the heavily-overlapping ring forms a continuous
  coastline that **hides the square water patch's edge** with no diagonal void. (A procedural
  noise-terrain system existed earlier but was removed — the real cliff scans look far better.)

### Key model numbers
| Model | meshes | textures | raw size → world | notes |
|---|---|---|---|---|
| marble cliff (rock) | 1 | 3 (diff/normal/ARM) | ×4 | foreground hero rock |
| coastal cliff | 1 | 3 | ×9, ×8 instances | the mountain ring |
| jet-ski | 1 | 4 | ×120 (model is ~0.15 u) | textured |
| yacht | 23 | 0 (flat colour) | ×0.03 → ~90 m | multi-part node hierarchy |
| big-ship | 62 | 0 | ×0.02 → ~110 m | multi-part node hierarchy |

---

## 8. WAKE / DISTURBANCE (boat wakes + the C-key wave)

**What it is:** a **GPU wave-equation simulation** on a separate height texture — interactive
ripples from the **C-key** and from **moving boats' sterns**, layered on top of the FFT ocean.

### Where to look
- **C++:** `src/ocean/GPUDisturbance.{h,cpp}`.
- **Shaders:** `assets/shaders/disturbance_inject.comp` (add a Gaussian bump),
  `disturbance_propagate.comp` (the wave equation step).
- **Consumed by:** `standard.vert` (adds to vertex height) + `standard.frag` (adds to the normal).

### How it works (the principle)
It solves the **2D wave equation** on a 256² height field using **Verlet integration**:
```
next = (2·current − previous + waveC · laplacian) · damping
```
- `laplacian` = the 4-neighbour stencil (how curved the surface is at each texel).
- `waveC = (speed² · dt²)/dx²` controls propagation speed; `damping ≈ 0.997–0.999` makes ripples
  fade. Three textures rotate as **current / previous / next** each frame (ping-pong).
- **`disturb(worldPos, amplitude, sigmaTexels)`** injects a Gaussian bump (whose footprint radius
  is `sigmaTexels`) at an XZ point → it then **spreads outward and fades** like a real ripple. The
  **C-key** fires one 40 m ahead of the camera (default sigma); **moving boats** inject one just
  behind their stern every frame, with footprint and amplitude **scaled per boat** (see §4 B) so a
  jet-ski leaves a tight ripple and the ship a broad swell, both trailing as a separation wake.
- The result feeds the water: the **vertex shader** raises the surface by the disturbance height,
  and the **fragment shader** bends the normal by its slope (so wakes catch the light).

### Key numbers
| Quantity | Value |
|---|---|
| disturbance texture | **256 × 256** (R32F), covers the 1024 m ocean |
| integration | Verlet, 3-texture ping-pong |
| damping | ~0.997–0.999 |
| C-key amplitude | `cKeySplash` (default 3, 0–10) |
| boat wake amplitude | `wakeStrength × wakeScale × min(1, speed/12)` (slider default 0.01) |
| boat wake footprint | `clamp(wakeWidth / texelM, 1.5, 5.0)` texels (texel ≈ 4 m) |

---

## 9. Performance summary (numbers for the presentation)

- **~112 FPS** on an RTX 3090 at defaults (1024×768 window).
- **GPU per frame:** the FFT (`~18 IFFT dispatches` + spectrum/phase/displacement/normal) on 512²
  textures; the 256² disturbance step; rendering a 1024² ocean grid (~2 M triangles) + the models.
- **CPU per frame:** only the throttled rain ripple-ring spawner (~480/s), boat steering + 6-DOF
  buoyancy (15 facets × 3
  boats), and **one async PBO readback** of the 512² displacement (the readback is double-buffered
  to avoid a GPU stall — the single biggest perf decision on the buoyancy side).
- **Key optimizations:** Stockham FFT (no shared-mem cap, enables 512); async PBO height readback
  (no stall); **per-pixel ripple early-out** (squared-distance reject before the heavy math — the
  fix that unstuck weaker/Windows GPUs); **fully GPU rain** (compute update + instanced draws, no
  CPU per-drop work / no vertex upload); rain ripple-budget throttling (fixed 512-entry SSBO);
  camera-local rain; cubemap skybox depth-trick (no overdraw).

---

## 10. Quick "where do I look?" cheat-sheet

| To understand… | Open these files |
|---|---|
| The whole pipeline / render loop | `src/main.cpp` |
| FFT ocean simulation | `src/ocean/GPUFFTOcean.cpp` + `assets/shaders/fft_*.comp` |
| How waves are drawn / lit | `assets/shaders/standard.vert` + `standard.frag` |
| Rain (GPU) | `src/water/GPURain.cpp` + `assets/shaders/rain_update.comp`, `rain_gpu.*`, `rain_splash.*` |
| Sky | `main.cpp` skybox block + `assets/shaders/skybox.*` |
| Boats moving | `src/main.cpp` vehicle navigation block (~line 566) |
| Boats floating | `src/water/BoatPhysics.cpp` |
| Boat wakes / C-key | `src/ocean/GPUDisturbance.cpp` + `assets/shaders/disturbance_*.comp` |
| Loading models (glTF) | `src/graphics/Model.cpp` (+ `Mesh`, `Texture`) |
| Object/rock/cliff shading | `assets/shaders/object.frag` |
| All tunable parameters | `src/main.cpp` ImGui block (~line 670) |

---

## 11. Credits, licenses & references (attribution + versioning)

Everything in the project that originates from someone else — vendored libraries, art assets, and
published algorithms — is recorded here, with the version we ship and the license we're bound by.
Items marked **TODO** are unresolved attribution gaps that must be closed before public release.

### 11.1 Third-party libraries (all vendored under `external/`)

Versions are read from the vendored headers; pin these in any write-up.

| Library | Version | License | Role | License file in-tree |
|---|---|---|---|---|
| GLFW | **3.4.0** | zlib/libpng | window + input + GL context | `external/glfw/LICENSE.md` ✓ |
| Dear ImGui | **1.91.9** | MIT | control-panel UI | `external/imgui/LICENSE.txt` ✓ |
| GLM | **1.1.0** | MIT / "The Happy Bunny" | vector/matrix math | ⚠️ **TODO** — none vendored |
| stb_image | **2.30** | public domain (MIT / Unlicense) | image loading | banner in header only |
| stb_image_write | **1.16** | public domain (MIT / Unlicense) | PNG frame capture | banner in header only |
| cgltf | **1.15** | MIT | glTF parser | banner in header only |
| GLAD | generated, **OpenGL 4.6 core** | glad code public-domain/MIT; Khronos headers Apache-2.0/MIT | GL function loader | ⚠️ **TODO** — none vendored |

- **Engine targets:** C++17, OpenGL 4.6 core profile (compute shaders + SSBO + image load/store).
  Build with CMake ≥ 3.15. `external/` is third-party and excluded from the quarter study docs.
- **Action:** add the upstream license text for **GLM, stb, cgltf, and GLAD** (the others already
  ship a license file).

### 11.2 3D models — **CC-BY-4.0, author credit is legally required**

These ship from Sketchfab under CC-BY-4.0; the author **must** be credited wherever the work is
shared (report, slides, repo). Per-model `license.txt` files are bundled; the required credit lines
are reproduced here so they travel with the project:

- **Jet-ski** — *"Kawasaki 310XUltra Jet Ski"* by **XOIAL**
  (https://sketchfab.com/3d-models/kawasaki-310xultra-jet-ski-37a2348f02da472d98310fd5621307c6),
  licensed under CC-BY-4.0. — `assets/models/jet-ski/`
- **Yacht** — *"Yacht"* by **Gman The Cruise Dude**
  (https://sketchfab.com/3d-models/yacht-5d8bd8b42bbc4cb1ad42d5ae68a63794),
  licensed under CC-BY-4.0. — `assets/models/yacht/`
- **Big ship** — *"SS Royal Bastion"* by **Gman The Cruise Dude**
  (https://sketchfab.com/3d-models/ss-royal-bastion-a18c4bafe79744129cd9e1f3557a0168),
  licensed under CC-BY-4.0. — `assets/models/big-ship/`

### 11.3 Poly Haven assets — CC0 (credit by convention, not required)

Models + their PBR texture sets, all **CC0 1.0** (public domain) from polyhaven.com:

- **coastal_cliff_04** — the cliff ring around the bay (`assets/models/mountain_terrain/`, loaded at
  `main.cpp:283`).
- **marble_cliff_05** — the central rock (`assets/models/rock_marble_cliff_05/`, loaded at
  `main.cpp:217`).
- **marble_cliff_06**, **dry_riverbed_rock** — present in the tree but **not loaded by any code**.
  Either credit-and-keep or delete to avoid shipping unused assets.

### 11.4 Skybox cubemaps — ⚠️ **TODO: source + license unknown**

Four cubemap sets ship in `assets/textures/skybox/` (the root set + `environment_1`, `sky_1`,
`sky_2`, `sky_3`), six faces each. **No license or source file accompanies them.** Their origin and
license must be identified and recorded here before any public distribution — this is the project's
highest-priority attribution gap.

### 11.5 Algorithms & techniques (academic references)

Implemented from published work; cite these in the report:

| Technique | Reference | Where in code |
|---|---|---|
| FFT ocean surface (spectrum → IFFT → displacement, choppiness, Jacobian foam) | Tessendorf, *Simulating Ocean Water*, SIGGRAPH 2001 | `src/ocean/GPUFFTOcean.cpp`, `assets/shaders/fft_*.comp` |
| Unified directional wave spectrum (long+short wave terms, JONSWAP γ, spreading Δ, capillary 0.23 m/s) | Elfouhaily, Chapron, Katsaros & Vandemark, 1997 ("Elfouhaily/IFREMER") | `fft_initial_spectrum.comp:49-77` |
| Gravity–capillary dispersion `ω = √(g·k·(1+(k/k_m)²))` | Standard oceanography (Lamb; Kinsman) | `fft_initial_spectrum.comp:33` |
| Stockham auto-sort radix-2 FFT (no shared-memory cap) | Stockham FFT (Cochran et al.; cf. GPU Gems FFT-ocean) | `fft_horizontal.comp`, `fft_vertical.comp`, `GPUFFTOcean.cpp:228` |
| Fresnel reflectance approximation (R₀ = 0.02) | Schlick, 1994 | `assets/shaders/standard.frag:160` |
| Normal mapping without precomputed tangents (cotangent frame) | Mikkelsen, 2010 (surface-gradient / cotangent-frame trick) | `assets/shaders/object.frag:28` |
| 2-D wave-equation wake field via explicit Verlet integration | Standard finite-difference / Verlet | `assets/shaders/disturbance_propagate.comp` |
| Steering behaviours (containment, separation, obstacle avoidance) | Reynolds, *Steering Behaviors / Boids*, 1987/1999 | `src/main.cpp:549-590` |
| Exposure tone-mapping `1 − e^(−color·exposure)` | Standard HDR tonemap | `assets/shaders/standard.frag` |

> **TODO — adapted vs. from-scratch:** the spectrum code's structure closely matches a well-known
> open-source GPU-ocean implementation. If it was adapted from a specific repo/tutorial rather than
> coded directly from the Elfouhaily paper, add that repo + its license here.

### 11.6 Code provenance — ⚠️ **TODO: verify LearnOpenGL derivation**

The Q3 engine boilerplate — `src/core/Camera.cpp`, `src/graphics/Shader.cpp`,
`src/graphics/Mesh.cpp`, and the skybox/cubemap loader — closely follows the **LearnOpenGL**
tutorials (Euler-angle camera with integer `ProcessKeyboard` direction codes + `GetViewMatrix`, the
`.xyww` skybox depth trick, the `Shader` `setX` uniform wrapper). No attribution comment is present.
**Confirm** whether these were adapted from LearnOpenGL; if so, credit **Joey de Vries /
learnopengl.com (CC BY-NC 4.0)** — and note the **non-commercial** clause if the project is ever
distributed beyond coursework.

### 11.7 Open attribution gaps (do before release)

1. **Skybox source/license** — currently unknown (§11.4). *Highest risk.*
2. **Surface the CC-BY model credits** (§11.2) into the README and the presentation slides, not just
   the bundled `license.txt` files.
3. **Confirm LearnOpenGL** (or other tutorial) provenance for the Q3 engine classes (§11.6).
4. **Add missing LICENSE files** for GLM, stb, cgltf, GLAD (§11.1).
5. **Resolve unused assets** marble_cliff_06 + dry_riverbed_rock (§11.3) — cite or remove.
6. **Confirm/cite** the ocean-spectrum implementation's origin (§11.5 TODO).
