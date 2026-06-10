# Quarter 2 — Ocean Spectral Simulation & Water Surface

## 📚 The four study quarters

This project's implementation is divided into **four roughly-equal study units**. Together they
cover **every** source and shader file in the project; each unit is written to be understood on
its own, and the four explanations together account for the whole implementation.

| Quarter | Theme (what you'd master) | Files it owns |
|---|---|---|
| **Q1 — Application Host & Frame Loop** | The program that drives everything: startup, the fixed-timestep render loop, input/camera control, the ImGui control panel, scene assembly, frame recording, and the build system. | `src/main.cpp`, `CMakeLists.txt`, `build_and_run.sh`, `.gitignore` |
| **Q2 — Ocean Spectral Simulation & Water Surface** | The signature feature: the spectral FFT ocean — ocean spectrum → GPU Stockham IFFT → displacement/normal/foam → the displaced, shaded water surface. | `src/ocean/GPUFFTOcean.*`, `src/ocean/OceanMesh.*`, the 7 `fft_*.comp` shaders, `standard.vert`, `standard.frag` |
| **Q3 — Rendering Engine, Camera & Scene Objects** | The reusable rendering toolkit (GL context/window, camera, shader programs, meshes, glTF model loading, textures) and how solid scene objects (boats, rock, cliffs) + the skybox are drawn. | `src/core/Window.*`, `src/core/Camera.*`, `src/graphics/{Shader,Mesh,Model,Texture}.*`, `object.vert/frag`, `skybox.vert/frag`, `debug_wireframe.frag` |
| **Q4 — Dynamic Water Interactions** | Everything dynamic layered on the ocean: boat buoyancy (6-DOF spring-damper), GPU rain + splashes, and the boat-wake / ripple disturbance field. | `src/water/BoatPhysics.*`, `src/water/GPURain.*`, `src/ocean/GPUDisturbance.*`, the `rain_*` shaders, `disturbance_*.comp` |

> The repository is a real-time GPU water simulator in **C++17 + OpenGL 4.6** (GLFW, GLAD, GLM,
> STB, cgltf, Dear ImGui — all vendored under `external/`, which is NOT covered by these docs).

**This document covers Q2.**

---

## 🧭 In one minute (the high-level picture)

This quarter is **the ocean itself** — the moving, glittering water surface that the whole demo is
named after. Everything else (boats, rain, rocks) is decoration layered on top of what this code
produces.

Think of it like a **player piano**. A composer (oceanography) writes down, once, a "score" that
says how tall and how frequent every possible wave size should be for a given wind. That frozen
score is the *initial spectrum*. Then, every single frame, a fast machine (the GPU) reads the
score, advances time a little, and unfolds it into the actual up-and-down shape of the water — a
heightfield you can see and that boats can float on. The unfolding step is a **Fourier transform**:
the trick that turns "a list of wave ingredients" into "the actual bumpy surface".

Why does it exist? Because real ocean waves are not one sine wave — they are *thousands* of
overlapping waves of every size and direction at once. Animating those by hand is impossible. The
spectral / FFT approach is the industry-standard way (used in films and AAA games) to get a
believable, non-repeating sea cheaply: you describe the *statistics* of the sea once, and the FFT
gives you a tileable patch of real-looking water for free, every frame.

What would break without it? Everything visual and physical about the water. There would be no
waves to render, no surface normals to light, no foam on the crests, no height for boats to bob on,
and nothing for rain ripples or boat wakes to sit on top of. This quarter produces three outputs
that the rest of the program leans on: a **displacement texture** (how far each point of water
moves), a **normal+foam texture** (which way each point faces, plus where whitecaps are), and a
**CPU copy of the heights** so the physics code can ask "how high is the water right here?".

The two halves of the quarter are: (1) `GPUFFTOcean` + the seven `fft_*.comp` compute shaders,
which *compute* the moving water on the GPU; and (2) `OceanMesh` + `standard.vert`/`standard.frag`,
which *draw and shade* it into a beautiful HDR sea.

---

## 🔌 How this quarter connects to the others

**Inputs into Q2 (things produced elsewhere that we consume):**

- **From Q1 (Application Host)** — Q1 owns the `GPUFFTOcean` and `OceanMesh` objects. Each frame Q1
  calls `ocean.update(dt)` inside the fixed-timestep substep loop, then `ocean.readbackDisplacement()`
  once per render frame. Q1 binds our output textures to texture units, sets all the look/lighting
  uniforms on `standard.frag` (sun direction, colours, exposure, `time`), supplies `view`/`projection`/
  `viewPos`, and calls `OceanMesh::draw()`. Q1 also feeds the runtime knobs (`setWindSpeed`,
  `setSeaMaturity`, `setChoppiness`, `setHeightScale`, …) from the ImGui panel.
- **From Q3 (Rendering Engine)** — the `Shader` class (`src/graphics/Shader.h`) compiles every one of
  our compute shaders and the `standard` draw program; Q2 only ever calls its `use()`, `setInt`,
  `setFloat`, `setUInt`, `setVec2`, `setMat4` helpers. Q3 also owns the **skybox cubemap** which we
  sample for reflections in `standard.frag` (`uniform samplerCube skybox`).
- **From Q4 (Water Interactions)** — two channels feed `standard.frag`:
  - **`disturbanceMap`** (GL texture unit **3**), a single-channel height field of interactive
    "C-key" waves / boat wake, produced by Q4's `GPUDisturbance` (256×256). `standard.vert` adds it to
    the vertex height; `standard.frag` finite-differences it into a normal perturbation.
  - **Ripple SSBO** at **`binding = 4`** (`RippleBuffer`), an array of `vec4` rain-ripple centres/ages
    produced by Q4's `GPURain`. `standard.frag` draws expanding ring whitecap normals from it.

**Outputs from Q2 (things we produce that others consume):**

- **Displacement texture** (`getDisplacementTexture()`, `GL_RGBA32F`) — bound by Q1 at **texture unit 1**
  as `displacementMap` for `standard.vert`. `.xyz = (dx, height, dz)` in metres.
- **Normal texture** (`getNormalTexture()`, `GL_RGBA16F`) — bound by Q1 at **texture unit 2** as
  `normalMap` for `standard.frag`. `.xyz = surface normal`, `.a = foam`.
- **CPU height query** — `sampleSurfaceHeight(worldX, worldZ)` and `sampleDisplacement(worldX, worldZ)`
  are called by **Q4's `BoatPhysics`** so boats sit *on* the wave (choppiness-corrected), not beside it.
  The data is filled by `readbackDisplacement()` and is **one frame stale** by design (see Gotchas).

The clean hand-off rule: Q2 **produces textures and a CPU heightfield**; Q1 **wires them up**; Q3
**supplies the GL toolkit + skybox**; Q4 **feeds disturbance + ripples back in** and **reads heights out**.

---

## 🗂️ Files in this quarter

| File | Lines (approx) | One-line role |
|---|---|---|
| `src/ocean/GPUFFTOcean.h` | 107 | Public interface + all GPU texture/PBO/ping-pong state of the ocean simulator. |
| `src/ocean/GPUFFTOcean.cpp` | 382 | The orchestrator: builds textures, dispatches all 7 compute passes per frame, async CPU readback, surface sampling. |
| `src/ocean/OceanMesh.h` | 26 | Interface for the flat water grid (VAO/VBO/EBO). |
| `src/ocean/OceanMesh.cpp` | 78 | Builds and draws the flat, origin-centred grid the FFT displaces. |
| `assets/shaders/fft_initial_spectrum.comp` | 85 | Oceanographic spectrum (JONSWAP/Elfouhaily-style) → frozen `h0(k)` texture. One-time. |
| `assets/shaders/fft_phase.comp` | 40 | Time-evolves the per-wave phase via the dispersion relation. Per frame. |
| `assets/shaders/fft_spectrum.comp` | 56 | Assembles the frequency-domain wave field `h(k,t)` + choppiness `dx`/`dz`. Per frame. |
| `assets/shaders/fft_horizontal.comp` | 65 | Stockham radix-2 IFFT, one row (horizontal) butterfly pass. Run `log2(N)` times. |
| `assets/shaders/fft_vertical.comp` | 57 | Stockham radix-2 IFFT, one column (vertical) butterfly pass. Run `log2(N)` times. |
| `assets/shaders/fft_displacement.comp` | 29 | Normalises (1/N) the spatial result into the `(dx, height, dz)` displacement texture. |
| `assets/shaders/fft_normal.comp` | 54 | Finite-difference surface normals + Jacobian-based foam mask. |
| `assets/shaders/standard.vert` | 48 | Displaces the grid vertices by sampling the displacement map (+ disturbance). |
| `assets/shaders/standard.frag` | 219 | HDR deep-ocean shading: fresnel reflection/refraction/scatter/glint/foam/ripples/haze. |

---

## 🔬 Deep dive

### `src/ocean/GPUFFTOcean.h` — the simulator's contract & state

This header declares the `GPUFFTOcean` class. The public surface is small and falls into four groups:

- **Construction** — `GPUFFTOcean(unsigned int resolution, float oceanSize, float windSpeed, float windAngleDegrees, float choppiness)` (`GPUFFTOcean.h:11`). `resolution` is the FFT grid size N (must be power-of-two ≤ 1024); `oceanSize` is the physical side length L of one tileable patch in metres.
- **Per-frame driving** — `update(float deltaTime)` (`:14`) runs the whole GPU pipeline once; `readbackDisplacement()` (`:43`) pulls heights back to the CPU **once per render frame, not per substep**.
- **Output accessors** — `getDisplacementTexture()` (`:17`), `getNormalTexture()` (`:18`), `getOceanSize()` (`:16`).
- **Runtime knobs** — two flavours. Cheap ones that just set a member used as a uniform next frame: `setHeightScale`/`setHorizontalScale`/`setChoppiness`/`setTimeScale` (`:27–30`). Expensive ones that **rebuild the initial spectrum**: `setWindSpeed` (`:31`), `setWindAngle` (`:32`), `setSeaMaturity` (`:37`), `setAmplitude` (`:38`).
- **CPU sampling** — `sampleDisplacement(worldX, worldZ)` (`:44`) returns the `(dx, height, dz)` vector at a world XZ; `sampleSurfaceHeight(worldX, worldZ)` (`:51`) returns the height *that visually appears* at (X,Z), correcting for the sideways choppiness push.

**Key private members (`:53–98`):**

- Seven `Shader` members, one per compute pass (`:67–73`): `m_initialSpectrumShader`, `m_phaseShader`, `m_spectrumShader`, `m_fftHorizontalShader`, `m_fftVerticalShader`, `m_displacementShader`, `m_normalShader`.
- The texture zoo (`:75–85`):
  - `m_gaussianNoiseTexture` (`GL_RG32F`) — frozen white-noise field (two independent N(0,1) samples per texel) used to randomise the spectrum.
  - `m_initialSpectrumTexture` (`GL_RG32F`) — the frozen complex amplitude `h0(k)`.
  - `m_phaseTextures[2]` (`GL_R32F`) — double-buffered scalar phase per wave (ping-ponged each frame).
  - `m_spectrumTextureA` (`GL_RGBA32F`, holds `dx` in `.xy`, `h` in `.zw`) and `m_spectrumTextureB` (`GL_RG32F`, holds `dz` in `.xy`) — the per-frame frequency-domain field that feeds the FFT.
  - `m_intermediateTextureA/B` and `m_spatialTextureA/B` — the **two ping-pong buffer pairs** the Stockham FFT bounces between.
  - `m_displacementTexture` (`GL_RGBA32F`) and `m_normalTexture` (`GL_RGBA16F`) — the public outputs.
- **FFT ping-pong bookkeeping** (`:89–92`): `m_fftRowResultA/B` track where the row pass landed; `m_fftFinalA/B` track where the column pass landed. Because the number of passes is `log2(N)` (parity varies), the final result can be in *either* pair — so the code records the handles rather than assuming.
- **CPU readback state** (`:95–98`): `m_cpuDisplacement` (a `std::vector<glm::vec4>`, the CPU mirror), `m_pbo[2]` (two pixel-pack buffers for ping-pong async readback), `m_pboIndex`, and `m_readbackCount` (guards the first frame when no PBO is filled yet).

Private helpers: `initializeTextures()`, `initializeNoiseTexture()`, `buildInitialSpectrum()`, and `configureTexture(texture, internalFormat)`.

---

### `src/ocean/GPUFFTOcean.cpp` — the orchestrator

#### File-local constants (`:11–14`)
`kMaxResolution = 1024` (largest allowed N) and `kPi = 3.14159265359f`.

#### Constructor (`:16–56`)
Initialises all the tunables. Note the **defaults baked here** because they are coupled to shader behaviour:
- `m_heightScale = 1.0f`, `m_horizontalScale = 0.9f` (`:22–23`) — feed the displacement shader's `heightScale`/`horizontalScale`; the comment notes the FFT applies a single `1/N` and these dial the rest.
- `m_timeScale = 2.0f` (`:24`) — multiplies `omega*dt` in the phase shader, speeding up the animation.
- `m_seaMaturity = 2.0f` (`:25`) → uniform `OMEGA`; `m_amplitude = 1500.0f` (`:26`) → uniform `SPECTRUM_BOOST`.
- `m_windDirection` (`:27`) is the *normalised* `(cos θ, sin θ)` of the wind angle.

The constructor validates N is a non-zero power of two ≤ `kMaxResolution` via the bit trick `(resolution & (resolution - 1u)) != 0u` (`:49`) and warns otherwise. It then calls `initializeTextures()`, `initializeNoiseTexture()`, `buildInitialSpectrum()` (`:53–55`).

#### Destructor (`:58–71`)
Deletes every texture and both PBOs.

#### `configureTexture` (`:73–80`)
Allocates immutable storage with `glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, N, N)` (1 mip), sets `GL_LINEAR` min/mag (so the vertex/fragment shaders get bilinear interpolation for free) and `GL_REPEAT` wrap on both axes (so the patch tiles seamlessly — critical for the world-anchored UVs in `standard.vert`).

#### `initializeTextures` (`:82–130`)
Generates all texture names and assigns formats (`:95–106`) — memorise this format map, it is load-bearing for the image bindings later:

| Texture | Format |
|---|---|
| `m_gaussianNoiseTexture`, `m_initialSpectrumTexture` | `GL_RG32F` |
| `m_phaseTextures[0/1]` | `GL_R32F` |
| `m_spectrumTextureA`, `m_intermediateTextureA`, `m_spatialTextureA`, `m_displacementTexture` | `GL_RGBA32F` |
| `m_spectrumTextureB`, `m_intermediateTextureB`, `m_spatialTextureB` | `GL_RG32F` |
| `m_normalTexture` | `GL_RGBA16F` |

The `A` textures are `RGBA32F` because they pack **two** complex sequences (`dx` in `.xy`, height `h` in `.zw`); the `B` textures are `RG32F` because they carry just the third complex sequence (`dz` in `.xy`). The output normal is `RGBA16F` — half precision is plenty for a unit normal + foam and halves bandwidth.

It seeds the **initial phase** (`:108–118`): a `std::mt19937` RNG with the fixed seed `2026u`, uniform on `[0, 2π)`, written into *both* phase textures via `glTexSubImage2D(..., GL_RED, GL_FLOAT, ...)`. Fixed seed → deterministic ocean every run.

Then it sizes the CPU mirror `m_cpuDisplacement` to N×N (`:120`) and creates the two **PBOs** (`:123–128`) with `glBufferData(GL_PIXEL_PACK_BUFFER, dispBytes, nullptr, GL_STREAM_READ)` where `dispBytes = N*N*4*sizeof(float)` — i.e. one `RGBA32F` texel per grid point.

#### `initializeNoiseTexture` (`:132–154`)
Fills `m_gaussianNoiseTexture` with N×N pairs of independent **Gaussian** N(0,1) samples (`std::mt19937` seed `1337u`, `std::normal_distribution`). This is the random part of the wave field: in the Tessendorf formulation the complex amplitude is `h0 = (1/√2)(ξr + iξi)√(spectrum)`, and `ξr, ξi` are exactly these two Gaussians.

#### `buildInitialSpectrum` (`:156–172`)
Runs the one-time spectrum pass. Workgroup count `workgroups = (N + 15) / 16` (`:157`) because the shader's local size is 16×16. It sets uniforms `resolution`, `oceanSize`, `windSpeed`, `windDirection`, `OMEGA` (= `m_seaMaturity`), `SPECTRUM_BOOST` (= `m_amplitude`), binds the noise texture to sampler unit 0, binds `m_initialSpectrumTexture` as **image unit 0** (`GL_WRITE_ONLY, GL_RG32F`), dispatches `workgroups×workgroups×1`, and issues a memory barrier `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT` so subsequent passes see the writes. Called again whenever a spectrum knob changes.

#### Knob setters (`:174–194`)
- `setWindSpeed` clamps to `[2, 30]` m/s (`:175`) then rebuilds the spectrum.
- `setWindAngle` recomputes `m_windDirection` and rebuilds (`:179–184`).
- `setSeaMaturity` clamps to `[0.84, 5.0]` (`:187`) — `0.84` is fully-developed sea (Pierson–Moskowitz limit); higher = younger/choppier. Rebuilds.
- `setAmplitude` clamps to `≥ 0` (`:192`). Rebuilds.

#### `update(float deltaTime)` — the per-frame pipeline (`:196–308`)
This is the heart of the quarter. Five stages run in order, each followed by a `glMemoryBarrier`.

**Stage 1 — Phase evolution (`:200–211`).** Binds `m_phaseTextures[m_currentPhaseIndex]` to sampler 0 as `currentPhaseTexture`, binds the *other* phase texture as image unit 0 (`GL_R32F`), sets `resolution`/`oceanSize`/`deltaTime`/`timeScale`, dispatches 16×16-group grid. Then flips `m_currentPhaseIndex = nextPhaseIndex` (`:211`). This is a classic **double-buffer**: never read and write the same texture in one pass.

**Stage 2 — Spectrum assembly (`:213–226`).** Binds `m_initialSpectrumTexture` (sampler 0) and the freshly-written phase texture (sampler 1), sets `choppiness`, binds `m_spectrumTextureA` (image 0, `RGBA32F`) and `m_spectrumTextureB` (image 1, `RG32F`) as outputs, dispatches. Produces the frequency-domain field `h(k,t)` plus the choppiness shifts `dx`/`dz`.

**Stage 3 — Stockham IFFT, rows then columns (`:228–283`).** `halfN = N/2` because each butterfly thread handles two elements, so we launch N/2 threads per line.

The **horizontal (rows) loop** (`:235–257`): local size is `(256,1)`, so `groupsXh = (halfN + 255)/256`. It starts reading from the spectrum textures and bounces between the `intermediate*` and `spatial*` pairs. The loop variable `p` (the uniform `subseqCount`) runs `1, 2, 4, …, N/2` (`for (p = 1; p < N; p <<= 1)`). Each iteration:
1. sets `subseqCount = p`,
2. binds `srcA/srcB` as **image units 0/1 READ_ONLY**, `dstA/dstB` as **image units 2/3 WRITE_ONLY**,
3. dispatches `glDispatchCompute(groupsXh, N, 1)` (one group-row per image row),
4. barriers with `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`,
5. ping-pongs: `dst` becomes the next `src`, and `dst` flips to whichever of the two pairs it *isn't* currently using (`:251–253`).

After the loop the result is recorded in `m_fftRowResultA/B` (`:256`).

The **vertical (columns) loop** (`:260–281`): local size `(1,256)`, `groupsYv = (halfN + 255)/256`, dispatch `glDispatchCompute(N, groupsYv, 1)`. It first picks a destination pair *distinct* from the row result (`:265–266`), then runs the same `subseqCount` ladder, ping-ponging by swapping `src`/`dst` each iteration (`:276–277`). Result recorded in `m_fftFinalA/B` (`:280`).

> **Why CPU-driven `log2(N)` passes instead of one shared-memory pass?** A single-workgroup
> shared-memory FFT is capped at the workgroup size (≈256 elements), so it can't do 512 or 1024.
> Stockham bounces between two global textures across separate dispatches, so there is no shared-memory
> limit — any power-of-two up to 1024 works. The cost is more dispatches + barriers, but each is fully
> parallel. (See the comment block at the top of `fft_horizontal.comp`.)

A final combined barrier `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT` (`:283`) guards the transition from image writes to texture sampling.

**Stage 4 — Displacement assembly (`:285–297`).** Binds `m_fftFinalA` (sampler 0, `spatialTextureA`) and `m_fftFinalB` (sampler 1, `spatialTextureB`) — *"wherever the ping-pong landed"* (`:290`) — sets `heightScale`/`horizontalScale`, binds `m_displacementTexture` as image 0 (`RGBA32F`), dispatches. Note it samples from the *recorded handles*, not a fixed texture name — this is why the bookkeeping members exist.

**Stage 5 — Normals + foam (`:299–307`).** Binds `m_displacementTexture` (sampler 0), sets `oceanSize`, binds `m_normalTexture` (image 0, `RGBA16F`), dispatches.

#### `readbackDisplacement()` — async CPU readback (`:310–342`)
This is a **two-PBO ping-pong** that avoids a stall. A plain synchronous `glGetTexImage` would block the CPU until the in-flight FFT dispatches finish (~15ms), dropping the app to ~55fps. Instead:
1. `glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT)` (`:321`).
2. Kick off **this** frame's copy: bind `m_displacementTexture`, bind `m_pbo[m_pboIndex]` as the pack buffer, and `glGetTexImage(..., nullptr)` — with a PBO bound, the destination is the buffer and the call returns immediately (`:324–326`).
3. Map **last** frame's PBO (`prev = m_pboIndex ^ 1`), which finished filling already, with `glMapBufferRange(..., GL_MAP_READ_BIT)`, `memcpy` into `m_cpuDisplacement`, unmap (`:329–337`). Guarded by `if (m_readbackCount > 0)` so the first frame doesn't read an empty PBO.
4. Flip `m_pboIndex = prev` and increment `m_readbackCount` (`:340–341`).

Consequence: the CPU heightfield is **one frame stale** — negligible for buoyancy.

#### `sampleDisplacement(worldX, worldZ)` (`:344–367`)
Bilinearly samples the CPU mirror **exactly matching the GPU**. It maps world XZ → UV with `u = worldX/oceanSize + 0.5`, `v = worldZ/oceanSize + 0.5` (identical to `standard.vert`'s `OceanUV`), then converts to texel space with the half-texel offset `fx = u*N - 0.5` (because GL texel centres sit at half-integers). It floors to `(x0i, y0i)`, computes fractional weights `tx, ty`, wraps all four corner indices with a `GL_REPEAT`-equivalent lambda `wrap(i) = ((i % N) + N) % N` (`:357`), and does the standard 2D `glm::mix` lerp (`:364–366`). Returns the `vec3` `(dx, height, dz)`.

#### `sampleSurfaceHeight(worldX, worldZ)` (`:369–381`)
Solves the **inverse displacement problem**. The vertex shader maps a grid point `g` to world position `(g + disp.xz, disp.y)`, so the surface point you *see* at world (X,Z) actually originated at some other grid point `g`. To find the height there we must solve `g + disp_xz(g) = (X,Z)`. It uses **fixed-point iteration** `g ← (X,Z) − disp_xz(g)`, four iterations (`:375–379`), which converges because the displacement is small relative to the wavelength. Returns `sampleDisplacement(g).y`. This is what keeps boats sitting *on* the wave rather than next to it, and the error it removes grows with wave size and choppiness.

---

### `src/ocean/OceanMesh.h` / `OceanMesh.cpp` — the flat grid we displace

`OceanMesh(int resolution, float tileSize)` builds a flat grid of `(resolution+1)²` vertices centred on the origin. `getWorldSize()` returns `resolution * tileSize` (the world side length, used as `oceanSize`).

**Construction (`OceanMesh.cpp:7–56`):**
- `verts = resolution + 1`, `half = resolution * tileSize * 0.5f` (`:11–12`).
- Vertex positions (`:16–22`): a double loop emits `(x*tileSize - half, 0, z*tileSize - half)` — i.e. **only XZ, y = 0**; all the bumps come later from the displacement texture in the shader.
- Indices (`:24–35`): two triangles per quad, winding `tl,bl,tr` / `tr,bl,br`. This winding is *coupled* to the normal shader's assumption that all four quads contribute +Y for a flat surface.
- GL setup (`:39–55`): one VAO, a `GL_STATIC_DRAW` VBO of positions, a `GL_STATIC_DRAW` EBO of indices. Crucially **only attribute location 0 (position) is in the VBO** — there are no per-vertex normals stored (`:51–53`).

**`draw(const Shader& shader)` (`OceanMesh.cpp:64–77`):**
- Sets `model = identity` — the ocean is **fixed at the world origin**, it does *not* follow the camera, so it stays inside the ring of coastal cliffs (`:65–69`).
- Provides the missing normal attribute via `glVertexAttrib3f(1, 0, 1, 0)` (`:72`) — an "attributeless" constant flat up-normal supplied to location 1 for every vertex. This is why `standard.vert` reads `aNormal` even though it's not in the VBO. (This same shader is shared with solid objects in Q3, which *do* have real normals.)
- `glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr)`.

---

### `assets/shaders/fft_initial_spectrum.comp` — the oceanographic spectrum

Local size 16×16. Output image `binding=0, rg32f initialSpectrumTexture` (write-only). Inputs: `sampler2D gaussianNoiseTexture` and uniforms `resolution`, `oceanSize`, `windSpeed`, `windDirection`, `OMEGA`, `SPECTRUM_BOOST`. Constants: `PI`, `GRAVITY = 9.81`, `KILOMETERS = 370.0` (the capillary-wave scale `k_m`), `CAPILLARY_WAVE_SPEED = 0.23`.

This is an **Elfouhaily/JONSWAP-style unified directional spectrum** (the `achalpandeyy/OceanFFT` model). It runs once and produces the frozen complex amplitude `h0(k)`.

- **`waveVector(coord)` (`:22–26`):** maps a texel to a signed wave vector. The signed index trick `n = coord.x < N/2 ? coord.x : coord.x - N` puts the **zero frequency at the corner** (standard FFT layout, "fftshift" baked into indexing). Then `k = 2π·(n,m)/L` where `L = oceanSize`.
- **`dispersion(waveNumber)` (`:28–34`):** the deep-water gravity-capillary dispersion relation
  `ω(k) = sqrt( g·k·(1 + (k/k_m)²) )` with `k_m = KILOMETERS`. The `(k/k_m)²` term adds surface-tension (capillary) stiffening at short wavelengths. Guards `k < 1e-6 → 0`.
- **`main()` (`:36–84`):** computes the spectrum `S(k)` directionally:
  - `kp = g·(OMEGA/windSpeed)²` is the **peak wavenumber**; `cp = dispersion(kp)/kp` its phase speed. `OMEGA` is the **inverse wave age / sea maturity**.
  - `z0`, `uStar` model the wind drag / friction velocity over the sea surface (`:51–52`).
  - `c = dispersion(k)/k` is the phase speed at this `k`.
  - `lpm = exp(-1.25·(kp/k)²)` is the **Pierson–Moskowitz** low-frequency shape.
  - `jp = gamma^gammaExponent` with `gamma = 1.7` is the **JONSWAP peak enhancement**; `sigma` widens with maturity.
  - `bl` (long/gravity waves) and `bh` (short/capillary waves) are the two spectral curvature branches, combined (`:63,69`).
  - `delta` + `cosPhi` build the **directional spreading**: `cosPhi = dot(normalize(k), normalize(windDirection))`, and the spread factor is `(1 + delta·(2cos²φ − 1))` (`:73–76`) — waves prefer to travel along the wind.
  - Final isotropic-times-directional spectrum (`:76`):
    `spectrum = (1/2π)·k⁻⁴·(bl+bh)·(1 + delta·(2cos²φ − 1))`, clamped `≥ 0`.
  - **No high-frequency cutoff** (comment `:78–79`): at res 512 the grid carries the crisp short capillary waves without aliasing.
  - **Amplitude (`:81–83`):** `h0 = gaussian · sqrt(spectrum·0.5)·dk·SPECTRUM_BOOST` where `dk = 2π/L` is the wavenumber cell size and `gaussian` is the two-component N(0,1) noise. The `sqrt(·0.5)` is the Tessendorf `1/√2` factor. Stored as `(h0.x, h0.y, 0, 0)` in `RG32F`.

#### Cross-check: `fft_phase.comp` (`:1–40`)
Local size 16×16. Output `binding=0, r32f nextPhaseTexture`; input `sampler2D currentPhaseTexture`; uniforms `resolution`, `oceanSize`, `deltaTime`, `timeScale`. Re-declares the same `waveVector` and `dispersion` (note: written as `(waveNumber*waveNumber)/(KILOMETERS*KILOMETERS)` here vs `square(...)` in the spectrum shader — mathematically identical). `main()` advances the phase: `nextPhase = mod(currentPhase + ω(k)·deltaTime·timeScale, 2π)` (`:37`). Each wave's phase rotates at its own angular frequency ω; wrapping at 2π keeps it bounded. This is the only time-dependent state in the whole spectral chain.

#### `fft_spectrum.comp` — frequency-domain wave field (`:1–56`)
Local size 16×16. Outputs `binding=0, rgba32f spectrumTextureA` and `binding=1, rg32f spectrumTextureB`; inputs `initialSpectrumTexture`, `phaseTexture`, uniforms `resolution`, `oceanSize`, `choppiness`. Helpers `complexMul(a,b) = (a.x·b.x − a.y·b.y, a.y·b.x + a.x·b.y)` (`:14–16`) and `complexConjugate(v) = (v.x, −v.y)` (`:18–20`).

This builds the **time-evolved Hermitian wave field**:
- `mirrored = ((N − coord.x) % N, (N − coord.y) % N)` (`:34`) is the `−k` index.
- `h0 = initialSpectrum(coord)`, `h0Mirror = conj(initialSpectrum(mirrored))` (`:35–36`).
- `phase` is read; `phasePositive = (cos φ, sin φ) = e^{iφ}` and `phaseNegative = conj = e^{−iφ}` (`:38–39`).
- The assembled field (`:40`):
  `h(k,t) = h0(k)·e^{iω t} + conj(h0(−k))·e^{−iω t}` — this enforces Hermitian symmetry so the inverse FFT yields a **real** heightfield (no imaginary residue). The phase `φ = ω·t` is exactly what the phase texture accumulated.
- **Choppiness** (`:50–51`): the horizontal displacement is `dx = h·(0, −kx/|k|·choppiness)` and `dz = h·(0, −ky/|k|·choppiness)`. Multiplying by `(0, −1)` is the `−i` factor that, after the IFFT, produces the gradient that pinches crests sideways (Tessendorf choppy waves). At `k ≈ 0` it writes zeros to avoid division blow-up (`:44–47`).
- Stored: `spectrumTextureA = (dx.xy, h.xy)`, `spectrumTextureB = (dz.xy, 0, 0)` (`:53–54`) — **three complex sequences packed into the A/B layout**.

---

### `assets/shaders/fft_horizontal.comp` & `fft_vertical.comp` — the Stockham radix-2 IFFT

These two are near-identical, transposed. Each computes **one butterfly pass** of a 2D inverse FFT by running 1D FFTs along rows (horizontal) then columns (vertical). The CPU calls each `log2(N)` times with an increasing `subseqCount`.

**Bindings (both):** `binding=0 rgba32f inputA` (READ_ONLY), `binding=1 rg32f inputB` (READ_ONLY), `binding=2 rgba32f outputA` (WRITE_ONLY), `binding=3 rg32f outputB` (WRITE_ONLY). Uniforms `resolution`, `subseqCount`.

**Layout reminder** (comment, `fft_horizontal.comp:10–14`): Texture A `.xy = dx`, `.zw = h`; Texture B `.xy = dz`. All three complex sequences are transformed together in lockstep — one butterfly does all three.

**Horizontal local size** `(256,1)`; **vertical** `(1,256)`.

**The butterfly (`fft_horizontal.comp:33–65`):**
- `threadIdx` = butterfly index within the line `[0, N/2)`, `rowIndex` = which row; `halfN = resolution >> 1`. Bounds-checked (`:37`).
- **Stockham index math** (`:39–40`):
  - `inIdx = threadIdx & (subseqCount − 1)` — the position within the current sub-sequence (cheap modulo because `subseqCount` is a power of two).
  - `outIdx = ((threadIdx − inIdx) << 1) + inIdx` — the de-interleaved output position. This is the Stockham auto-sort: it writes outputs into natural order each pass, so **no separate bit-reversal step is needed** (the key advantage over Cooley–Tukey for GPUs — no scattered memory access).
- **Twiddle factor** (`:42–43`): `angle = −π · (inIdx / subseqCount)`, `twiddle = (cos, sin) = e^{iangle}`. The negative sign makes this an **inverse** transform (synthesis: frequency → space).
- **Two inputs N/2 apart** (`:46–49`): reads element `threadIdx` and `threadIdx + halfN` on this row, for all three sequences.
- **Butterfly** (`:52–59`): `t = twiddle · b`, then `out_lo = a + t`, `out_hi = a − t`, applied to `dx`, `h`, `dz`.
- **Scatter writes** (`:61–64`): low half to `outIdx`, high half to `outIdx + subseqCount`.

`fft_vertical.comp` is the same with `colIndex`/`threadIdx` swapped and the two samples spaced `halfN` apart **vertically** (`ivec2(colIndex, threadIdx)` and `ivec2(colIndex, threadIdx + halfN)`), writing to `ivec2(colIndex, outIdx)` / `ivec2(colIndex, outIdx + subseqCount)`.

> **Gotcha — no per-pass normalisation.** Neither pass divides by anything. The full `1/N` factor is
> applied exactly once, later, in `fft_displacement.comp`.

---

### `assets/shaders/fft_displacement.comp` — normalise & pack (`:1–29`)

Local size 16×16. Output `binding=0, rgba32f displacementTexture`; inputs `spatialTextureA`, `spatialTextureB`, uniforms `resolution`, `heightScale`, `horizontalScale`. Computes `invN = 1/N` (`:19`) — the single normalisation for the whole Stockham transform. Reads `spatialA` (`.x = dx`, `.z = height`) and `spatialB` (`.x = dz`), then writes:

```
displacement = ( spatialA.x · horizontalScale · invN,
                 spatialA.z · heightScale     · invN,
                 spatialB.x · horizontalScale · invN )
```

stored as `(dx, height, dz, 1)`. So the `.y` channel is the wave **height**, `.x/.z` are the **choppy horizontal shifts**. `heightScale`/`horizontalScale` are the artist dials on top of the physically-derived amplitude.

---

### `assets/shaders/fft_normal.comp` — finite-difference normals + foam (`:1–54`)

Local size 16×16. Output `binding=0, rgba16f normalTexture`; input `sampler2D displacementTexture`; uniforms `resolution`, `oceanSize`. Constant `FOAM_GAIN = 2.5`.

- **`wrapCoord`** (`:11–16`) wraps a texel index into `[0,N)` for seamless tiling at edges.
- **`sampleDisplacedPosition(coord)`** (`:18–23`) reconstructs the **actual world-space position** of a texel: the base grid position is `baseXZ = ((coord/N) − 0.5)·oceanSize`, and the displaced position is `(baseXZ.x + disp.x, disp.y, baseXZ.y + disp.z)`. Normals must be computed from the *displaced* positions, not the flat grid, or choppy crests would shade wrong.
- **`main()` (`:25–53`):** samples the centre and four cardinal neighbours, forms the four edge vectors `right/left/fwd/back` (`:32–35`; note `fwd` is `−z`), and computes the normal as the normalised sum of four cross products `cross(right,fwd)+cross(fwd,left)+cross(left,back)+cross(back,right)` (`:38–43`). Averaging four quad normals is more robust than a single cross product and matches the mesh winding.
- **Jacobian / foam (`:45–51`):** the Jacobian of the horizontal displacement map is
  `J = (1 + ∂dx/∂x)(1 + ∂dz/∂z) − (∂dx/∂z)(∂dz/∂x)`, evaluated by finite differences as `(right.x·back.z − back.x·right.z)/texelSize²` where `texelSize = oceanSize/N`. When **`J < 0` the surface folds over itself** — that's a breaking crest, so `foam = clamp(−J·FOAM_GAIN, 0, 1)`. Stored in the normal texture's alpha. This is the only source of whitecaps.
- Output: `vec4(normal, foam)` in `RGBA16F`.

---

### `assets/shaders/standard.vert` — vertex displacement (`:1–48`)

Inputs: `aPos` (loc 0), `aNormal` (loc 1, the constant `(0,1,0)` from `OceanMesh::draw`). Outputs: `FragPos`, `PhysicsNormal`, `OceanUV`, `DisturbUV`, `SurfaceMask`. Uniforms include `model/view/projection`, `displacementMap`, `disturbanceMap`, `oceanSize`, `floorY`, `applyOceanDisplacement`.

- `PhysicsNormal = normalize(aNormal)` (`:21`) — pass-through (matters for shared use with solid objects).
- `worldPos = model · aPos` (`:24`).
- **`OceanUV = worldPos.xz / oceanSize + 0.5`** (`:30`) — world-anchored, one FFT patch per `oceanSize`, *continuous* (no `fract`) so `GL_REPEAT` tiles cleanly without a seam. **This is the exact mapping `sampleDisplacement` mirrors on the CPU.**
- **`DisturbUV = clamp(worldPos.xz/oceanSize + 0.5, 0, 1)`** (`:33`) — non-tiling, for the Q4 disturbance map.
- **`SurfaceMask`** (`:35`): 1 only if `applyOceanDisplacement != 0` AND `aPos.y > floorY + 1.0`. This lets the *same* shader draw both displaced water (mask=1) and static geometry/floor (mask=0).
- **Displacement (`:37–44`):** if mask>0.5, sample `disp = texture(displacementMap, OceanUV).xyz` and add it to `worldPos` (full 3D: XZ choppiness + Y height), then add the Q4 disturbance height `textureLod(disturbanceMap, DisturbUV, 0).r` to `worldPos.y`.
- `gl_Position = projection · view · worldPos` (`:47`).

---

### `assets/shaders/standard.frag` — HDR deep-ocean shading (`:1–219`)

The big one. Composites a physically-based deep ocean entirely in HDR (sky values well above 1.0) and tonemaps at the end. Fresnel drives the contrast that makes each wave read sharply.

**Inputs / uniforms (`:4–37`):** the varyings from the vertex shader; `viewPos`; `samplerCube skybox`; `sampler2D normalMap` (our Q2 normal texture, unit 2); `sampler2D disturbanceMap` (Q4, unit 3); `oceanSize`; ripple controls `numRipples`, `uRippleLifetime`, `uRingSpeed`, `uRingStrength`; `time`; and a block of UI-tunable look knobs (`uDeepColor`, `uShallowColor`, `uDepthFalloff`, `uReflectStrength`, `uHorizonColor`, `uScatterColor`, `uSunColor`, `uSunDir`, `uSunGlint`, `uSunGlitter`, `uExposure`, `uMidWaveDetail`).

**SSBO (`:21–23`):** `layout(std430, binding = 4) readonly buffer RippleBuffer { vec4 ripples[]; }` — Q4's rain ripples. Using an SSBO instead of a uniform array sidesteps the constant-register limit (can hold up to ~512 ripples).

**Constants (`:41–42`):** `TWO_PI = 6.2831853`, `PI = 3.14159265` (digits preserved from the originals).

- **`applyMediumWaveDetail(baseNormal, worldXZ)` (`:45–56`):** adds procedural mid-frequency normal detail the FFT grid can't resolve — a `warp` term plus four directional `cos` slope waves with wavelengths 10.5/6.3/3.7/18.0 m (`TWO_PI / λ`), scaled by `uMidWaveDetail`. Perturbs the normal's X/Z. This is what keeps the sea looking detailed between FFT texels.

**`main()`:**
1. **Normals (`:59–86`):** if `SurfaceMask > 0.5`, sample `normalMap` at `OceanUV` → `oceanNormal` (`.xyz`) and `foam` (`.a`). Add a physics offset (`PhysicsNormal − (0,1,0)`, zero for flat water), apply medium-wave detail, then **disturbance normal perturbation**: with `kDisturbResolution = 256.0` (`:74`, the comment notes it **must match `GPUDisturbance(256u, …)` in main.cpp**), central-difference the disturbance height into `slopeX/slopeZ` over `kTexelWorld = oceanSize/256` metres and subtract `slope·2.5` from the normal's X/Z (`:77–85`).
2. **Rain ripple rings (`:88–143`):** each ripple is `xy = world centre`, `z = age`. Constants: `kRingWidth = 0.35` m, `kBurst = 0.1` m, `kGap = 0.5` m, four ring amplitudes `kAmp = {0.015, 0.030, 0.050, 0.070}` (outer rings stronger), `kExpandTime = 0.7` s. A **squared-distance early-out** rejects ripples beyond `kRippleMax = kBurst + uRingSpeed·kExpandTime + 3·kGap + kRingWidth` (`:106–110`) before any `sqrt`/`sin` — mathematically exact (skipped ripples contribute 0) but turns O(512·4) per pixel into "only nearby ripples". A second cull skips ripples when the fragment is >350 m from the camera (`:114–115`). For surviving ripples it computes `reach = kMaxReach·(1 − e^{−age/kExpandTime})`, and for each of four concentric rings adds a `sin((dist − r)·(π/kRingWidth))` normal bump faded by ring lifetime (`:118–142`).
3. **Lighting** (`:146–218`), the `achalpandeyy/OceanFFT` HDR-fresnel model:
   - `viewDir`, `incident`, `NdotV` (`:156–158`).
   - **Schlick fresnel** with water `R0 = 0.02`: `fresnel = 0.02 + 0.98·(1 − NdotV)⁵` (`:161`).
   - **Reflection (`:163–172`):** `R = reflect(incident, finalNormal)`, sample the skybox cubemap × `uReflectStrength` (boost LDR cube to HDR), and blend toward `uHorizonColor` when `R.y` dips below the horizon (`belowHorizon = smoothstep(0, −0.2, R.y)`) so grazing rays never sample the dark cube underside.
   - **Refraction (`:174–181`):** depth cue `depthCue = clamp((FragPos.y + 2.5)·uDepthFalloff, 0, 1)`, `bodyColor = mix(uDeepColor, uShallowColor, depthCue)`, lifted by a half-Lambert sun diffuse `(0.5 + 0.5·diffuse)`.
   - **Subsurface scatter (`:183–190`):** the signature backlit-crest glow. `crest = clamp((FragPos.y − 0.1)·0.4, 0, 1)`, `backlight = max(0, dot(viewDir, −uSunDir))⁴`, `sideLight = max(0, dot(N, uSunDir))·0.5 + 0.5`, `scatter = uScatterColor·crest·backlight·sideLight·2.2`.
   - **Compose (`:192–195`):** `underwater = bodyColor + scatter`, then `color = mix(underwater, reflection, reflectStrength)` where `reflectStrength = fresnel` on water (mask>0.5) else a flat `0.15`.
   - **Sun specular (`:197–202`):** Blinn-Phong half-vector `NdotH`; a tight `glint = NdotH^1200` (mirror sun disc) and a broad `glitter = NdotH^120` (sparkle over chop), added as `uSunColor·(glint·uSunGlint + glitter·uSunGlitter)`.
   - **Foam (`:204–208`):** `foamMask = smoothstep(0.05, 0.55, foam)`, `foamCol = vec3(0.95,0.98,1.02)·(0.7+0.3·diffuse)`, `color = mix(color, foamCol, foamMask·0.85)`.
   - **Distance haze (`:210–213`):** `haze = smoothstep(500, 1700, dist)`, blends far water toward `uHorizonColor·0.55` so the sea melts into the sky at the horizon.
   - **HDR tonemap (`:215–216`):** exposure tonemap `finalResult = 1 − exp(−color·uExposure)` — this (not a clamp) gives the crisp high-contrast look. Output `vec4(finalResult, 1)`.

---

## ⚠️ Gotchas & invariants

- **Texture format ↔ image-binding coupling.** Every `glBindImageTexture` format in `update()` must match the texture's `glTexStorage2D` format and the shader's `layout(..., format)`. The A textures are `RGBA32F` (pack `dx`+`h`), B textures `RG32F` (pack `dz`). The normal output is `RGBA16F`. Mismatch = undefined image access.
- **The FFT result lands in an unpredictable pair.** Because there are `log2(N)` ping-pong passes, the final textures may be the `intermediate*` *or* `spatial*` pair. Never hardcode `m_spatialTextureA/B` as the displacement input — always read from the recorded `m_fftFinalA/B` (and `m_fftRowResultA/B`). This is exactly why those bookkeeping members exist.
- **One `1/N` only.** Stockham doesn't normalise per pass; the single `invN = 1/resolution` lives in `fft_displacement.comp`. If you ever add per-pass scaling you must remove it there, or amplitudes will be wrong by a factor of N.
- **`subseqCount` ladder.** Each axis runs `p = 1,2,4,…,N/2` (`for (p=1; p<N; p<<=1)`). It is a uniform, set per pass. Skipping a value or going to `N` corrupts the transform.
- **Phase double-buffer.** Stage 1 reads `m_phaseTextures[current]` and writes the other, then flips `m_currentPhaseIndex`. Reading and writing the same phase texture in one pass is a data race.
- **Memory barriers between every stage.** Each compute dispatch is followed by `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` (and `GL_TEXTURE_FETCH_BARRIER_BIT` where the next stage *samples* rather than image-loads). Drop one and the next pass may read stale data.
- **`update()` per substep, `readbackDisplacement()` once per frame.** Q1 calls `update(dt)` inside the fixed-step loop but must call `readbackDisplacement()` exactly once per render frame after the loop — calling it per substep would map the PBO redundantly and re-introduce the stall the double-buffer exists to avoid.
- **Readback is one frame stale.** `m_cpuDisplacement` reflects the *previous* frame's displacement texture. Fine for buoyancy; do not assume it's the current frame's heights.
- **CPU sampling must mirror the GPU exactly.** `sampleDisplacement` replicates `standard.vert`'s `OceanUV = worldXZ/oceanSize + 0.5`, GL bilinear (the `−0.5` half-texel offset), and `GL_REPEAT` wrap. If you change the UV mapping in the vertex shader, you must change `sampleDisplacement` in lockstep or boats will float beside the waves.
- **`kDisturbResolution = 256.0` in `standard.frag:74` must equal Q4's `GPUDisturbance(256u, …)`** in `main.cpp`. They are independently hardcoded; changing one without the other mis-scales the disturbance normal slope.
- **Mesh winding ↔ normal shader.** `OceanMesh`'s triangle winding and the four-cross-product sum in `fft_normal.comp` together assume a flat surface yields +Y. Change one and normals flip.
- **`SurfaceMask` gate.** Both `standard.vert` and `standard.frag` only apply ocean displacement/shading when `applyOceanDisplacement != 0` and the vertex is above `floorY + 1.0`. The shader is shared with non-water geometry, so this gate is what keeps the floor flat.
- **Spectrum knobs trigger a rebuild; look knobs don't.** `setWindSpeed/WindAngle/SeaMaturity/Amplitude` call `buildInitialSpectrum()` (a full GPU dispatch); `setHeightScale/HorizontalScale/Choppiness/TimeScale` are free per-frame uniforms. Clamp ranges (`windSpeed [2,30]`, `seaMaturity [0.84,5.0]`) are physical limits — don't widen blindly.
- **Fixed RNG seeds (`2026u` phase, `1337u` noise)** make the ocean deterministic. Changing them changes the exact wave pattern.

---

## 🧠 Mental-model recap

- The ocean is **statistics → shape**: an oceanographic spectrum describes how much wave energy lives at each wave size/direction; the inverse FFT turns that into an actual moving heightfield, every frame.
- **Three textures, three jobs:** displacement (where water moves), normal+foam (which way it faces + whitecaps), and a CPU height mirror (so physics can ask "how high is the water here?").
- **Only one thing is time-dependent:** the per-wave phase, advanced by the dispersion relation `ω = sqrt(g·k·(1+(k/k_m)²))`. Everything downstream is a deterministic transform of that phase + the frozen `h0`.
- **Hermitian assembly** `h = h0·e^{iωt} + conj(h0(−k))·e^{−iωt}` guarantees the IFFT output is real; the extra `−i·k/|k|` factors produce the choppy sideways crest pinch.
- **Stockham over shared-memory FFT** because shared-memory FFTs cap at the workgroup size (~256). Stockham ping-pongs two global textures across `log2(N)` CPU-driven passes — no size cap, auto-sorted output, no bit-reversal step.
- **Normalise once (1/N) at the end**, not per pass.
- The visible grid (`OceanMesh`) is **flat at y=0**, fixed at the origin; all the bumps are added in the vertex shader from the displacement texture.
- The look is **HDR fresnel**: dark ocean body where you face the water, bright sky reflection at grazing angles, plus scatter/glint/foam, all tonemapped with `1 − exp(−color·exposure)`.
- **Readback is async + one-frame-stale** via a two-PBO ping-pong, to avoid a CPU stall that costs ~15ms.
- **Choppiness breaks the 1:1 grid→world mapping**, so `sampleSurfaceHeight` inverts it with fixed-point iteration to keep boats on the wave.

---

## 📖 Glossary

- **Spectrum (ocean):** a function giving wave energy/amplitude per wave size (wavenumber) and direction; here a JONSWAP/Elfouhaily directional model.
- **JONSWAP / Pierson–Moskowitz:** empirical ocean wave spectra; PM is the fully-developed-sea limit, JONSWAP adds a peak-enhancement factor for younger seas.
- **Wave vector `k` / wavenumber `|k|`:** the 2D spatial frequency of a wave; `|k| = 2π/wavelength`. Computed as `2π·(n,m)/L`.
- **Dispersion relation:** ties a wave's spatial frequency to its temporal frequency ω; deep-water gravity-capillary form `ω = sqrt(g·k·(1+(k/k_m)²))`.
- **FFT / IFFT:** Fast Fourier Transform; converts between frequency-domain coefficients and a spatial signal. The inverse (IFFT) synthesises the heightfield from the spectrum.
- **Stockham FFT:** a self-sorting FFT formulation that writes outputs in natural order each pass, avoiding the separate bit-reversal step — ideal for GPUs (no scattered memory access).
- **Butterfly:** the radix-2 FFT primitive combining two inputs into two outputs `a±twiddle·b`; one thread per butterfly.
- **Twiddle factor:** the complex root-of-unity `e^{±iθ}` multiplied into a butterfly's second input.
- **Ping-pong (double buffer):** alternating two buffers so a pass reads one and writes the other, never both at once.
- **Choppiness:** horizontal displacement of the surface (the `−i·k/|k|` term) that sharpens crests and flattens troughs into a realistic non-sinusoidal sea.
- **Jacobian:** determinant of the displacement field's spatial derivative; `J < 0` means the surface folds over itself → a breaking wave / foam.
- **Foam / whitecap:** white water generated where the Jacobian goes negative, stored in the normal texture's alpha.
- **Fresnel (Schlick):** the angle-dependent reflectance approximation `R0 + (1−R0)(1−cosθ)⁵`; water uses `R0 = 0.02`.
- **Subsurface scatter:** light transmitted *through* a wave crest toward the viewer, giving the backlit glow on swell tops.
- **HDR / tonemap:** lighting computed at values above 1.0 (high dynamic range), then mapped to displayable range with `1 − exp(−color·exposure)`.
- **Compute shader:** a GPU program not tied to the raster pipeline; dispatched in workgroups, writes to image textures / buffers.
- **Workgroup / local size / dispatch:** a compute shader runs in groups of `local_size_*` threads; `glDispatchCompute(gx,gy,gz)` launches `gx·gy·gz` such groups.
- **Image texture / `glBindImageTexture`:** a texture bound for direct read/write inside a shader (vs sampled), with an explicit format and read/write qualifier.
- **Memory barrier (`glMemoryBarrier`):** forces prior GPU writes to be visible to subsequent reads of a given type (image access, texture fetch, texture update).
- **SSBO (Shader Storage Buffer Object):** a large, shader-readable/writable GPU buffer (`std430` layout); used here for the ripple array at binding 4.
- **PBO (Pixel Buffer Object):** a buffer used as the source/destination of pixel transfers; enables asynchronous, non-stalling texture readback to the CPU.
- **`GL_REPEAT` / bilinear:** texture wrap mode that tiles UVs, and the linear interpolation between the four nearest texels; the CPU sampler reproduces both.
- **Fixed-point iteration:** solving `g = f(g)` by repeatedly applying `g ← f(g)`; used to invert the choppiness displacement for `sampleSurfaceHeight`.
- **Half-Lambert:** a softened diffuse term `0.5 + 0.5·N·L` that keeps surfaces from going fully black on the shadow side.
