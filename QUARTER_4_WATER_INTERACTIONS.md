# Quarter 4 — Dynamic Water Interactions & Procedural Geometry

## 📚 The four study quarters

This project's implementation is divided into **four roughly-equal study units**. Together they
cover **every** source and shader file in the project; each unit is written to be understood on
its own, and the four explanations together account for the whole implementation.

| Quarter | Theme (what you'd master) | Files it owns |
|---|---|---|
| **Q1 — Application Host & Frame Loop** | The program that drives everything: startup, the fixed-timestep render loop, input/camera control, the ImGui control panel, scene assembly, frame recording, and the build system. | `src/main.cpp`, `CMakeLists.txt`, `build_and_run.sh`, `.gitignore` |
| **Q2 — Ocean Spectral Simulation & Water Surface** | The signature feature: the spectral FFT ocean — ocean spectrum → GPU Stockham IFFT → displacement/normal/foam → the displaced, shaded water surface. | `src/ocean/GPUFFTOcean.*`, `src/ocean/OceanMesh.*`, the 7 `fft_*.comp` shaders, `standard.vert`, `standard.frag` |
| **Q3 — Rendering Engine, Camera & Scene Objects** | The reusable rendering toolkit (GL context/window, camera, shader programs, meshes, glTF model loading, textures) and how solid scene objects (boats, rock, cliffs) + the skybox are drawn. | `src/core/Window.*`, `src/core/Camera.*`, `src/graphics/{Shader,Mesh,Model,Texture}.*`, `object.vert/frag`, `skybox.vert/frag`, `debug_wireframe.frag` |
| **Q4 — Water Interactions & Procedural Geometry** | Everything dynamic layered on the ocean: boat buoyancy (6-DOF spring-damper), GPU rain + splashes, the boat-wake / ripple disturbance field, and procedural rock generation. | `src/water/BoatPhysics.*`, `src/water/GPURain.*`, `src/ocean/GPUDisturbance.*`, `src/graphics/RockGenerator.*`, the `rain_*` shaders, `disturbance_*.comp` |

> The repository is a real-time GPU water simulator in **C++17 + OpenGL 4.6** (GLFW, GLAD, GLM,
> STB, cgltf, Dear ImGui — all vendored under `external/`, which is NOT covered by these docs).

**This document covers Q4.**

---

## 🧭 In one minute (the high-level picture)

This quarter is **everything that reacts to or rides on top of the water**. Q2 makes a beautiful but
*inert* ocean — it heaves and rolls, but nothing touches it. Q4 is the layer that makes the scene feel
*alive*: boats bob in the swell, rain falls and splashes, moving boats leave a trailing wake, and the
big central rock is grown from scratch by code.

There are four independent systems, each with a real-world analogy:

- **Boat buoyancy (`BoatPhysics`).** Imagine a cork floating on a pond. As a wave passes under it, the
  cork rises and falls and tilts to match the slope of the water. We don't simulate true fluid pressure
  (that's expensive and unstable); instead we attach the boat to the water surface with three invisible
  **springs** — one pulling it up/down to the average water height, one tilting its nose to match the
  water's fore/aft slope, one rolling it side-to-side to match the cross-slope.

- **GPU rain (`GPURain`).** A swarm of falling streaks around the camera, each computed entirely on the
  GPU. When a drop hits the water it kicks up a little splash column and (separately) seeds a spreading
  ring on the water surface. Think of standing under an umbrella watching rain pock the surface of a lake.

- **Wake / disturbance field (`GPUDisturbance`).** A small height-map of the water that holds *ripples we
  added ourselves* — the V-shaped wake trailing a boat, or a splash you trigger with the `C` key. It runs
  a tiny wave simulation (the same physics as a drum skin or pond surface) so the ripples spread and fade
  realistically. Q2's water shader adds this height-map on top of the FFT ocean.

- **Procedural rock (`RockGenerator`).** A geometry recipe: start with a faceted sphere, subdivide it
  until it's smooth, then push every vertex in and out with layered noise to make it lumpy like a boulder.
  This is a *fallback* — the scene normally loads a fancy glTF rock, but if that file is missing we grow
  one here so the scene is never empty.

**Why it exists / what would break without it:** without Q4 the ocean would still render, but boats would
sit frozen at a fixed height clipping through waves, there would be no rain, no wake behind moving boats,
no interactive splashes, and (if the glTF rock failed to load) a hole in the middle of the scene. Q4 is
the "interaction" half of an interactive water *simulator*.

---

## 🔌 How this quarter connects to the others

Q4 produces no rendering of its own for the water surface — instead it **feeds data into Q2's water
shader** and is **driven each frame by Q1's loop**. Every hand-off is explicit:

**Inbound (data/calls Q4 receives):**

- **From Q1 (the frame loop, `src/main.cpp`):**
  - `BoatPhysics::step(dt, fixedXZ, yaw, heightAt)` is called once per vehicle per frame
    (`src/main.cpp:692` — `v.phys.step(deltaTime, v.pos, v.heading + v.modelYaw, surfFn)`).
  - The `heightAt` callback handed in is **Q2's** `ocean.sampleSurfaceHeight` — wrapped at
    `src/main.cpp:687` as `auto surfFn = [&](float x, float z){ return ocean.sampleSurfaceHeight(x, z); };`.
    This is the single coupling point between buoyancy and the FFT ocean.
  - `GPUDisturbance::disturb(...)` is called from Q1's vehicle loop to inject boat wakes
    (`src/main.cpp:638`) and from the `C`-key handler to fire a manual splash (`src/main.cpp:490`).
  - `GPUDisturbance::update(kFixedDt)` advances the wave field once per fixed substep
    (`src/main.cpp:554`).
  - `GPURain::update(...)` / `GPURain::render(...)` are called once per frame with camera position,
    wind, and the UI sliders (`src/main.cpp:677` for update).
  - The Q2 vertex attribute `Vertex` struct (from Q3's `Mesh.h`) is the output container that
    `generateRock` fills.

**Outbound (data Q4 hands to other quarters):**

- **To Q2's water shader (`standard.frag`):**
  - `GPUDisturbance::getHeightTexture()` returns the current wave-field texture; Q1 binds it to **GL
    texture unit 3** (`src/main.cpp:803-804`, `glActiveTexture(GL_TEXTURE3)` then
    `glBindTexture(GL_TEXTURE_2D, disturbance.getHeightTexture())`). Q2's `standard.frag` samples it as
    `uniform sampler2D disturbanceMap;` (declared at `standard.frag:13`, set to unit 3 at `src/main.cpp:160`)
    and adds it to the FFT displacement. The world-→-UV mapping uses a hard-coded `256.0` divisor in
    `standard.frag` that **must match** `kDisturbResolution = 256` (`src/main.cpp:96`).
  - `GPURain::getActiveRipples()` returns the CPU ripple-ring `std::deque<glm::vec3>`. Q1 packs it into the
    `rippleSSBO` at **SSBO binding 4** (`src/main.cpp:808` reads it, `initRippleSSBO`/`uploadRipples`
    bind it via `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rippleSSBO)` at `src/main.cpp:105,122`).
    Q2's `standard.frag` reads it as `layout(std430, binding = 4) readonly buffer RippleBuffer { ... }`
    (`standard.frag:21`) and uses `numRipples` + `uRippleLifetime` uniforms.

- **To Q3 (the renderer):**
  - `BoatPhysics::position` and `BoatPhysics::orientation` (a `glm::quat`) are read by Q1 to build each
    vehicle's model matrix; Q3's `Model`/`Mesh` then draw the boat with `object.vert/frag`.
  - `generateRock(...)` fills `std::vector<Vertex>` + indices that Q1 hands to a Q3 `Mesh`/`Model`,
    but **only as a fallback** when the glTF rock fails to load (`src/main.cpp:218-223`).
  - The rain itself is drawn by Q4's own shaders (`rain_gpu.*`, `rain_splash.*`) but uses the
    projection/view matrices produced by Q3's `Camera`.

**The one constant shared by three places:** the water plane Y (`kWaterSurfaceY = 0.5`) appears in
`rain_update.comp:60` and `rain_splash.vert:28` and must match the FFT base height used in Q2.

---

## 🗂️ Files in this quarter

| File | Lines (approx) | Role |
|---|---|---|
| `src/water/BoatPhysics.h` | 46 | 6-DOF spring-damper buoyancy struct: state, tuning fields, `step()` signature. |
| `src/water/BoatPhysics.cpp` | 96 | The `step()` solver: per-facet surface sampling, heave/pitch/roll springs, quaternion compose. |
| `src/water/GPURain.h` | 50 | `GPURain` class interface: SSBO-driven drops + CPU ripple-ring spawner. |
| `src/water/GPURain.cpp` | 122 | Drop SSBO setup, compute dispatch, attributeless draw, CPU ripple throttling. |
| `src/ocean/GPUDisturbance.h` | 39 | Interface for the Verlet wave-equation height field (wake/ripple map). |
| `src/ocean/GPUDisturbance.cpp` | 82 | Triple-buffer textures, `update()` (propagate), `disturb()` (inject Gaussian pulse). |
| `src/graphics/RockGenerator.h` | 16 | Declaration of `generateRock(subdivisions, seed, outVerts, outIndices)`. |
| `src/graphics/RockGenerator.cpp` | 110 | Icosphere subdivision + midpoint cache + layered simplex noise + normal accumulation. |
| `assets/shaders/rain_update.comp` | 76 | Compute: integrate drops, respawn, splash timer; hash RNG. |
| `assets/shaders/rain_gpu.vert` | 30 | Attributeless streak vertex shader reading the drop SSBO. |
| `assets/shaders/rain_gpu.frag` | 10 | Faint silver rain colour with tip→tail alpha fade. |
| `assets/shaders/rain_splash.vert` | 39 | Parabolic splash-column vertex shader, same SSBO. |
| `assets/shaders/rain_splash.frag` | 9 | Bright blue-white splash colour. |
| `assets/shaders/disturbance_inject.comp` | 23 | Compute: add a Gaussian pulse to the height image. |
| `assets/shaders/disturbance_propagate.comp` | 32 | Compute: one Verlet wave-equation step with edge clamping + height clamp. |

---

## 🔬 Deep dive

### `src/water/BoatPhysics.h` — the buoyancy state & contract

The header declares one POD-ish `struct BoatPhysics` (`BoatPhysics.h:21`). It holds **mutable physics
state** and **tuning knobs** (the tuning fields are exposed to ImGui in Q1).

**State members** (`BoatPhysics.h:23-27`):

- `glm::vec3 position` — the hull origin (waterline centre) in world space. XZ is overwritten by the
  caller each step (scripted navigation); only `position.y` is *solved* by the physics.
- `glm::quat orientation` — the current world orientation, composed as `yaw * pitch * roll`.
- `float heaveVel` — vertical velocity (m/s); the integrated state of the heave spring.
- `float pitch, pitchVel` — rotation about the local **X** (forward) axis and its angular velocity
  (nose up/down).
- `float roll, rollVel` — rotation about the local **Z** (beam) axis and its angular velocity
  (lean side-to-side).

**Tuning members** (`BoatPhysics.h:30-37`), with documented defaults:

- `length = 90.0f` — hull length (m) along local +X (forward).
- `beam = 28.0f` — hull width (m) along local +Z.
- `buoyancy = 3.0f` — up-force per metre submerged; scales the heave spring firmness.
- `linearDamp = 2.0f` — heave damping (higher settles faster, less bobbing).
- `angularDamp = 2.8f` — pitch/roll damping.
- `facetsX = 5`, `facetsZ = 3` — the sample grid resolution along length × beam.
- `floatHeight = 0.0f` — extra waterline offset (freeboard) added to the heave target.
- `bool initialized = false` — first-step latch.

**The contract** (`BoatPhysics.h:42-43`): `step(dt, fixedXZ, yaw, heightAt)`. The crucial design note in
the header comment: *"A spring toward the surface is far more stable than summing raw Archimedes forces
(which sink the hull unless perfectly tuned)."* Heave/pitch/roll **emerge** from the waves; the caller
supplies yaw and forward motion separately. The `heightAt` is a `std::function<float(float,float)>` that
must return ocean surface height at a world XZ — in practice Q2's `sampleSurfaceHeight`.

---

### `src/water/BoatPhysics.cpp` — the spring-damper solver

**Named constants** in an anonymous namespace (`BoatPhysics.cpp:6-13`):

- `kMaxStepSeconds = 1.0f/30.0f` — clamps `dt` so a hitch can't blow the explicit integrator up.
- `kHeaveStiffnessScale = 3.0f` — maps the `buoyancy` slider to heave spring stiffness.
- `kAngularStiffness = 6.0f` — pitch/roll spring stiffness.
- `kCriticalDampFactor = 2.0f` — the `2·ζ` term for (near-)critical damping.
- `kSlopeEps = 1e-3f` — guards against dividing by a tiny lever-arm denominator.
- `kMaxTiltRad = 0.6f` — clamps tilt to ≈34° so a violent wave can't flip the boat.

**`step()` walkthrough** (`BoatPhysics.cpp:15`):

1. **First-time init** (`:22-27`): on the first call, snap `position` to `(fixedXZ.x, heightAt(...), fixedXZ.z)`,
   zero the angles and velocities, and set `initialized = true`.

2. **Pin XZ** (`:28-29`): `position.x/z` are overwritten by `fixedXZ` every step — navigation is scripted
   by Q1; only Y/pitch/roll are physical.

3. **Clamp dt** (`:31`): `dt = std::clamp(dt, 0.0f, kMaxStepSeconds)`.

4. **Build current rotation** (`:34-38`): `yawQ = angleAxis(yaw, +Y)`, then the full orientation
   `ori = normalize(yawQ * angleAxis(pitch, +X) * angleAxis(roll, +Z))`, and its `mat3` form `R`. This `R`
   is used to push each local facet offset into world space so sampling respects the boat's current tilt.

5. **Facet grid setup** (`:40-44`): `halfL = 0.5·length`, `halfB = 0.5·beam`, `nx = max(2, facetsX)`,
   `nz = max(2, facetsZ)`, `facetCount = nx·nz`. (The `max(2, …)` guarantees the lever-arm fit below
   sees a span, not a single point.)

6. **Sample the surface at every facet** (`:54-66`). For facet `(ix, iz)`:
   - Local position: `lx = -halfL + 2·halfL · ix/(nx-1)` (forward), `lz = -halfB + 2·halfB · iz/(nz-1)` (beam).
   - World position: `wp = position + R · (lx, 0, lz)`.
   - Surface height: `surfH = heightAt(wp.x, wp.z)`.
   - Accumulate three things:
     - `sumH += surfH` (for the heave average),
     - the **fore/aft slope fit**: `fwdNum += lx·surfH`, `fwdDen += lx²`,
     - the **port/starboard slope fit**: `sideNum += lz·surfH`, `sideDen += lz²`.

   These are the closed-form numerator/denominator of a **least-squares slope through the origin**:
   for a line `h = m·l` minimising `Σ(h − m·l)²`, the best slope is `m = Σ(l·h)/Σ(l²)`. Because the facet
   grid is symmetric about the origin, `Σl = 0`, so the intercept term drops out and the average height is
   simply `sumH / facetCount`.

7. **Heave target** (`:67`): `avgH = sumH/facetCount + floatHeight` — the average surface height under the
   hull, plus freeboard.

8. **HEAVE spring** (`:69-74`): a critically-damped spring on `position.y`:
   - `stiffness = buoyancy · kHeaveStiffnessScale`,
   - `heaveAccel = stiffness·(avgH − position.y) − kCriticalDampFactor·linearDamp·heaveVel`,
   - integrate semi-implicitly: `heaveVel += heaveAccel·dt; position.y += heaveVel·dt`.

   This is the standard damped-spring ODE `ÿ = k·(target − y) − c·ẏ`. With `c = 2·linearDamp` and
   `k = buoyancy·3`, the system is roughly critically damped near the default tuning (no overshoot, fast
   settle).

9. **Pitch/roll targets from the fitted slopes** (`:76-81`):
   - `slopeFwd = fwdDen > kSlopeEps ? fwdNum/fwdDen : 0`,
   - `slopeSide = sideDen > kSlopeEps ? sideNum/sideDen : 0`,
   - `targetPitch = −atan(slopeFwd)` — nose follows the water, so a rising-forward slope tips the nose
     *down* (note the minus sign and the +X = forward convention),
   - `targetRoll = +atan(slopeSide)`.

   `atan` converts the dimensionless height-slope into an actual tilt angle.

10. **Pitch/roll springs** (`:84-87`): two more critically-damped springs toward those targets, with
    `kAngularStiffness` and `kCriticalDampFactor·angularDamp`, integrated semi-implicitly into
    `pitch/roll`.

11. **Tilt clamp** (`:90-91`): `pitch/roll = clamp(±kMaxTiltRad)`.

12. **Recompose orientation** (`:93-95`): `orientation = normalize(yawQ * angleAxis(pitch,+X) * angleAxis(roll,+Z))`.

**Why semi-implicit (symplectic) Euler:** updating velocity *then* position with the new velocity is more
stable for springs than explicit Euler and is essentially free. **Gotcha:** the `R` used for sampling is
last frame's orientation (pitch/roll from the previous step), which is correct — we sample where the boat
*is*, then compute where it should move to.

---

### `src/water/GPURain.h` — the rain system interface

`class GPURain` (`GPURain.h:17`) owns a GPU drop SSBO and a CPU ripple-ring queue. The header comment
captures the design split clearly: **all drops live on the GPU** (advanced by `rain_update.comp`, drawn
attributeless), and **the only CPU piece is the ripple-ring spawner**, throttled to ≈480 rings to feed the
water shader's bounded SSBO.

**Public API:**

- `GPURain(unsigned int maxDrops)` — allocate the drop buffer.
- `update(dt, camPos, windDrift, fallSpeed, spawnRate, rippleLifetime)` (`GPURain.h:24`) — advance drops on
  the GPU and spawn/age ripple rings on the CPU.
- `render(streakShader, splashShader, proj, view, windDrift, dropSize, opacity, splashHeight)`
  (`GPURain.h:29`) — two attributeless draw calls (streaks + splashes).
- `getActiveRipples()` (`GPURain.h:34`) — const ref to the `std::deque<glm::vec3>` ripple field for Q1 to
  upload to SSBO binding 4.

**Private members** (`GPURain.h:37-47`): `m_maxDrops`, `m_activeCount`, `m_ssbo` (drop buffer at binding 0),
`m_vao` (empty VAO for attributeless draws), `m_updateShader` (the compute shader), `m_ripples` (the ring
deque), `m_rippleBudget` (fractional accumulator), `m_time` (shader clock).

---

### `src/water/GPURain.cpp` — drop buffer, dispatch, and the ripple throttle

**Anonymous-namespace types & constants** (`GPURain.cpp:7-18`):

- `struct Drop { float pos[4]; float vel[4]; }` — the CPU mirror of the SSBO layout (two `vec4`s,
  32 bytes), used only to allocate the buffer.
- `kMaxStoredRipples = 4000` — hard cap on the CPU ripple deque length.
- `kRippleTarget = 480.0f` — target steady-state ring count; chosen just under the **512-entry SSBO budget**
  in the water shader.
- `kRippleRadius = 80.0f` — rings spawn within this radius of the camera.
- `kSpawnRadius = 100.0f` — radius of the disc drops spawn in (passed to the compute shader as `uSpawnRadius`).
- `kTwoPi = 6.2831853f` — matches `TWO_PI` in the rain shaders for consistent angular spawning.
- `kMaxRippleBurstPerFrame = 20.0f` — caps rings created in a single frame (prevents bursts after a stall).
- `frand01()` — uniform `[0,1)` via the global C `rand()`. Note (per the comment at `:16`): there is **no
  `srand` anywhere in `src/`**, so ripple placement is deterministic across runs.

**Constructor** (`GPURain.cpp:20-33`): loads the compute shader `../assets/shaders/rain_update.comp`,
value-initialises a `std::vector<Drop>(maxDrops)` to all-zero (so each drop's `pos.w == 0`, the respawn
sentinel), uploads it to `m_ssbo` with `GL_DYNAMIC_DRAW`, and creates an empty VAO for attributeless draws.

**Destructor** (`:35-38`): deletes the SSBO and VAO.

**`update()`** (`:40-89`):

1. Advance `m_time += dt`.
2. **Drop count from the slider** (`:47-48`): `dropsPerSpawn = 24.0f`; `m_activeCount =
   min(m_maxDrops, spawnRate · 24)`. The `spawnRate` UI value (originally "drops per frame") now controls
   *how many drops are alive*.
3. **GPU dispatch** (`:51-65`): if any drops are active, bind the compute shader and set its uniforms —
   `uDt`, `uCamPos`, `uWindDrift`, `uFallSpeed`, `uSpawnRadius = kSpawnRadius`, `uCount = m_activeCount`,
   `uTime`. Bind the SSBO to binding 0 with `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo)`, then
   `glDispatchCompute((m_activeCount + 255)/256, 1, 1)` — **256 threads per workgroup** matching the
   shader's `local_size_x = 256`. Issue a `GL_SHADER_STORAGE_BARRIER_BIT` barrier; the comment at
   `:62-64` explains only the storage barrier is needed because the vertex shader reads the SSBO as
   storage (not as vertex attributes).
4. **CPU ripple throttle** (`:67-88`):
   - `rippleRate = kRippleTarget / max(rippleLifetime, 0.25)` — rings/sec needed to hold ≈480 alive given
     their lifetime.
   - `m_rippleBudget = min(m_rippleBudget + rippleRate·dt, kMaxRippleBurstPerFrame)` — accumulate a
     fractional spawn budget, capped per frame.
   - While `m_rippleBudget ≥ 1` and `spawnRate > 0`: subtract 1, pick a uniform disc point
     `a = frand01()·kTwoPi`, `r = sqrt(frand01())·kRippleRadius` (the `sqrt` makes the distribution
     **uniform over area**, not bunched at the centre). Pack a ring as `vec3(camPos.x + cos(a)·r,
     camPos.z + sin(a)·r, 0)` — **`.x/.y` = world X/Z centre, `.z` = age in seconds** — matching
     `standard.frag`'s `RippleBuffer` layout. Evict the oldest if the deque hits `kMaxStoredRipples`.
   - **Age every ring** (`:84-88`): `it->z += dt`; erase when `it->z > rippleLifetime`.

**`render()`** (`:91-122`): early-out if no active drops. Bind the SSBO to binding 0 and the empty VAO,
enable `GL_LINE_SMOOTH`. Two passes, **both reading the same SSBO with zero CPU vertex work**:

- **Pass 1 — streaks** (`:102-109`): use `streakShader`, set `projection/view/uWindDrift/uDropSize/uOpacity`,
  set line width `clamp(dropSize·1.5, 1, 6)`, draw `GL_LINES` with `m_activeCount·2` vertices (2 per drop).
- **Pass 2 — splash columns** (`:113-118`): use `splashShader`, set `projection/view/uSplashHeight`, fixed
  line width 2.5, same `m_activeCount·2` `GL_LINES`. Inactive splashes emit a degenerate off-screen line
  (see `rain_splash.vert`), so this is cheap.

Restore state (`:120-121`).

---

### `src/ocean/GPUDisturbance.h` — the wake/ripple height field interface

`class GPUDisturbance` (`GPUDisturbance.h:7`) is a small **2D wave-equation simulation** living in a single
red-channel float texture. It is *not* the FFT ocean — it's a separate height map that holds **only the
disturbances we inject** (boat wakes, `C`-key splashes), which Q2's water shader then sums on top of the
spectral displacement.

**API:**

- `GPUDisturbance(resolution, worldSize)` — `resolution` is the texture edge (e.g. 256), `worldSize` is the
  ocean diameter in metres (so 1 texel = `worldSize/resolution` metres).
- `update(float dt)` — advance one wave step. (The `dt` is ignored; see below.)
- `disturb(worldPos, amplitude = 3.0f, sigmaTexels = 5.0f)` — inject a Gaussian pulse at a world XZ. The
  `sigmaTexels` footprint lets a small boat make a tight ripple and a big ship a broad swell from the same
  call (`GPUDisturbance.h:16-19`).
- `getHeightTexture()` — returns `m_tex[m_curr]`, the texture Q1 binds to GL texture unit 3.

**Private state** (`:24-36`): `m_resolution`, `m_worldSize`, a **triple texture buffer** `m_tex[3]` with
indices `m_curr / m_prev / m_next` rotated each frame, the two compute shaders, the precomputed `m_waveC`
and `m_damping`, and the `allocTexture` helper.

---

### `src/ocean/GPUDisturbance.cpp` — Verlet wave equation on a triple buffer

**Constants** (`GPUDisturbance.cpp:6-11`):

- `kWaveSpeed = 12.0f` — wave propagation speed (m/s); how fast disturbance rings spread.
- `kDamping = 0.998f` — energy retained per tick (just under 1, so ripples fade).
- `kSigmaTexels = 5.0f` — default Gaussian radius if the caller passes ≤0.
- `kFixedDt = 1.0f/60.0f` — the **fixed** timestep baked into the wave constant.

**Constructor** (`:13-26`): loads `disturbance_propagate.comp` and `disturbance_inject.comp`, sets
`m_damping = kDamping`, and precomputes the **Courant number squared**:

```
dx     = worldSize / resolution                       // metres per texel
m_waveC = (kWaveSpeed² · kFixedDt²) / dx²
```

This `m_waveC` is the dimensionless `c = (v·Δt/Δx)²` term of the discrete wave equation. **For stability
the 2D explicit scheme requires `v·Δt/Δx ≤ 1/√2`, i.e. `m_waveC ≤ 0.5`.** With the project's defaults
(`worldSize = kOceanMeshRes·kOceanMeshTile`, resolution 256) `dx` is large enough that `m_waveC` stays
comfortably under that bound. Then it allocates all three textures.

**`allocTexture()`** (`:32-43`): `glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, N, N)` — immutable single-mip
**R32F** texture; `LINEAR` filtering (so the water shader gets smooth interpolation); `CLAMP_TO_EDGE`
wrapping (so ripples don't tile across the ocean); and a `glTexSubImage2D` upload of an all-zero buffer to
clear it.

**`update(dt)`** (`:45-64`) — *note the `dt` parameter is unused* (`/*dt*/`); the timestep is the baked-in
`kFixedDt` from the constructor. This is intentional: the explicit wave scheme is only stable for the `dt`
its Courant number was computed for, so it must run at a fixed step regardless of frame time.
- `groups = (resolution + 15)/16` → workgroup count matching the shader's `local_size = 16×16`.
- Set `waveC` and `damping` uniforms.
- Bind images: unit 0 = `m_curr` **READ_ONLY**, unit 1 = `m_prev` **READ_ONLY**, unit 2 = `m_next`
  **WRITE_ONLY**, all `GL_R32F`.
- `glDispatchCompute(groups, groups, 1)`, then barrier
  `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT` (image writes + the later texture
  sample in the water shader must both see the result).
- **Rotate the buffers** (`:60-63`): `tmp = m_prev; m_prev = m_curr; m_curr = m_next; m_next = tmp`. So
  `next` becomes the new `curr`, `curr` becomes `prev`, and the old `prev` is recycled as the next write
  target. This is the classic **ping-pong for a second-order (3-frame) scheme**: the Verlet update needs
  *both* the current and previous frames.

**`disturb(worldPos, amplitude, sigmaTexels)`** (`:66-82`):
- Map world XZ to UV: `uv = worldPos / worldSize + 0.5`. If `uv` is outside `[0,1]²`, **bail** (off-map
  impacts do nothing).
- Set `centerUV`, `amplitude`, and `sigmaTexels` (defaulting to `kSigmaTexels` if ≤0).
- Bind **only** `m_curr` to image unit 0 as **READ_WRITE** — the pulse is added to the *current* buffer so
  it is visible the same frame.
- Dispatch with the same `16×16` workgroup tiling and barrier.

**Why inject into `curr` (read-write) but propagate using three separate bound images:** injection is an
additive `prev + pulse` blend on a single texture; propagation must read two textures and write a third, so
it needs the full triple binding.

---

### `src/graphics/RockGenerator.h` — the rock recipe declaration

A single free function `generateRock(subdivisions, seed, outVertices, outIndices)` (`RockGenerator.h:12`).
The header documents it as a subdivided **icosphere** with **value-noise displacement** and recomputed
normals, **deterministic per seed** (same seed ⇒ same rock). `subdivisions` controls smoothness (2–3 is a
good rock); `seed` varies shape. Output containers are Q3's `std::vector<Vertex>` + `std::vector<unsigned int>`.

---

### `src/graphics/RockGenerator.cpp` — icosphere + layered noise

**`buildIcosahedron()`** (`RockGenerator.cpp:10-24`): the 12 canonical icosahedron vertices built from the
golden ratio `t = (1 + √5)/2 ≈ 1.618` (`:11`), each normalised onto the unit sphere (`:17`), plus the 20
triangle faces as an index list (`:18-23`). An icosahedron is the most uniform starting point for a sphere
(20 equilateral faces, no pole pinching).

**`midpoint()`** (`RockGenerator.cpp:27-38`): the **edge-midpoint cache** that keeps subdivision watertight.
For an edge `(a, b)` it builds a 64-bit key `(min(a,b) << 32) | max(a,b)` (order-independent, so both
triangles sharing the edge get the *same* new vertex), looks it up in a `std::map<uint64_t, unsigned int>`,
and if absent creates the normalised midpoint `normalize((verts[a]+verts[b])·0.5)` (projecting it back onto
the sphere — this is what turns a flat subdivision into a *sphere*), records it, and returns its index.

**Noise tuning constants** (`RockGenerator.cpp:41-51`):

- `kSeedScatterX/Y/Z = 13.13 / 7.77 / 3.33` — multipliers turning the integer `seed` into a 3D offset into
  noise space (so different seeds sample different regions of the noise field).
- `kNoiseBaseFreq = 2.3f` — base sampling frequency (how lumpy the lowest octave is).
- `kOctaveAmp0/1/2 = 0.50 / 0.25 / 0.13` — per-octave amplitudes (each octave contributes less, ≈½ each
  step — a **fractal/fBm** falloff).
- `kOctaveFreq1/2 = 2.1 / 4.3` — per-octave frequency multipliers (each octave roughly doubles detail; the
  non-integer ratios avoid the noise repeating).
- `kDisplacementGain = 0.45f` — how far the summed noise pushes the vertex radius.
- `kVerticalSquash = 0.8f` — flattens Y so the rock sits like a boulder, not a perfect ball.

**`generateRock()`** (`RockGenerator.cpp:54-110`):

1. **Base mesh** (`:57-59`): `buildIcosahedron(verts, tris)`.
2. **Subdivide** `subdivisions` times (`:62-74`): for each pass, build a fresh `cache`, and for every
   triangle `(a,b,c)` create midpoints `ab, bc, ca` (via the cache) and emit the **4 child triangles**
   `{a,ab,ca}, {b,bc,ab}, {c,ca,bc}, {ab,bc,ca}` — the standard 1→4 loop subdivision that quadruples the
   triangle count each pass.
3. **Displace** (`:76-88`): compute `seedOffset = seed · (kSeedScatterX, kSeedScatterY, kSeedScatterZ)`.
   For each unit-sphere vertex `v`: sample point `p = v·kNoiseBaseFreq + seedOffset`, then sum three
   octaves of `glm::simplex` (despite the "value-noise" wording in the header, the code uses **simplex
   noise**):
   ```
   n = kOctaveAmp0·simplex(p)
     + kOctaveAmp1·simplex(p·kOctaveFreq1)
     + kOctaveAmp2·simplex(p·kOctaveFreq2)
   radius = 1 + kDisplacementGain·n
   v *= radius;  v.y *= kVerticalSquash
   ```
   This pushes each vertex in/out along its radial direction by the fractal noise, then squashes Y.
4. **Vertices with placeholder normals** (`:91-98`): resize `outVertices`, copy positions, zero the
   normals, and assign a **planar UV** `(v.x·0.5 + 0.5, v.z·0.5 + 0.5)` (a top-down projection — good
   enough for a noise/material look). `outIndices = tris`.
5. **Face-normal accumulation** (`:100-109`): for each triangle, the unnormalised face normal is the cross
   product `fn = (verts[ib]−verts[ia]) × (verts[ic]−verts[ia])`; add `fn` to each of the triangle's three
   vertex normals. Because `fn`'s magnitude is proportional to twice the triangle area, this naturally
   **area-weights** the smooth normals. Finally normalise every vertex normal (`:107-109`).

**Gotcha:** normals are accumulated from the **displaced** `verts` (the same array that was pushed by
noise), so the lighting matches the bumpy surface, not the original sphere.

---

### `assets/shaders/rain_update.comp` — drop integration on the GPU

`#version 460`, `local_size_x = 256` (`rain_update.comp:1-2`).

**SSBO layout** (`:10-12`): `struct Drop { vec4 pos; vec4 vel; }`, bound `std430, binding = 0` as
`buffer DropBuffer`. The packing (documented at `:5-9`) is:
- `pos.xyz` = world position, `pos.w` = fall speed (m/s),
- `vel.x` = splash timer (seconds remaining; >0 means a splash is playing at the impact),
- `vel.yz` = the impact XZ (where the splash plays after the drop respawns),
- `vel.w` = a stable per-drop seed.

**Uniforms** (`:14-20`): `uDt`, `uCamPos`, `uWindDrift` (vec2 horizontal m/s), `uFallSpeed`,
`uSpawnRadius`, `uCount`, `uTime`. `const float TWO_PI = 6.2831853` (`:22`, matches the CPU side).

**`hash(uint)`** (`:25-29`): an integer bit-mixing hash (the classic
`n=(n<<13)^n; n=n·(n·n·15731+789221)+1376312589`) returning a float in `[0,1)` via the low 31 bits divided
by `0x7fffffff`. This is the GPU RNG used for respawn placement.

**`respawn(inout Drop d, uint id)`** (`:31-39`): scatters the drop into a disc around the camera:
- angle `a = hash(id·7 + uTime·31)·TWO_PI`, radius `r = sqrt(hash(id·13 + uTime·17))·uSpawnRadius`
  (again `sqrt` for area-uniform placement),
- `pos.x/z = camPos.x/z + cos/sin(a)·r`,
- `pos.y = camPos.y + 30 + hash(...)·25` (drops start 30–55 m above the camera),
- `pos.w = uFallSpeed + (hash(id·5) − 0.5)·20` (per-drop fall-speed jitter ±10 m/s),
- `vel.w = hash(id·23)` (the stable per-drop seed used by the splash shader for height variety).

**`main()`** (`:41-76`):
1. `id = gl_GlobalInvocationID.x`; bail if `id ≥ uCount`.
2. Load the drop. **First-time init**: if `pos.w < 1e-4` (the zero sentinel from the all-zero SSBO),
   `respawn`.
3. **Decrement the splash timer**: `vel.x = max(vel.x − uDt, 0)`.
4. **Integrate** fall + wind: `pos.y -= pos.w·uDt`, `pos.x += uWindDrift.x·uDt`, `pos.z += uWindDrift.y·uDt`.
5. **Local constants** (`:60-61`): `kWaterSurfaceY = 0.5` (the water plane Y — *must match*
   `rain_splash.vert` and the FFT base) and `kSplashLife = 0.45` (*must match* `rain_splash.vert`).
6. **Out-of-disc test**: `outside = dx²+dz² > uSpawnRadius²·1.2` (drifted too far from the camera).
7. **Impact logic** (`:64-73`):
   - If it hit the water (`pos.y ≤ kWaterSurfaceY`) **and** still inside the disc: remember the impact XZ,
     `respawn` the drop, then set `vel.x = kSplashLife`, `vel.y = ix`, `vel.z = iz` — i.e. **kick a splash
     at the old impact point while the drop itself recycles to the top**.
   - Else if it hit the water **or** drifted outside: just `respawn` (no splash).
8. Write back: `drops[id] = d`.

---

### `assets/shaders/rain_gpu.vert` — attributeless streak draw

`#version 460` (`rain_gpu.vert:1`). Reads the same `Drop` SSBO `std430, binding = 0` as **readonly**
(`:5-6`). Uniforms: `projection`, `view`, `uWindDrift` (a **vec2**), `uDropSize` (`:8-11`). Output `vAlpha`.

The draw issues `uCount·2` vertices as `GL_LINES`; this shader turns each pair into one streak (`:15-30`):
- `drop = gl_VertexID >> 1` (2 verts per drop), `tail = (gl_VertexID & 1) == 1` (even = bright tip, odd =
  faded tail).
- Streak length `len = pos.w · 0.18 · uDropSize` (longer for faster drops / bigger UI size).
- Direction `dir = normalize(vec3(uWindDrift.x, −pos.w, uWindDrift.y))` — points down plus the wind drift.
- The **tip** vertex stays at `pos.xyz` with `vAlpha = 1`; the **tail** vertex is `pos − dir·len` with
  `vAlpha = 0`. So each streak fades from a bright head to a transparent tail.
- `gl_Position = projection · view · vec4(p, 1)`.

---

### `assets/shaders/rain_gpu.frag` — rain colour

`#version 460` (`rain_gpu.frag:1`). Input `vAlpha`, uniform `uOpacity`. Outputs a faint silver-white
`vec4(vec3(0.80, 0.84, 0.90), vAlpha · uOpacity)` (`:7-10`) — so the tip is bright and the tail transparent
(via the interpolated `vAlpha`), and the whole sheet's strength is scaled by the UI `uOpacity`.

---

### `assets/shaders/rain_splash.vert` — the parabolic splash column

`#version 460` (`rain_splash.vert:1`). Reads the same `Drop` SSBO readonly (`:6-7`). Uniforms `projection`,
`view`, `uSplashHeight` (`:9-11`). Output `vAlpha`.

Like the streak pass it consumes `uCount·2` `GL_LINES` vertices (`:15-38`):
- `drop = gl_VertexID >> 1`; `tip = (gl_VertexID & 1) == 0` (even = rising tip, odd = base at surface).
- `splashT = vel.x` (seconds remaining), `seed = vel.w`.
- **No active splash** (`splashT ≤ 0`): emit a **degenerate off-screen vertex** at clip-space
  `(2,2,2,1)` with `vAlpha = 0` and return — this is how inactive splashes cost nothing visible.
- **Local constants** (`:27-28`): `kSplashLife = 0.45` (*MUST match* `rain_update.comp`) and
  `kWaterSurfaceY = 0.5` (*must match* `rain_update.comp` and the FFT base).
- **Parabolic height**: `age = kSplashLife − splashT` (counts up 0→life), `t = clamp(age/kSplashLife, 0, 1)`,
  and
  ```
  h = (1 − (2t − 1)²) · (1.5 + seed·2) · uSplashHeight
  ```
  The `1 − (2t−1)²` factor is a parabola that is 0 at `t=0` and `t=1` and **peaks at `t=0.5`** (mid-life),
  so the column rises and falls; `(1.5 + seed·2)` gives per-drop height variety via the stored seed.
- `base = vec3(vel.y, kWaterSurfaceY, vel.z)` — the stored impact XZ on the surface. The **tip** vertex is
  `base + (0, h, 0)` with `vAlpha = 1 − t` (brightest at impact, fading as it falls); the **base** vertex is
  `base` with `vAlpha = 0`.
- `gl_Position = projection · view · vec4(p, 1)`.

---

### `assets/shaders/rain_splash.frag` — splash colour

`#version 460` (`rain_splash.frag:1`). Input `vAlpha`. Outputs a bright blue-white water column
`vec4(vec3(0.85, 0.92, 1.0), vAlpha · 0.6)` (`:8`). The fixed `0.6` (rather than the rain `uOpacity`)
keeps the splash reading as **kicked-up water** independent of how faint the rain is set.

---

### `assets/shaders/disturbance_inject.comp` — additive Gaussian pulse

`#version 460`, `local_size = 16×16` (`disturbance_inject.comp:1-2`). One image `binding = 0, r32f`
`image2D heightImg` (`:4`). Uniforms `centerUV` (impact in `[0,1]`), `amplitude` (metres), `sigmaTexels`
(Gaussian radius in texels) (`:6-8`).

**`main()`** (`:10-23`):
- `coord = gl_GlobalInvocationID.xy`; bail if outside `imageSize`.
- Convert: `texelPos = coord + 0.5`, `centerTexel = centerUV · size`, `diff = texelPos − centerTexel`,
  `dist2 = dot(diff, diff)`.
- **Gaussian splat**: `pulse = amplitude · exp(−dist2 / (2·sigmaTexels²))` — a 2D Gaussian centred on the
  impact with standard deviation `sigmaTexels` texels.
- Additive blend onto the current height: `prev = imageLoad(...).r`, `imageStore(coord, vec4(prev + pulse,
  0, 0, 1))`. (Only the `.r` channel matters; the texture is R32F.)

**Why additive:** multiple wakes/splashes in the same frame must accumulate, not overwrite.

---

### `assets/shaders/disturbance_propagate.comp` — one Verlet wave step

`#version 460`, `local_size = 16×16` (`disturbance_propagate.comp:1-2`). Three images (`:5-7`): `binding 0`
`currImg` readonly, `binding 1` `prevImg` readonly, `binding 2` `nextImg` writeonly — all `r32f`. Uniforms
`waveC` (the `(speed²·dt²)/dx²` term) and `damping` (`:9-10`).

**`main()`** (`:12-31`):
- `coord = gl_GlobalInvocationID.xy`; bail if outside `imageSize`.
- **Fixed (clamped) boundary** (`:18-21`): the four neighbours `L/R/D/U` are clamped to the texture edge
  (`max(coord−1,0)`, `min(coord+1,size−1)`) — a **Neumann-ish reflecting boundary**, no wrap, matching the
  texture's `CLAMP_TO_EDGE`.
- Read `cur` and `prev` at this texel.
- **Discrete Laplacian** (`:25-27`):
  `lapl = curr(L) + curr(R) + curr(D) + curr(U) − 4·cur` (the standard 5-point stencil).
- **Verlet wave update** (`:30`):
  ```
  next = clamp( (2·cur − prev + waveC·lapl) · damping, −HEIGHT_CLAMP, HEIGHT_CLAMP )
  ```
  This is the explicit finite-difference 2D **wave equation** `∂²h/∂t² = v²·∇²h`: rearranged,
  `next = 2·cur − prev + c·∇²h` with `c = waveC`, then multiplied by `damping` (0.998) to bleed energy and
  clamped to `HEIGHT_CLAMP = 25.0` (`:29`) as a hard stability bound against runaway oscillation.
- `imageStore(nextImg, coord, vec4(next, 0, 0, 1))`.

**Gotcha:** the scheme is only conditionally stable — `waveC` (the Courant number squared) must stay below
≈0.5, which is why the CPU side bakes a *fixed* `kFixedDt` into `m_waveC` and runs `update()` at a fixed
step rather than the variable frame `dt`.

---

## ⚠️ Gotchas & invariants

- **`kWaterSurfaceY = 0.5` is duplicated** in `rain_update.comp:60` and `rain_splash.vert:28`, and must
  equal the FFT ocean base height in Q2. Splashes spawn/play exactly at this Y; if it drifts, splashes
  float or sink.
- **`kSplashLife = 0.45` is duplicated** in `rain_update.comp:61` and `rain_splash.vert:27`. The compute
  shader *sets* the timer to this value on impact; the splash vertex shader *normalises* against the same
  value to compute the parabola. They must match or the splash height curve desyncs.
- **`kTwoPi`/`TWO_PI = 6.2831853`** appears CPU-side (`GPURain.cpp:13`) and in `rain_update.comp:22`; kept
  identical so CPU ripple rings and GPU drop respawns scatter consistently.
- **Disturbance resolution coupling:** `kDisturbResolution = 256` (`src/main.cpp:96`) must match the
  `GPUDisturbance` ctor argument **and** the hard-coded `256.0` world→UV divisor in `standard.frag`. The
  wake-sizing math in `src/main.cpp:629` also derives `kTexelM` from this.
- **The wave field must run at a fixed dt.** `GPUDisturbance::update(dt)` ignores its `dt` and uses the
  baked `kFixedDt = 1/60`. Calling it from a variable-rate loop, or changing `kWaveSpeed`/resolution
  without re-checking `m_waveC ≤ ~0.5`, can make `disturbance_propagate.comp` blow up (the `HEIGHT_CLAMP`
  is a safety net, not a license to violate the Courant condition).
- **Triple-buffer rotation order is load-bearing.** In `GPUDisturbance::update` the rotation
  `prev←curr, curr←next, next←(old prev)` (`GPUDisturbance.cpp:60-63`) must happen *after* the dispatch and
  *before* the next frame, or the Verlet step reads the wrong history. `disturb()` writes into `m_curr`
  (read-write) so its pulse survives the next propagate (which treats `curr` as the "current" history).
- **Ripple SSBO budget:** the CPU spawner targets `kRippleTarget = 480` (`GPURain.cpp:11`) to stay under
  the **512-entry** `RippleBuffer` in `standard.frag`. The deque is hard-capped at `kMaxStoredRipples =
  4000`, but Q1 only uploads the newest `min(count, kMaxRipples)` (`src/main.cpp:111`). Don't raise the
  spawn target past the shader's array size.
- **Ripple vec3 packing** is an implicit contract: `.x = world X`, `.y = world Z`, `.z = age (s)`
  (`GPURain.cpp:79`). Q1 widens each to `vec4(..., 0)` for the SSBO; `standard.frag`'s layout must match.
- **Determinism:** there is no `srand` in `src/`, so both the CPU `frand01()` and (because `uTime` derives
  from accumulated `dt`) the GPU rain are reproducible run-to-run. Adding an `srand(time(...))` anywhere
  would silently break that.
- **BoatPhysics XZ is not physical.** `step()` overwrites `position.x/z` from `fixedXZ` every call; only
  heave/pitch/roll are solved. Yaw is passed in, not integrated. Don't expect the boat to drift from wave
  forces.
- **`kMaxStepSeconds = 1/30` and `kMaxTiltRad = 0.6`** are the two safety clamps keeping the explicit
  spring integrator stable through frame hitches and steep waves; removing either risks NaNs / capsizing.
- **`facetsX/Z` are floored to 2** in `step()` (`BoatPhysics.cpp:42-43`) so the least-squares slope fit
  always has a real lever arm; a 1×N grid would zero a denominator.
- **RockGenerator is a fallback only** (`src/main.cpp:218-223`). It accumulates normals from the
  *displaced* vertices, and uses `glm::simplex` despite the header saying "value-noise".

---

## 🧠 Mental-model recap

- Q4 is the **interaction layer**: it never owns the water surface render — it feeds height/ripple data
  into Q2's shader and is ticked by Q1's loop.
- **Boats float by springs, not forces.** Sample the FFT surface under a grid of hull facets; spring heave
  to the average height and spring pitch/roll to the least-squares fore/aft and port/starboard slopes.
  Critically damped ⇒ no overshoot, never sinks.
- **Rain is all-GPU drops + a thin CPU ripple thread.** One compute dispatch advances every drop; two
  attributeless `GL_LINES` draws render streaks and splashes from the *same* SSBO. The CPU only maintains
  ≈480 spreading rings to feed the water shader.
- **A drop's two `vec4`s carry a tiny state machine:** position + fall speed, plus a splash timer / stored
  impact XZ / seed. On impact the drop teleports to the top while leaving a 0.45 s splash behind.
- **The wake field is a literal pond simulation:** an R32F height texture stepped by the explicit Verlet
  wave equation (`next = 2·cur − prev + c·∇²h`, ×damping), triple-buffered for its second-order history,
  with Gaussian pulses injected by `disturb()`.
- **Wake footprint scales with the boat** via `sigmaTexels` — a jet-ski drops a tight ripple, a ship a
  broad swell, from the same `disturb()` call.
- **Courant condition is king** for the wave field: fixed `dt`, fixed wave speed, and a hard `HEIGHT_CLAMP`
  backstop.
- **The rock is grown, not modelled:** icosahedron → cached-midpoint subdivision (1→4 triangles, projected
  to the sphere) → 3-octave simplex radial displacement → area-weighted normals. Deterministic per seed,
  used only when the glTF rock is missing.
- **Several magic numbers are duplicated across files on purpose** (`kWaterSurfaceY`, `kSplashLife`,
  `TWO_PI`, the 256 resolution); treat them as cross-file invariants.

---

## 📖 Glossary

- **6-DOF (six degrees of freedom):** the 3 translations + 3 rotations a rigid body can have; here only
  heave (Y translation), pitch, and roll are solved, with XZ + yaw scripted.
- **Heave / pitch / roll:** vertical bob, nose-up/down rotation (about forward axis), side-lean rotation
  (about beam axis).
- **Spring-damper / critically-damped spring:** a mass on a spring with friction; "critically damped" means
  it returns to rest as fast as possible without overshooting (`accel = k·(target−x) − c·v`, `c = 2√k`).
- **Semi-implicit (symplectic) Euler:** integrate velocity first, then position using the *new* velocity;
  more stable for oscillators than plain Euler.
- **Least-squares slope:** the best straight-line slope through scattered samples; `m = Σ(l·h)/Σ(l²)` when
  the line passes through the origin.
- **Quaternion:** a 4-number representation of 3D rotation that avoids gimbal lock; composed by
  multiplication (`yaw·pitch·roll`) and normalised to stay a valid rotation.
- **SSBO (Shader Storage Buffer Object):** a large read/write GPU buffer a shader can index like an array;
  the rain drops (binding 0) and ripple rings (binding 4) live in SSBOs.
- **Compute shader:** a GPU program not tied to the rasteriser, dispatched in workgroups
  (`glDispatchCompute`) to do general-purpose parallel work (drop integration, wave stepping).
- **Workgroup / `local_size`:** the block of GPU threads that run together; rain uses `local_size_x = 256`,
  the disturbance shaders `16×16`. Dispatch count = `ceil(work / local_size)`.
- **Memory barrier (`glMemoryBarrier`):** tells the GPU to finish/visibility-flush certain writes before a
  dependent read — `SHADER_STORAGE_BARRIER_BIT` for SSBOs, `SHADER_IMAGE_ACCESS` + `TEXTURE_FETCH` for
  image/texture round-trips.
- **Image binding / `image2D`:** a texture bound for direct load/store in a shader (`imageLoad`/`imageStore`),
  distinct from sampling; used by the disturbance compute passes.
- **Attributeless / instanced draw:** issuing `glDrawArrays` with no vertex buffer; the vertex shader
  synthesises geometry from `gl_VertexID` and reads data from an SSBO instead.
- **R32F:** a single-channel 32-bit float texture format; used for the disturbance height field.
- **Verlet integration:** a time-stepping scheme for second-order ODEs that uses the current and previous
  states (`next = 2·cur − prev + accel`); ideal for the wave equation.
- **Wave equation / discrete Laplacian:** `∂²h/∂t² = v²∇²h`; on a grid `∇²h` is the 5-point stencil
  `(L+R+U+D − 4·center)`.
- **Courant (CFL) condition:** the stability limit for explicit wave/diffusion schemes; here `v·Δt/Δx ≤
  1/√2`, equivalently `waveC = (vΔt/Δx)² ≤ 0.5`.
- **Ping-pong / double (triple) buffering:** alternating read/write textures so a shader never reads the
  buffer it's writing; the wave field needs *three* (curr/prev/next) for its second-order history.
- **Gaussian splat:** adding a bell-curve bump `A·exp(−r²/2σ²)` to a field; how `disturb()` injects a
  ripple of radius `sigmaTexels`.
- **Damping factor:** the per-step multiplier (<1) that bleeds energy out of the wave field so ripples fade
  (`kDamping = 0.998`).
- **Icosahedron / icosphere:** a 20-faced regular polyhedron; subdividing its faces and projecting new
  vertices to the sphere yields an evenly-tessellated "icosphere".
- **Subdivision + midpoint cache:** splitting each triangle into 4; the cache ensures an edge shared by two
  triangles produces one shared midpoint vertex (a watertight mesh).
- **Simplex noise:** a smooth, gradient-based procedural noise (Perlin's successor) sampled by
  `glm::simplex`; summed across octaves for fractal detail.
- **Octave / fBm (fractional Brownian motion):** layering noise at doubling frequency and halving amplitude
  to build natural-looking detail.
- **Face-normal accumulation (area-weighted normals):** summing each triangle's (unnormalised) cross-product
  normal into its vertices then normalising, giving smooth, area-weighted vertex normals.
- **Freeboard:** the height of a hull above the waterline; here the `floatHeight` offset added to the heave
  target.
- **Fresnel / Schlick (referenced by Q2):** the angle-dependent reflectance of water; computed in Q2's
  `standard.frag`, not here — listed for cross-reference.
- **PBO (Pixel Buffer Object, referenced by Q1):** an async GPU↔CPU pixel transfer buffer used by Q1's
  frame recorder, not by Q4 — listed for completeness.
