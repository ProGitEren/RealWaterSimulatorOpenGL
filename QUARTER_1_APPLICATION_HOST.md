# Quarter 1 — Application Host, Frame Loop & Build System

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

**This document covers Q1.**

---

## 🧭 In one minute (the high-level picture)

Think of this project like a movie set. There are actors (boats), scenery (rocks, cliffs, sky),
special-effects departments (the ocean simulation, the rain machine), and a camera. **Quarter 1 is
the film director and the soundstage manager.** It does not personally simulate water or draw a boat
— but it hires every department, tells each one *when* to do its work and in *what order*, hands the
output of one department to the next, and decides what the camera sees.

Concretely, Q1 is **`main()`** — the single entry point of the program — plus the small build/ignore
files that turn the source tree into a runnable executable.

What it actually does, in plain terms:

- **Sets everything up once at startup.** It opens a window, starts the on-screen control panel
  (ImGui), creates the camera, loads all the shader programs, loads the sky images, builds the ocean,
  loads the boats/rock/cliffs, and prepares the rain system.
- **Runs the main loop forever** (until you close the window). Each pass through the loop it reads
  keyboard/mouse input, advances the physics, then draws one frame.
- **Keeps physics steady regardless of frame rate.** It uses a *fixed-timestep accumulator* so the
  ocean and the boats always advance in identical 1/60-second chunks, even if the screen is drawing
  at 30 or 144 frames per second. This is the difference between a simulation that looks the same on
  every machine and one that wobbles when the frame rate dips.
- **Choreographs the boats.** A lightweight "boids-style" steering routine keeps the jet-ski, yacht,
  and big ship cruising around the bay, turning away from the cliffs, the rock, and each other —
  and a hard safety net guarantees they can never clip through anything.
- **Wires the departments together.** The ocean produces height/normal textures; the disturbance
  system produces a wake texture; Q1 binds those to specific GPU texture slots and passes them to the
  water shader. The ocean's CPU-readable heights feed the boat buoyancy. And so on.

Without Q1 there is no program: nothing would be created, nothing would update, nothing would be
drawn in the right order, and the carefully separated subsystems (ocean, rain, physics, rendering)
would have no one telling them how to cooperate.

---

## 🔌 How this quarter connects to the others

Q1 is the hub; every other quarter is a spoke it instantiates and calls. Here are the explicit
hand-off points (constructor args, function calls, texture units, SSBO bindings, uniforms).

### Into Q2 (Ocean Spectral Simulation & Water Surface)
- **Constructs** `GPUFFTOcean ocean(kOceanResolution, kOceanMeshRes * kOceanMeshTile, 15.0f, 35.0f, 2.0f)`
  (`main.cpp:212`) and `OceanMesh oceanMesh(kOceanMeshRes, kOceanMeshTile)` (`main.cpp:213`).
- **Loads the surface shaders** `standard.vert` + `standard.frag` into `shader` (`main.cpp:147`) and
  `standard.vert` + `debug_wireframe.frag` into `debugWireframeShader` (`main.cpp:148`, the
  `debug_wireframe.frag` itself is Q3-owned but pairs with the Q2 vertex shader).
- **Drives the sim** by calling `ocean.update(kFixedDt)` once per fixed substep (`main.cpp:553`) and
  `ocean.readbackDisplacement()` once per frame (`main.cpp:681`).
- **Reads sim outputs back**: `ocean.sampleSurfaceHeight(x,z)` (`main.cpp:687`) feeds boat buoyancy;
  `ocean.getDisplacementTexture()`, `ocean.getNormalTexture()` (`main.cpp:800–802`) are bound for
  rendering; `ocean.getOceanSize()` is passed as the `oceanSize` uniform (`main.cpp:815`, `825`).
- **Tunes the sim** through getters/setters wired to keys (`setWindSpeed`, `setHeightScale`,
  `setTimeScale`, `setChoppiness`) and the ImGui *Water* panel (`setWindAngle`,
  `setHorizontalScale`, `setSeaMaturity`, `setAmplitude`).
- **Draws the surface** with `oceanMesh.draw(shader)` / `oceanMesh.draw(debugWireframeShader)`
  (`main.cpp:818`, `852`).

### Into Q3 (Rendering Engine, Camera & Scene Objects)
- **Constructs** `Window` (`main.cpp:129`), `Camera` (`main.cpp:145`), several `Shader`s
  (`main.cpp:147–152`), and several `Model`s — `rock`, `jetski`, `yacht`, `bigShip`, `coastalCliff`
  (`main.cpp:219`, `232–234`, `291`).
- **Uses Q3 shaders** `object.vert`/`object.frag` (`objectShader`) and `skybox.vert`/`skybox.frag`
  (`skyboxShader`).
- **Drives Q3** via `window.shouldClose()`, `window.getGLFWWindow()`, `window.getWidth/Height()`,
  `window.swapBuffers()`, `window.pollEvents()`; `camera.GetViewMatrix()`, `camera.ProcessKeyboard()`,
  `camera.ProcessMouseMovement()`, `camera.Position`, `camera.Front`; and `model.draw()`,
  `model.setPosition/Scale/Orientation/RotationY()`, `model.localCenter()`, `model.loaded()`.
- **Falls back** to Q4's `generateRock()` to feed a `Model(vertices, indices)` when the glTF rock
  fails (`main.cpp:220–225`).

### Into Q4 (Water Interactions & Procedural Geometry)
- **Constructs** `GPUDisturbance disturbance(kDisturbResolution, kOceanMeshRes * kOceanMeshTile)`
  (`main.cpp:214`), `GPURain rainSystem(120000u)` (`main.cpp:341`), and one `BoatPhysics phys` member
  inside each `Vehicle` (`main.cpp:251`).
- **Loads Q4 shaders** `rain_gpu.vert`/`rain_gpu.frag` (`rainShader`) and
  `rain_splash.vert`/`rain_splash.frag` (`rainSplashShader`) at `main.cpp:149–150`.
- **Drives the disturbance field**: `disturbance.update(kFixedDt)` per substep (`main.cpp:554`),
  `disturbance.disturb(...)` for the C-key splash (`main.cpp:490`) and per-vehicle wake injection
  (`main.cpp:638`); reads `disturbance.getHeightTexture()` for rendering (`main.cpp:804`).
- **Drives rain**: `rainSystem.update(...)` once per frame (`main.cpp:676`), `rainSystem.render(...)`
  (`main.cpp:906`), `rainSystem.getActiveRipples()` (`main.cpp:808`) → packed into the ripple SSBO.
- **Drives buoyancy**: per frame sets `phys.buoyancy/linearDamp/angularDamp` then calls
  `phys.step(deltaTime, pos, yaw, surfFn)` (`main.cpp:689–692`), reading back `phys.position` and
  `phys.orientation` for rendering (`main.cpp:876`, `884`).
- **Calls** Q4's `generateRock()` (`main.cpp:223`) as the procedural rock fallback.

### Data Q1 produces for the shaders directly
- **GL texture units** assigned at render (`main.cpp:799–806`): unit **0** = sky cubemap,
  **1** = ocean displacement, **2** = ocean normal, **3** = disturbance height. The sampler→unit
  bindings are set once at startup (`main.cpp:153–167`).
- **SSBO binding 4** = the rain-ripple buffer (`rippleSSBO`), uploaded each frame by `uploadRipples`
  and read by `standard.frag` (`main.cpp:101–124`).
- **`numRipples`** uniform (count of live ripples) is set alongside the SSBO upload (`main.cpp:112`).

---

## 🗂️ Files in this quarter

| File | Lines (approx) | One-line role |
|---|---:|---|
| `src/main.cpp` | 950 | The whole application: startup wiring, fixed-timestep loop, input, navigation, ImGui panel, render order, PNG recording. |
| `CMakeLists.txt` | 66 | Build definition: C++17, source list, vendored GLFW/GLAD/ImGui, include dirs, link libs, warning flags. |
| `build_and_run.sh` | 5 | One-shot Linux convenience script: configure (Release, no Wayland), build, run. |
| `.gitignore` | 34 | Keeps build output, binaries, frame captures, IDE/OS cruft out of version control. |

---

## 🔬 Deep dive

### `src/main.cpp`

This single translation unit *is* the application. It has three top-level pieces: a free function
`loadCubemap`, an anonymous-namespace block of constants + ripple-SSBO helpers, and `main()`.

#### Includes & STB single-header definitions (`main.cpp:1–36`)
- Lines `1–10` pull in the public headers from all four quarters: Q3 `core/Window.h`,
  `core/Camera.h`, `graphics/Shader.h`, `graphics/Model.h`; Q4 `graphics/RockGenerator.h`,
  `ocean/GPUDisturbance.h`, `water/GPURain.h`, `water/BoatPhysics.h`; Q2 `ocean/GPUFFTOcean.h`,
  `ocean/OceanMesh.h`.
- Lines `12–14` pull in Dear ImGui + its GLFW and OpenGL3 backends.
- `cstdlib` (line `24`) is included explicitly with a comment: `rand()`/`RAND_MAX` are *not*
  transitively guaranteed on MSVC. `filesystem` (line `25`) is for `create_directories` in the
  recording path.
- **Crucial single-header trick** (`main.cpp:32–36`): `STB_IMAGE_WRITE_IMPLEMENTATION` and
  `STB_IMAGE_IMPLEMENTATION` are `#define`d *in this file only*, so the STB image-read and
  image-write implementations are compiled exactly once into the program here. (Other files use the
  STB headers without defining the implementation macros.)

#### `loadCubemap(const std::vector<std::string>& faces) -> unsigned int` (`main.cpp:39–75`)
Builds an OpenGL cube-map texture for the skybox.
- `glGenTextures` + bind to `GL_TEXTURE_CUBE_MAP`.
- `stbi_set_flip_vertically_on_load(false)` (line `44`): cube-map faces must **not** be flipped (the
  cube-map convention differs from normal 2-D textures).
- Loops the 6 faces, picks `GL_RED`/`GL_RGB`/`GL_RGBA` from the channel count, and uploads each into
  `GL_TEXTURE_CUBE_MAP_POSITIVE_X + i` (the 6 cube-map face enums are contiguous, so `+i` walks them
  in the order the caller supplied the faces).
- Filtering = `GL_LINEAR`; wrap = `GL_CLAMP_TO_EDGE` on S/T/R so face seams don't bleed.
- **Gotcha**: on a failed load it prints the path and still calls `stbi_image_free(data)` with a
  null `data`; STB tolerates a null free, so this is safe but produces an undefined/empty face.

#### Anonymous-namespace constants (`main.cpp:77–96`)
All `constexpr`, so they have internal linkage and fold at compile time:

| Constant | Value | Meaning / why |
|---|---|---|
| `kFixedDt` | `1.0f/60.0f` | Fixed physics timestep (≈16.67 ms). The substep size. |
| `kMaxAccumulatedTime` | `0.25f` | Accumulator clamp — caps how much sim time one slow frame can request, preventing a "spiral of death" where the sim falls further behind every frame. |
| `kOceanResolution` | `512` | Stockham FFT grid size (true 512², matches the achalpandeyy/OceanFFT reference). |
| `kOceanMeshRes` | `1024` | Vertex grid of the water mesh: 1024×1024. |
| `kOceanMeshTile` | `1.0f` | Metres per tile → ocean patch is `1024 × 1.0 = 1024 m` wide; 1 m spacing (vs an old 2 m) is what lets fine FFT detail show in geometry. |
| `kPhysicsGridSize` | `150` | (Declared; legacy CPU physics grid for rain ripples.) |
| `kPhysicsTileSize` | `1.0f` | (Declared; companion to the above.) |
| `kMaxRipples` | `512` | Max ripple entries packed into the SSBO; each is one fragment-shader loop iteration. |
| `kWindowWidth` / `kWindowHeight` | `1024` / `768` | Framebuffer size; also used by the readback/recording path. |
| `kDisturbResolution` | `256` | Disturbance (wake) map resolution; **must** match the `GPUDisturbance` ctor and the `256.0` divisor in `standard.frag`. |

> The ocean's world size is computed as `kOceanMeshRes * kOceanMeshTile` (= 1024 m) and passed to
> **both** `GPUFFTOcean` and `GPUDisturbance` so the two maps cover exactly the same world extent.

#### Ripple SSBO helpers (`main.cpp:98–124`)
- `GLuint rippleSSBO` (line `98`) and `glm::vec4 rippleStagingBuf[kMaxRipples]` (line `99`) are
  file-static; the staging buffer is **pre-allocated** so there is *no heap allocation per frame*.
- `initRippleSSBO()` (`101–107`): generates the buffer, allocates `kMaxRipples * sizeof(glm::vec4)`
  bytes as `GL_DYNAMIC_DRAW`, and binds it to **SSBO binding point 4** via `glBindBufferBase`.
- `uploadRipples(const Shader& shader, const std::deque<glm::vec3>& ripples)` (`109–124`):
  - Sets the `numRipples` uniform to `count = min(total, kMaxRipples)` (line `112`); early-returns if
    zero.
  - Packs the **newest** `count` ripples (offset `total - count`, line `116`) into the staging buffer
    as `vec4(xyz, 0)`. Keeping the newest matters because the disturbance `std::deque` grows over
    time and only the freshest rings are still visually relevant.
  - `glBufferSubData` uploads exactly `count * sizeof(vec4)` bytes, re-binds base 4, unbinds.
  - **Gotcha**: each ripple costs one loop iteration *per fragment* in `standard.frag`, so
    `kMaxRipples` is deliberately a safe loop budget rather than "as many as possible". The comment
    at `main.cpp:88–90` notes the SSBO route was chosen because uniform arrays hit the
    constant-register limit.

#### `main()` — startup phase

The init order is deliberate; each step depends on the previous ones existing.

1. **Window + GL context** (`129–135`). `Window window(kWindowWidth, kWindowHeight, …)` creates the
   GLFW window and loads GL. Early-out `if (!window.getGLFWWindow()) return -1`. Cursor set to
   `GLFW_CURSOR_NORMAL` (the panel is interactive at startup); `GL_TEXTURE_CUBE_MAP_SEAMLESS` enabled
   so the skybox doesn't show cube edges.
2. **Dear ImGui** (`137–143`). `IMGUI_CHECKVERSION`, create context, enable keyboard nav,
   dark style, `ImGui_ImplGlfw_InitForOpenGL(win, true)` (the `true` installs GLFW callbacks —
   safe here because the app polls input itself), and `ImGui_ImplOpenGL3_Init("#version 460 core")`.
   ImGui must come after the GL context exists.
3. **Camera** (`145`). `Camera camera(glm::vec3(0, 40, 120))` — starts above and behind the origin
   looking at the bay.
4. **Shaders** (`147–167`). Seven programs are compiled:
   - `shader` = `standard.vert`+`standard.frag` (the water surface, Q2).
   - `debugWireframeShader` = `standard.vert`+`debug_wireframe.frag` (wireframe water).
   - `rainShader` = `rain_gpu.*`, `rainSplashShader` = `rain_splash.*` (Q4).
   - `skyboxShader` = `skybox.*`, `objectShader` = `object.*` (Q3).
   Then the **sampler→unit bindings** are set once: `objectShader.skybox = 0`; `shader` gets
   `skybox=0, displacementMap=1, normalMap=2, disturbanceMap=3`; `debugWireframeShader` gets
   `displacementMap=1, disturbanceMap=3`; `skyboxShader.skybox=0`. These constant assignments are the
   contract the per-frame `glActiveTexture` calls (`799–806`) must honour.
5. **Skybox VAO/VBO** (`169–193`). A hard-coded 36-vertex unit cube (`skyboxVertices`,
   `170–183`) is uploaded to `skyboxVBO`, with attribute 0 = `vec3` position, stride `3*sizeof(float)`.
6. **Cubemaps** (`195–209`). Four sky sets — `skyNames[]`/`skyDirs[]` = `sky_1/2/3`, `environment_1`.
   For each, six face PNGs are assembled in the order `right,left,top,bottom,front,back` (the order
   `loadCubemap` walks the `+i` face enums) and loaded into `skyTextures[si]`. `currentSky = 0`;
   `cubemapTexture = skyTextures[0]`.
7. **Ocean trio** (`211–214`). `GPUFFTOcean ocean`, `OceanMesh oceanMesh`, `GPUDisturbance
   disturbance` — see the Q2/Q4 hand-offs above. Both `ocean` and `disturbance` receive the same
   `kOceanMeshRes * kOceanMeshTile` world size.
8. **Rock** (`216–227`). Loads the Poly Haven marble cliff glTF into `Model rock`. If
   `!rock.loaded()`, it falls back to the procedural `generateRock(3u, 1u, …)` (Q4) and rebuilds the
   model from raw vertices/indices. Scaled ×4, positioned at `(60, -3, 30)` (partly out of the water).
9. **Vehicles** (`229–275`). Loads `jetski`, `yacht`, `bigShip` glTFs. Defines the `Vehicle` struct
   (model pointer, `pos`, `heading`, `speed`, `scale`, `yOffset`, `modelYaw`, `wakeWidth`,
   `hullLength`, `wakeScale`, runtime `turnVel`/`bankAngle`, and an embedded `BoatPhysics phys`).
   A `frand(a,b)` lambda gives uniform randoms. The three vehicles are initialized with hand-tuned
   per-model values; the comments explain the `scale` and `modelYaw` differences:
   - The jet-ski's glTF node matrix bakes a 0.01 FBX scale, so it needs a *much* larger multiplier
     (120) and a `-π/2` `modelYaw` correction.
   - The yacht is modelled facing backward, so `modelYaw = +π`; the big ship is already correct (`0`).
   Then a loop copies hull dimensions into each `phys`: `phys.length = hullLength`,
   `phys.beam = wakeWidth*2`, `phys.floatHeight = yOffset` (`271–275`).
10. **Containment constants** (`277–319`). `vehiclesMoving`/`pKeyWasDown` (P toggle).
    `kSoftRadius=300` (steer-back ramp), `kHullLimit=400` (hard cap; cliff faces sit ~440),
    `kRockXZ=(60,30)`, `kRockAvoid=70`, `kRockHard=45`. Then the **coastal cliff ring**: one
    `coastalCliff` model is reused for 8 `MountainInstance`s (pos/scale/rotY). `kEdge=470`,
    `kCorner=kEdge*0.7071` (so corners sit at the *same radius* as edges → uniform ring, no diagonal
    gap), `kCliffScale=9`. The `faceIn(px,pz)` lambda returns `atan2(-px,-pz) + kFaceOffset` so each
    cliff's local +Z face aims back at the origin.
11. **Safe spawns** (`321–338`). The random `frand(-250,250)` start positions could land on the rock,
    outside the play zone, or on top of each other, so each vehicle gets up to 200 rejection-sampling
    tries to find a point inside radius 200, clear of the rock (`kRockHard + ri + 20`), and clear of
    previously-placed boats (`ri + rj + 30`).
12. **Rain + ripple SSBO** (`340–342`). `GPURain rainSystem(120000u)` (max drops the GPU buffer
    holds) and `initRippleSSBO()`.
13. **Loop state + tuning defaults** (`344–408`). Timing (`deltaTime`, `lastFrame`), mouse state
    (`lastX/Y`, `firstMouse`, `mouseCaptured`, `escWasPressed`), key-edge flags
    (`tKeyWasPressed`…`cKeyWasPressed`), recording state (`isRecording`, `frameCount`,
    `kMaxFrames=600`), buoyancy tuning (`buoyancyStrength=3`, `buoyancyResponse=2`,
    `pitchRollDamp=2.8`, `wakeStrength=0.01`), nav realism (`boatTurnRate=0.45`, `boatWander=0.12`,
    `boatBank=0.1`), the live rain UI vars, the water-look colours/params, and the lighting params
    (sun azimuth/elevation, glint/glitter, HDR exposure). Finally GL state: clear colour, depth test
    on, alpha blending `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`.

#### `main()` — the frame loop (`410–938`)

**Frame timing** (`411–413`): `deltaTime = now - lastFrame`. This `deltaTime` drives the
fixed-timestep accumulator and the *per-frame* (not per-substep) systems.

**ImGui new-frame + input gating** (`417–424`): begins the ImGui frame, then toggles
`ImGuiConfigFlags_NoMouse` based on `mouseCaptured`. While the camera owns the (locked, centred)
cursor, ImGui must ignore it; the moment you release with Esc, the panel is interactive again.

**Mouse capture** (`429–436`): a left-click on the 3-D view (`!io.WantCaptureMouse`, i.e. *not* the
panel) switches to `GLFW_CURSOR_DISABLED` and enables raw mouse motion if supported — first-person
look with unbounded virtual deltas. `firstMouse=true` prevents a look-jump on capture.

**Esc** (`438–448`, edge-triggered via `escWasPressed`): releases the cursor if captured, otherwise
sets the window to close.

**Key toggles** (edge-triggered with `…WasPressed` flags so a held key fires once):
- **T / V** (`450–458`): toggle `wireframeMode`.
- **K / L** (`460–472`): wind speed ±0.5, clamped to `[5,10]`, via `ocean.setWindSpeed`.
- **WASD / Space / Shift** (`474–482`): camera movement, *only while `mouseCaptured`*. The integer
  arg to `camera.ProcessKeyboard` encodes the direction (0=fwd,1=back,2=left,3=right,4=up,5=down).
- **C** (`484–492`): fires a disturbance 40 m in front of the camera. It flattens `camera.Front` to
  the XZ plane, projects the impact point, and calls `disturbance.disturb(impactXZ, cKeySplash)`.
- **P** (`494–500`): toggles `vehiclesMoving`.

**Live ocean tuning** (`502–522`, *level-triggered* — hold to ramp): arrows tune height/time scale,
N/M tune choppiness, all rate-limited by `kTuneRate = deltaTime*3` and clamped. A throttled
`tunePrintTimer` (0.25 s) prints the current values.

**Recording start** (`524–532`): pressing **R** (when not already recording) ensures `frames/`
exists with `std::filesystem::create_directories` (STB silently fails writing to a missing dir),
sets `isRecording=true`, `frameCount=0`.

**Mouse-look** (`534–543`): reads cursor pos; while captured, feeds deltas to
`camera.ProcessMouseMovement` (note Y is inverted: `lastY - ypos`). `lastX/Y` updated every frame.

**Rain wind drift** (`545–547`): `windDir` (deg) → a 2-D drift vector
`(cos, sin) * windStrength * 25`, used by both rain update and render.

**The fixed-timestep accumulator** (`549–671`) — the heart of deterministic simulation:
```
static float accumulator = 0;
accumulator = min(accumulator + deltaTime, kMaxAccumulatedTime);
while (accumulator >= kFixedDt) {
    ... run one fixed substep of size kFixedDt ...
    accumulator -= kFixedDt;
}
```
Each substep runs:
- `ocean.update(kFixedDt)` (Q2) and `disturbance.update(kFixedDt)` (Q4).
- **Vehicle navigation** (`562–668`, only when `vehiclesMoving`). For each vehicle:
  1. Advance position: `dir = (sin h, 0, cos h)`, `pos += dir * speed * kFixedDt`.
  2. Build a `desired` heading vector starting from current `dir`, then blend three steering urges:
     - **(a) Boundary** (`576–579`): once `distXZ > 0.7*kSoftRadius`, add a centre-pull weighted
       `w*2.5` where `w` ramps 0→1 over the outer 30 % of `kSoftRadius`.
     - **(b) Separation** (`581–588`): push away from every other boat within
       `ri + rj + 30`, weighted `(1 - dd/keep) * 2.2`.
     - **(c) Rock** (`589–596`, only if `showCenterRock`): push away from `kRockXZ` within
       `kRockAvoid + ri`, weighted `(1 - dd/keep) * 3.0`.
  3. **Rate-limited turn** (`600–607`): convert `desired` to a target yaw `atan2(nd.x, nd.y)`, take
     the signed shortest delta `dh`, and clamp the per-step turn to `boatTurnRate * (1 + 3*urgency)
     * kFixedDt`, where `urgency` ramps up as the hull approaches `kHullLimit - 90`.
  4. **Gentle weave** (`608–610`): only when comfortably inside `0.7*kSoftRadius`, add a small
     `boatWander * sin(t)` heading wobble so paths curve naturally.
  5. **Banking input** (`612`): `turnVel = (heading - prevHeading) / kFixedDt`.
  6. **Wake injection** (`614–640`): compute the *visible* hull centre by rotating the model's
     `localCenter()` (boat meshes can sit off their glTF origin), then offset *behind the transom*
     by `hullLength*0.5 + sigmaM*0.6` to the stern point. The footprint is sized to the hull:
     `kTexelM = (kOceanMeshRes*kOceanMeshTile)/kDisturbResolution ≈ 4 m`,
     `sigmaTexels = clamp(wakeWidth/kTexelM, 1.5, 5)`, `sigmaM = sigmaTexels*kTexelM`. Then
     `disturbance.disturb(sternXZ, wakeStrength * wakeScale * min(1, speed/12), sigmaTexels)`.
- **Hard safety net** (`643–668`, runs after the per-boat steering, still inside the substep):
  - Pairwise push apart any overlapping boats (gap = sum of half-lengths + 10).
  - Then containment has the final say: never inside `kRockHard + ri` of the rock; never past
    `kHullLimit - ri` from centre (radial clamp `p *= cap/d`).

> **Why this split?** XZ navigation must be *deterministic* (frame-rate independent), so it lives in
> the fixed substep. Heave/pitch/roll (the visual bob) is stepped once per frame *after* the ocean
> readback because it needs the freshest CPU heights and tolerates a variable dt.

**Per-frame systems** (after the accumulator drains):
- `rainSystem.update(deltaTime, camera.Position, rainWindDrift, rainFallSpeed, rainSpawnRate,
  rippleLifetime)` (`674–677`) — runs *once per frame*, not per substep. The comment explains: drops
  are purely visual, so one compute dispatch over the whole frame dt looks identical but avoids
  redundant dispatches on slow frames.
- `ocean.readbackDisplacement()` (`679–681`) — copies the ocean displacement to the CPU *once* after
  all substeps, so object height sampling reads the final state.
- **Buoyancy** (`683–694`): a `surfFn` lambda wraps `ocean.sampleSurfaceHeight`. For each vehicle it
  pushes the UI tuning into `phys` (`buoyancy`, `linearDamp`, `angularDamp`) and calls
  `phys.step(deltaTime, pos, heading + modelYaw, surfFn)`. Note the yaw passed in already includes
  `modelYaw` so the physics hull aligns with the rendered model.

**ImGui control panel** (`696–787`): one `Begin("Controls")` window showing FPS, then four collapsing
headers:
- **Rain** (`DefaultOpen`, `702–719`): Intensity (spawn rate, fall speed, drop size, opacity); Wind
  (direction — also calls `ocean.setWindAngle`; strength); Splash & ripples (splash height, ripple
  lifetime, ring speed, ring strength).
- **Water** (`721–751`): Waves (wind speed, wave height, choppiness, horizontal displace, time
  scale — all via `ocean` setters); Spectrum (sea maturity, overall amplitude — rebuild on change);
  Surface (depth tint falloff, mid-wave detail); Colour (deep/shallow); Interaction (boat wake
  strength, C-key splash).
- **Lighting** (`753–772`): Sky combo (switches `cubemapTexture`); reflection strength + horizon
  tint; sun azimuth/elevation/colour/glint/glitter; scatter colour + HDR exposure.
- **Vehicles** (`774–782`): a speed slider per boat (`PushID(i)` keeps slider identities distinct).
- **Scene** (`783–785`): the `Central rock` checkbox (`showCenterRock`).

**Render** (`789–909`):
- Clear colour+depth (`790`).
- Build `projection` (45° FOV, near 0.1, far 4000) and `view = camera.GetViewMatrix()`.
- **Texture-unit binding** (`799–806`): unit 1 = `ocean.getDisplacementTexture()`, unit 2 =
  `ocean.getNormalTexture()`, unit 3 = `disturbance.getHeightTexture()`, unit 0 = `cubemapTexture`.
  This matches the sampler bindings set at startup.
- Grab `activeRipples = rainSystem.getActiveRipples()` (`808`).
- **Wireframe branch** (`810–819`): `glPolygonMode(GL_LINE)`, use `debugWireframeShader`, set
  projection/view/`oceanSize`/`floorY=-10000`/`applyOceanDisplacement=1`, draw the ocean mesh, then
  restore `GL_FILL`.
- **Shaded branch** (`820–887`):
  - `shader.use()`, set projection/view/`viewPos`/`oceanSize`/`floorY`/`time`/
    `applyOceanDisplacement`, ripple-ring uniforms (`uRippleLifetime`, `uRingSpeed`, `uRingStrength`).
  - Compute `sunDir` from azimuth/elevation and set the full block of water-look + lighting uniforms
    (`uDeepColor`, `uShallowColor`, `uDepthFalloff`, `uMidWaveDetail`, `uReflectStrength`,
    `uHorizonColor`, `uScatterColor`, `uSunColor`, `uSunDir`, `uSunGlint`, `uSunGlitter`,
    `uExposure`).
  - `uploadRipples(shader, activeRipples)` then `oceanMesh.draw(shader)`.
  - **Solid objects** before transparent passes: `objectShader` set up with projection/view/viewPos,
    grey `baseColor`, cubemap on unit 0. Draw `rock` (if `showCenterRock`); draw the 8 cliff
    instances by re-positioning/scaling/rotating the single `coastalCliff` model; draw each vehicle
    by reading `phys.position`/`phys.orientation`, applying a smoothed `bankAngle` lean
    (`bankTarget = clamp(-turnVel*speed*boatBank, -0.35, 0.35)`, eased by `min(1, dt*3)`), composing
    the bank quaternion with the physics orientation, and `model->draw(objectShader)`.
- **Skybox + rain** (`889–909`, only when not wireframe): the skybox draws at the far plane with
  `glDepthMask(GL_FALSE)` + `GL_LEQUAL`, using only the rotation part of the view matrix
  (`mat3(view)`), 36 vertices; then depth is restored. `rainSystem.render(...)` draws streaks +
  splashes last (they're transparent).

**Recording** (`911–930`): when `isRecording`, `glReadPixels` reads the framebuffer as `GL_RGB`,
the rows are vertically flipped into `flippedPixels` (OpenGL's origin is bottom-left, PNG's is
top-left), and `stbi_write_png` writes `frames/frame_<n>.png`. After `kMaxFrames` (600) frames it
stops. **Gotcha**: two `new[]` + `delete[]` happen *every recorded frame* — fine for a capture tool,
but it would be a per-frame allocation in the hot path if recording were always on.

**Present** (`932–937`): `ImGui::Render()` + `ImGui_ImplOpenGL3_RenderDrawData` draw the panel on
top, then `window.swapBuffers()` and `window.pollEvents()`.

**Shutdown** (`940–948`): ImGui backend + context teardown, delete the skybox VBO/VAO and the
current cubemap texture, `return 0`.

> **Note**: only `cubemapTexture` (the *currently selected* sky) is deleted at shutdown; the other
> three entries of `skyTextures[]` leak. Harmless on process exit but worth knowing.

### `CMakeLists.txt`

The build definition (CMake ≥ 3.15, project `RealWaterSimulator`).
- **Standard** (`4–5`): C++17, required (no silent fallback).
- **`SOURCES`** (`7–21`): every project `.cpp` across all four quarters — `main.cpp`, the Q3 core
  (`Window`, `Camera`) and graphics (`Shader`, `Mesh`, `Model`, `Texture`, `RockGenerator`), the Q2
  ocean (`GPUFFTOcean`, `OceanMesh`), Q4 (`GPUDisturbance`, `GPURain`, `BoatPhysics`). Shaders are
  *not* listed — they're loaded at runtime from `../assets/shaders/`.
- **`add_executable`** (`23`) and `set_target_properties` (`25–31`): force every build-config's
  runtime output into `${CMAKE_BINARY_DIR}` (so the binary always lands in the build root regardless
  of Debug/Release/etc.). This is why the app expects `../assets/...` relative paths — it runs from
  inside the build dir.
- **Dependencies** (`33–34`): `find_package(OpenGL REQUIRED)` and `add_subdirectory(external/glfw)`
  (GLFW is built from vendored source).
- **Include dirs** (`36–42`): `src`, `external`, `external/glad/include`, `external/imgui`,
  `external/imgui/backends`.
- **Extra sources** (`44–53`): compiled into the target — `external/glad/src/glad.c` and the six
  Dear ImGui (v1.91.9) TU + backend `.cpp` files. ImGui is built *into* the executable, not linked
  as a library.
- **Link libs** (`55–58`): `glfw`, `OpenGL::GL`.
- **Linux specifics** (`60–65`): under `UNIX AND NOT APPLE`, also link `dl` (GLAD's `dlopen`) and
  `pthread` (GLFW), and enable `-Wall -Wextra -Wno-unused-parameter` on GCC/Clang.

> **Gotcha**: assets are referenced as `../assets/...` because the executable runs from the build
> directory (one level below the repo root). Run it anywhere else and texture/shader/model loads
> fail.

### `build_and_run.sh`

A 5-line Linux convenience script:
```bash
#!/usr/bin/env bash
cd /home/bora/Projects/learning/RealWaterSimulatorOpenGL/build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF
make -j$(nproc)
./RealWaterSimulator
```
- `cd build_linux` (the out-of-source build dir; matches the `.gitignore` entry).
- `cmake .. -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF` — Release build; **Wayland off**
  forces the X11 backend (per the machine build notes, the Wayland backend wasn't wanted here).
- `make -j$(nproc)` — parallel build across all cores.
- `./RealWaterSimulator` — runs from `build_linux`, so `../assets/...` resolves to the repo root.

> **Gotcha**: the `cd` path is hard-coded to this machine's checkout and the `build_linux` directory
> must already exist (the script does not `mkdir -p` it). It is a personal convenience script, not a
> portable build.

### `.gitignore`

Keeps generated and machine-specific files out of the repo:
- **Build output** (`1–6`): `build/`, `build_linux/`, `build_win/`, `out/`, `cmake-build-*/`.
- **Binaries/objects** (`8–18`): `*.o *.obj *.exe *.pdb *.ilk *.exp *.lib *.a *.so *.dylib`.
- **Recordings** (`20–22`): `frames/` (the PNG captures from the R key), `output.mp4`, `*.mp4` (the
  ffmpeg result). This is why the recording path can freely write into `frames/`.
- **IDE/editor** (`24–29`): `.vs/ .vscode/ .idea/ *.user`.
- **OS cruft** (`31–33`): `.DS_Store`, `Thumbs.db`.

---

## ⚠️ Gotchas & invariants

- **Texture-unit contract**: the sampler bindings set once at startup (`skybox=0`,
  `displacementMap=1`, `normalMap=2`, `disturbanceMap=3`) *must* match the per-frame
  `glActiveTexture` order (`main.cpp:799–806`). Change one without the other and the water samples the
  wrong texture.
- **SSBO binding 4** is shared between `initRippleSSBO`/`uploadRipples` (C++) and `standard.frag`
  (GLSL `layout(std430, binding=4)`). The element type must stay `vec4` (xyz used, w padding) on both
  sides.
- **`kDisturbResolution = 256` is coupled three ways**: the `constexpr`, the `GPUDisturbance`
  constructor argument, and the literal `256.0` divisor inside `standard.frag`. All three must agree
  or the wake samples misalign.
- **Ocean world size must match**: both `GPUFFTOcean` and `GPUDisturbance` are constructed with the
  same `kOceanMeshRes * kOceanMeshTile` (= 1024 m). The wake texel size `kTexelM` and the shader's
  UV mapping assume this equality.
- **Fixed-step vs per-frame split**: `ocean.update`, `disturbance.update`, and vehicle *navigation*
  run **per substep** (`kFixedDt`); `rainSystem.update`, `ocean.readbackDisplacement`, and
  `phys.step` (buoyancy) run **once per frame** with the variable `deltaTime`. Moving any of these
  across the boundary changes determinism or correctness.
- **Accumulator clamp** `kMaxAccumulatedTime = 0.25` prevents a slow-frame spiral of death; never
  remove it, or one long stall would queue an unbounded number of substeps.
- **Readback before buoyancy**: `ocean.readbackDisplacement()` (`681`) must run *after* the
  accumulator loop and *before* `phys.step` (`692`), so buoyancy samples the final ocean state.
- **Wake injection point**: the wake is injected behind the *visible hull centre* (using
  `model->localCenter()` + `modelYaw`), not the raw glTF origin. Some boat meshes sit off-origin;
  using the raw position made the wake appear to one side.
- **`modelYaw` is added in two places consistently**: navigation uses raw `heading`, but wake
  rotation, buoyancy yaw (`phys.step`), and the rendered orientation all use `heading + modelYaw`.
- **Edge-triggered vs level-triggered keys**: toggles (T/V/K/L/C/P, R) use `…WasPressed` edge
  detection (fire once per press); ocean tuning (arrows/N/M) is level-triggered (ramps while held).
- **Camera moves only while `mouseCaptured`**: WASD/Space/Shift are gated so panel interaction never
  drifts the camera. ImGui's `NoMouse` flag is toggled in lock-step with capture.
- **Recording dir must exist**: `stbi_write_png` silently no-ops if `frames/` is missing, which is
  why R calls `create_directories("frames")` first.
- **PNG vertical flip**: `glReadPixels` returns bottom-up; the recording path manually flips rows
  before `stbi_write_png`.
- **STB implementation macros live only in `main.cpp`**: `STB_IMAGE_IMPLEMENTATION` and
  `STB_IMAGE_WRITE_IMPLEMENTATION` are defined here exactly once. Defining them elsewhere too would
  cause duplicate-symbol link errors.
- **Asset paths are relative to the build dir** (`../assets/...`). The CMake
  `RUNTIME_OUTPUT_DIRECTORY` settings + `build_and_run.sh` running from `build_linux/` are what make
  these resolve.

---

## 🧠 Mental-model recap

- `main()` is the *director*: it instantiates every subsystem from all four quarters, then loops:
  **input → fixed-step sim → per-frame sim → render → present**.
- Two clocks run: a **fixed 1/60 s** clock (ocean, disturbance, boat navigation, containment) for
  determinism, and the **wall-clock `deltaTime`** (rain, ocean readback, buoyancy, camera, banking)
  for things that just need to look smooth.
- The accumulator pattern: bank up real time, spend it in `kFixedDt` chunks, clamp the bank at
  `kMaxAccumulatedTime` so a stutter can never cause a runaway.
- Subsystems talk through **GPU textures** (units 0–3), one **SSBO** (binding 4 = rain ripples), a
  pile of **uniforms** set per frame, and a few **CPU readbacks** (ocean heights → buoyancy).
- Boat motion is two layers: **horizontal AI navigation** (boids-style steer-back + separation +
  rock avoidance + hard clamps, in the fixed step) and **vertical rigid-body buoyancy** (`BoatPhysics`,
  per frame). Rendering reads the physics result and adds a cosmetic bank into turns.
- Each boat injects a wake into the disturbance map sized to its hull (`kTexelM`-based sigma) and
  placed behind its transom, so a jet-ski leaves a thin ripple and a ship a broad swell.
- The ImGui panel mostly mutates plain C++ variables that become uniforms next render; a few sliders
  call ocean setters that may rebuild the spectrum.
- The skybox draws last (far plane, no depth write) and rain last of all (transparent), after the
  opaque water/objects.
- Pressing R captures up to 600 framebuffer PNGs into `frames/` (flipped vertically), later turned
  into a video with ffmpeg.
- The build is plain CMake: one executable, vendored GLFW/GLAD/ImGui compiled in, runs from the build
  directory so `../assets/...` resolves.

---

## 📖 Glossary

- **Fixed timestep**: advancing the simulation in constant-size time chunks (`kFixedDt = 1/60 s`)
  independent of render frame rate, for reproducibility.
- **Accumulator**: a running total of unspent real time; the loop drains it in `kFixedDt` substeps.
- **Spiral of death**: when each frame is so slow it queues more sim work than the next frame can
  clear, falling ever further behind — prevented here by `kMaxAccumulatedTime`.
- **Substep**: one iteration of the fixed-step inner `while` loop.
- **Edge-triggered input**: acting only on the press *transition* (was-up → now-down), so a held key
  fires once.
- **Level-triggered input**: acting every frame the key is held (used for "hold to ramp" tuning).
- **VAO (Vertex Array Object)**: GL object recording vertex-attribute layout (here, the skybox cube).
- **VBO (Vertex Buffer Object)**: GPU buffer holding vertex data (the skybox vertices).
- **Cubemap**: a 6-face texture sampled by a 3-D direction; used for the sky reflections/background.
- **Texture unit**: a numbered GPU slot a texture is bound to; shaders' sampler uniforms reference a
  unit number (0–3 here).
- **SSBO (Shader Storage Buffer Object)**: a large, shader-readable/writable GPU buffer; here binding
  4 holds the rain-ripple list, chosen over uniform arrays to dodge the constant-register limit.
- **Uniform**: a per-draw constant passed from CPU to a shader (matrices, colours, scalars).
- **`std430` layout**: the SSBO memory-packing rule the GPU expects; matched on the C++ side by
  uploading `glm::vec4`s.
- **PBO (Pixel Buffer Object)**: a GPU buffer for asynchronous pixel transfer; *not* used here — the
  recording path uses a synchronous `glReadPixels` instead.
- **Compute shader / dispatch**: a GPU program run over a grid of work items; Q1 triggers these only
  indirectly (e.g. `rainSystem.update`), the dispatches themselves live in Q2/Q4.
- **Quaternion**: a 4-number rotation representation (no gimbal lock, smooth to blend); used for boat
  orientation and the `glm::angleAxis` banking lean.
- **Boids**: Reynolds' flocking model (steering from simple urges like separation/cohesion);
  inspiration for the vehicle navigation.
- **Heave / pitch / roll**: vertical bob, nose up-down, side-to-side lean of a floating body — the
  3 vertical degrees of freedom `BoatPhysics` solves (the other 3 — surge/sway/yaw — come from the
  navigation layer).
- **Buoyancy**: upward force proportional to submerged volume; here approximated per facet against
  the sampled ocean surface.
- **Banking**: leaning a vehicle into a turn for a coordinated-turn look; cosmetic, applied at render.
- **Containment / hard clamp**: a final position correction that guarantees an invariant (no
  cliff/rock penetration, no boat overlap) regardless of what the soft steering produced.
- **Readback**: copying GPU data back to CPU memory (`ocean.readbackDisplacement`) so the CPU
  (buoyancy) can sample it.
- **Skybox**: a textured cube drawn at the far plane to represent the distant environment.
- **HDR exposure**: a tone-mapping scalar converting high-dynamic-range lighting to displayable
  values.
- **glTF**: the JSON+binary 3-D asset format the boats/rock/cliffs are loaded from (via cgltf, Q3).
- **STB single-header**: header-only C libraries (image load/write) whose implementation is enabled
  by a one-time `#define ..._IMPLEMENTATION` (done in `main.cpp`).
- **Out-of-source build**: building into a separate directory (`build_linux/`) so generated files
  never pollute the source tree.
