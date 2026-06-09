# RealWaterSimulator — Presentation Deck

> Slide-by-slide deck. Each `---` is a new slide. Render with any Markdown→slides tool
> (Marp, reveal.js, Slidev) or read top-to-bottom. Speaker notes in the bullets.
> Full detail: see `TECHNICAL_OVERVIEW.md`.

---

## Slide 1 — Title

# Real-Time Water Simulator
### GPU FFT Ocean · Buoyant Vehicles · Rain · Coastal Scene

**C++17 · OpenGL 4.6 (compute shaders)**
Libraries: GLFW · GLAD · GLM · STB · cgltf · Dear ImGui
Runs on Linux & Windows 11 · ~112 FPS (RTX 3090)

---

## Slide 2 — What we built

- A **physically-based ocean** simulated in real time with an **FFT** on the GPU
- **Rain** with splashes and spreading ripple rings
- **3 vehicles** (jet-ski, yacht, big-ship) that **float on the waves** and leave **wakes**
- A **coastal scene**: photoreal cliff ring + rock, dynamic sky
- A live **control panel** (ImGui) to tune everything: waves, rain, sun, vehicles

> One sentence: *"We add up thousands of real ocean waves on the GPU every frame, render them with
> film-style lighting, and float boats on them with real buoyancy."*

---

## Slide 3 — System architecture (one frame)

```
INPUT ─► PHYSICS (fixed 1/60s) ─► READBACK ─► BUOYANCY ─► RENDER
          │  rain particles                     │           │ ocean
          │  FFT ocean (GPU)                     │           │ rock + cliffs + boats
          │  disturbance/wakes (GPU)             │           │ skybox + rain
          │  boat steering                       └ heave/pitch/roll
```

**Folders:** `src/ocean` (FFT) · `src/water` (rain, buoyancy) · `src/graphics` (models, shaders) ·
`assets/shaders` (GLSL) · `assets/models` (glTF)

---

## Slide 4 — FFT Water: the idea

- The sea = **sum of thousands of sine waves** of different sizes & directions
- Wave **amplitudes** come from a real **wind-wave spectrum** (IFREMER/Elfouhaily)
- An **Inverse FFT** turns that spectrum into a **height field** every frame
- Plus **horizontal displacement** ("choppiness") to sharpen crests into points

**Look at:** `src/ocean/GPUFFTOcean.cpp` + `assets/shaders/fft_*.comp`

---

## Slide 5 — FFT Water: the GPU pipeline

```
initial spectrum  →  phase (time)  →  current spectrum (+choppy dx,dz)
       │                                      │
       └──────────►  Stockham IFFT (rows → cols, 18 passes)  ──────────►
                                              │
                          displacement (dx, height, dz)  →  normals + foam
```

- **Stockham FFT:** ping-pong across `log2(N)` passes → no shared-memory cap → **true 512**
- **Foam** = where the surface **folds over** (Jacobian < 0) → whitecaps

---

## Slide 6 — FFT Water: key numbers

| Quantity | Value |
|---|---|
| FFT resolution | **512 × 512** |
| Render mesh | **1024 × 1024 vertices** (~2M triangles) |
| Ocean patch | **1024 m** (1 m / vertex) |
| IFFT passes / frame | **18** (2 × log₂512) |
| Sim textures | 11 × 512² (~40 MB VRAM) |

**Dispersion:** `ω = √(g·k·(1 + (k/Km)²))` — physically-based wave speeds

---

## Slide 7 — FFT Water: the knobs (live UI)

| Parameter | Does what |
|---|---|
| **wind speed** (2–30 m/s) | calm ripples → storm swell |
| **wind direction** | wave travel direction |
| **wave height / choppiness** | amplitude / pointed crests |
| **sea maturity** (OMEGA) | young sea ↔ developed swell |
| **overall amplitude** | global wave energy |
| **time scale** | animation speed |

> Presets in code: Calm / Choppy / Storm

---

## Slide 8 — Rain

- **CPU particle system**, spawned **around the camera** (100 m disc — never the whole ocean)
- Drops fall + slant with **wind**, become bright **streaks**
- On impact → **splash jet** (water column) + **ripple ring** on the surface
- Ripple rings drawn by the **water shader** (SSBO of centres+ages)

**Optimizations:**
- **Per-pixel ripple early-out** — reject far ripples with a cheap distance test *before* the heavy
  math (the fix that unstuck weaker / Windows GPUs; **no visual change**)
- Fixed **512-ripple budget** → GPU cost constant even at 500 drops/frame
- One VBO + two draw calls; camera-local only

**Look at:** `src/water/RainSystem.cpp` + `assets/shaders/standard.frag` (ripple loop)

---

## Slide 9 — Sky

- **Cubemap skybox** — 6 textured faces, drawn at infinite depth (`gl_Position = pos.xyww`)
- Camera **rotation only** (no translation) → sky never moves with you
- Drawn **after** the scene with no depth write → **zero overdraw**
- **Reused as the reflection** for water + objects · **4 sky sets**, switchable live

**Look at:** `main.cpp` skybox block + `assets/shaders/skybox.*`

---

## Slide 10 — Vehicles: movement

- **Kinematic steering** (boids-style), not an engine sim
- Each boat blends 3 urges → desired heading:
  **boundary return** + **separation** (avoid other boats) + **rock avoidance**
- **Rate-limited turns** (gradual) + gentle **wander** + **banking** into turns
- **Hard safety net** → never overlaps boats / phases through cliffs or rock

**Look at:** `main.cpp` vehicle navigation block (~line 566)

---

## Slide 11 — Vehicles: buoyancy (water → boat)

- **Force-based 6-DOF**: hull = grid of **15 sample facets** (5 × 3)
- Sample wave height under each facet:
  - **Heave** = damped spring toward the **average** height
  - **Pitch / roll** = from the **surface slope** across the hull
- Long ships span **many wavelengths** → short chop **averages out** → big ships ride smoothly,
  small boats bob lively
- Tilt clamped to ±0.6 rad (anti-capsize)

**Look at:** `src/water/BoatPhysics.cpp`

---

## Slide 12 — Vehicles: wakes (boat → water)

- Moving boats inject a ripple at their **stern** every frame (amplitude ∝ speed)
- Goes into a **GPU wave-equation** field (the "disturbance" sim)
- Same system powers the **C-key** interactive splash
- **Foam** = the water shader's whitecaps (FFT Jacobian) — integrated, not separate particles

**Look at:** `src/ocean/GPUDisturbance.cpp` + `assets/shaders/disturbance_*.comp`

---

## Slide 13 — Disturbance / Wake sim (how)

- Solves the **2D wave equation** on a 256² height texture, **Verlet integration**:

```
next = (2·current − previous + waveC · ∇²) · damping
```

- `∇²` = 4-neighbour Laplacian · `damping ≈ 0.997` (ripples fade) · 3-texture ping-pong
- `disturb(pos, amp)` = inject a Gaussian bump → spreads + fades like a real ripple

---

## Slide 14 — Models: glTF (rocks, cliffs, boats)

- **glTF** = standard runtime 3D format: meshes + node hierarchy + **PBR materials**
- Loaded with **cgltf** (single-header parser) → `src/graphics/Model.cpp`
- **Walk the node tree & bake transforms** into vertices
  → critical for multi-part models (yacht = 23 meshes, ship = 62)
- **PBR shading:** albedo + normal (derivative TBN, no tangents) + roughness + AO,
  **sRGB → linear → gamma** encode

---

## Slide 15 — Rocks & Mountains

- **Rock:** Poly Haven marble-cliff scan (~107K tris, full PBR), procedural fallback exists
- **Mountains = coastal-cliff scans**, ringed around the bay (8 instances), faced inward,
  placed to **hide the water's edge** behind land
- Real scanned geometry → photoreal (procedural noise-terrain was tried & dropped)

**Look at:** `assets/models/` + `main.cpp` `mountains[]` + `object.frag`

---

## Slide 16 — Water shading (the look)

PBR ocean, composited in **HDR** then tonemapped:

- **Fresnel** drives everything (dark facing you, bright at grazing)
- **Reflection** (real skybox) + **Refraction** (depth-tinted body)
- **Subsurface scatter** (glow through backlit crests)
- **Sun glint + glitter** · **foam** · **distance haze**
- Tunable: deep/shallow colour, **sun azimuth/elevation**, exposure, reflection, scatter

**Look at:** `assets/shaders/standard.frag`

---

## Slide 17 — Performance

- **~112 FPS** @ RTX 3090, 1024×768
- Big wins:
  - **Stockham FFT** → enables true 512 (no shared-mem cap)
  - **Async PBO readback** of wave heights → no GPU stall for buoyancy
  - **Per-pixel ripple early-out** → was ~1.2 B iterations/frame; now only nearby ripples
    (fixed the "stuck" on weaker / Windows GPUs, no visual change)
  - **Rain budget** (fixed 512-ripple SSBO) → cost independent of intensity
  - **Camera-local rain**, **skybox depth-trick**, **batched rain draws**

---

## Slide 18 — Controls

| Key / UI | Action |
|---|---|
| Left-click | enter first-person look · **Esc** to release/quit |
| W A S D / Space / Shift | fly camera |
| **P** | pause / resume vehicles |
| **C** | fire interactive wave |
| K/L · N/M · arrows | wind · choppiness · height/time |
| **R** | record frames (→ ffmpeg → video) |
| ImGui panel | Rain · Water · Lighting · Vehicles (all parameters) |

---

## Slide 19 — Tech stack & numbers recap

- **OpenGL 4.6** compute (FFT, disturbance) + raster (scene)
- **512²** FFT · **1024²** mesh · **1024 m** ocean · **256²** wake field
- **6-DOF** buoyancy (15 facets/boat) · **glTF** PBR models · **cubemap** sky
- All vendored, builds with **CMake**, cross-platform

---

## Slide 20 — Summary

**We built a real-time, physically-grounded ocean world:**
- GPU **FFT** waves from a real spectrum
- **Buoyant** boats with wakes + steering
- **Rain**, **foam**, **dynamic sky & sun**, photoreal **coast**
- Fully **interactive & tunable** in real time

*Demo time.* 🌊
