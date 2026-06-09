# Quarter 3 — Rendering Engine, Camera & Scene Objects

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

**This document covers Q3.**

---

## 🧭 In one minute (the high-level picture)

Think of the whole project as a film studio shooting an ocean scene. Q2 builds the water itself, Q4
animates the boats and rain. **Q3 is the camera crew, the lighting rig, and the prop department** —
the boring-but-essential gear that everything else plugs into.

Concretely, this quarter is a small, reusable **rendering toolkit**:

- **Window** opens the actual on-screen window and turns on the GPU. It's the equivalent of
  switching on the studio and pointing a power cable at the camera. Without it there's no OpenGL
  context, so literally nothing else can draw.
- **Camera** is the operator's tripod and viewfinder. It tracks where you're standing and which way
  you're looking, and hands the rest of the engine a "view matrix" — the maths that says "render the
  world as seen from *here*."
- **Shader** is a thin wrapper that loads the little GPU programs (`.vert`/`.frag`/`.comp` text
  files), compiles them, reports errors, and lets C++ push values (matrices, colors, numbers) into
  them. Every other quarter uses this same class — it's the shared adapter between CPU and GPU.
- **Mesh / Model / Texture** are the prop department. **Texture** loads image files onto the GPU.
  **Mesh** is one chunk of triangles plus the images that skin it. **Model** loads a whole glTF
  3D file (a rock, a yacht, a ship, a jet-ski), splits it into meshes, and remembers where to place
  it in the world.
- The shaders **`object.vert/frag`** light those solid props with the sun and the sky.
  **`skybox.vert/frag`** paint the painted backdrop (the sky) infinitely far away.
  **`debug_wireframe.frag`** is a flat-white shader used to inspect geometry as a wireframe.

Why does it exist? Because the ocean simulation (Q2) and the interactions (Q4) need *something* to
run inside, *somewhere* to be seen from, and *a way* to load and draw the solid objects that share
the scene. Q3 is that connective tissue. Remove it and Q2/Q4 have no window, no view, no way to
compile their own shaders, and no boats or rocks on the water.

---

## 🔌 How this quarter connects to the others

Q3 is the most "shared" quarter — its classes are used by all the others. The hand-offs:

### Out of Q3 → into Q1 (Application Host)
- `Window` is constructed once in `main.cpp` (`Window window(...)`, `main.cpp:129`). Q1 pulls the
  raw `GLFWwindow*` via `getGLFWWindow()` for input polling, and the framebuffer dimensions via
  `getWidth()/getHeight()` to build the perspective projection (`main.cpp:794`).
- `Camera` is constructed in `main.cpp:145`. Q1's input handling calls
  `camera.ProcessKeyboard(...)` (direction codes `0–5`) and `camera.ProcessMouseMovement(...)`, then
  each frame reads `camera.GetViewMatrix()` and `camera.Position` to feed the `view` matrix and the
  `viewPos` uniform into every shader.
- The `Model` objects (`rock`, `coastalCliff`, vehicle models) are created and positioned in Q1, and
  drawn with `Model::draw(objectShader)`.

### Into Q3 ← from Q1 (the shader and the cubemap)
- Q1 owns the **skybox cubemap** (`cubemapTexture`) and binds it to **texture unit 0**
  (`GL_TEXTURE0` + `GL_TEXTURE_CUBE_MAP`, `main.cpp:860–861`) before each `Model::draw`. Both
  `object.frag` and `skybox.frag` read `uniform samplerCube skybox` from that unit (set with
  `objectShader.setInt("skybox", 0)`, `main.cpp:154`).
- Q1 sets the per-frame uniforms `projection`, `view`, `viewPos`, and the rock fallback `baseColor`
  on `objectShader` before drawing solid objects (`main.cpp:855–859`).
- Q1 supplies the **texture-unit contract**: Q3's `Model::draw` binds material maps to units **4–7**
  precisely because units 0–3 are reserved for the ocean's skybox/displacement/normal/disturbance
  textures (see `Model.cpp:33–34`). Breaking that split would collide with Q2/Q4 bindings.

### Shared with Q2 and Q4 (the Shader abstraction)
- The `Shader` class is the **single shader abstraction used by every quarter**. Q2's water shaders
  (`standard.vert/frag`), the FFT compute passes, and Q4's `rain_*` / `disturbance_*` shaders are all
  loaded through `Shader`'s two constructors (vertex+fragment, and compute-only).
- `debug_wireframe.frag` (Q3) is **paired with Q2's `standard.vert`** at construction
  (`Shader debugWireframeShader("../assets/shaders/standard.vert", ".../debug_wireframe.frag")`,
  `main.cpp:148`): it reuses the water vertex shader's vertex layout but discards shading, painting
  the displaced ocean mesh flat white for inspection.
- Q4's `RockGenerator` produces CPU geometry that is handed to Q3's **`Model(vertices, indices)`**
  constructor; Q4's `BoatPhysics` produces a `glm::quat` orientation that is fed to Q3's
  `Model::setOrientation`, and `Model::localCenter()` is consumed by Q4's wake alignment.

---

## 🗂️ Files in this quarter

| File | Lines (approx) | One-line role |
|---|---|---|
| `src/core/Window.h` | 38 | Declares the non-copyable/non-movable GL window owner. |
| `src/core/Window.cpp` | 65 | GLFW init, 4.6 core context request, GLAD load, framebuffer resize callback. |
| `src/core/Camera.h` | 30 | Free-fly camera: Euler angles, basis vectors, public state. |
| `src/core/Camera.cpp` | 50 | Yaw/pitch → basis, `lookAt` view matrix, WASD + mouse handling. |
| `src/graphics/Shader.h` | 32 | GLSL program wrapper interface (vert+frag and compute ctors, `setX` helpers). |
| `src/graphics/Shader.cpp` | 122 | Load/compile/link with info logs; uniform setters via `glm::value_ptr`. |
| `src/graphics/Mesh.h` | 51 | `Vertex` + `Material` structs; RAII GL-buffer-owning mesh. |
| `src/graphics/Mesh.cpp` | 70 | VAO/VBO/EBO setup, move semantics, indexed draw. |
| `src/graphics/Model.h` | 60 | glTF model: transform state, AABB, model-matrix builder, draw. |
| `src/graphics/Model.cpp` | 212 | cgltf node-tree walk, baked transforms, material/texture loading, draw. |
| `src/graphics/Texture.h` | 12 | Declares `loadTexture2D(path, srgb)`. |
| `src/graphics/Texture.cpp` | 36 | stb_image load, channel→format selection, mipmaps, sRGB. |
| `assets/shaders/object.vert` | 22 | World-space transform + inverse-transpose normal for solid objects. |
| `assets/shaders/object.frag` | 82 | Sun diffuse + skybox ambient + spec; cotangent-frame normal mapping; sRGB encode. |
| `assets/shaders/skybox.vert` | 15 | Cube positions as texcoords; `.xyww` far-plane depth trick. |
| `assets/shaders/skybox.frag` | 11 | Samples the environment cubemap. |
| `assets/shaders/debug_wireframe.frag` | 6 | Outputs flat white for wireframe inspection. |

---

## 🔬 Deep dive

### `src/core/Window.h` / `src/core/Window.cpp`

**Purpose.** Own the OS window and the OpenGL context — the single object whose lifetime brackets all
GL work. It initializes GLFW, requests a compute-capable **OpenGL 4.6 Core** context, creates the
window, loads function pointers via GLAD, and tears everything down on destruction.

**Class members** (`Window.h:30–32`):
- `GLFWwindow* m_window` — the owned raw handle.
- `int m_width`, `int m_height` — cached dimensions passed at construction.

**Ownership / RAII.** Because the destructor both destroys the window and terminates GLFW, the type
is deliberately **non-copyable and non-movable** — all four special members are `= delete`
(`Window.h:15–18`). The comment is explicit: "Owns a raw `GLFWwindow*` … so the window is
non-copyable and non-movable; it is constructed exactly once." Allowing a copy/move would risk a
double `glfwDestroyWindow`/`glfwTerminate`.

**Constructor** (`Window.cpp:4–41`):
1. `glfwInit()`; on failure it logs and returns early, leaving `m_window == nullptr`
   (`main.cpp:130` checks `getGLFWWindow()` and aborts if null).
2. Window hints request the context version: `GLFW_CONTEXT_VERSION_MAJOR=4`,
   `GLFW_CONTEXT_VERSION_MINOR=6`, `GLFW_OPENGL_PROFILE=GLFW_OPENGL_CORE_PROFILE`
   (`Window.cpp:14–16`). The 4.6 request is what makes the **compute shaders** (Q2 FFT, Q4
   rain/disturbance) and SSBOs legal. On `__APPLE__` it also sets `GLFW_OPENGL_FORWARD_COMPAT`
   (`Window.cpp:18–20`) — though Apple caps GL at 4.1, so compute would not actually run there.
3. `glfwCreateWindow(...)` then `glfwMakeContextCurrent(m_window)` (`Window.cpp:23,30`). The context
   must be current *before* GLAD loads.
4. `glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback)` (`Window.cpp:31`).
5. `gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)` (`Window.cpp:34`) — loads every GL entry
   point. **Gotcha:** this must come after `glfwMakeContextCurrent`; otherwise there is no context to
   load functions from.
6. Prints `GL_VERSION` and `GL_SHADING_LANGUAGE_VERSION` for diagnostics (`Window.cpp:39–40`).

**Destructor** (`Window.cpp:43–48`): destroys the window if non-null, then `glfwTerminate()`.

**Methods.**
- `shouldClose()` → `glfwWindowShouldClose` (loop condition in Q1).
- `swapBuffers()` → `glfwSwapBuffers` (present the back buffer).
- `pollEvents()` → `glfwPollEvents` (pump input/OS events).
- `getGLFWWindow()`, `getWidth()`, `getHeight()` are inline getters (`Window.h:25–27`).

**Static callback** `framebuffer_size_callback` (`Window.cpp:62–65`): on resize, calls
`glViewport(0, 0, width, height)`. **Gotcha:** it updates the GL viewport but does **not** write back
into `m_width`/`m_height`. Q1 builds its projection aspect ratio from `getWidth()/getHeight()`
(`main.cpp:794`), so those return the *original* construction size; the aspect ratio will not track a
runtime resize. Acceptable here because the app runs at a fixed window size (`kWindowWidth`/
`kWindowHeight` in Q1).

---

### `src/core/Camera.h` / `src/core/Camera.cpp`

**Purpose.** A classic Euler-angle free-fly camera (LearnOpenGL lineage). It stores position and
orientation, derives an orthonormal basis (`Front`/`Right`/`Up`), and produces a `lookAt` view
matrix. Pure CPU maths — it touches no GL.

**Public state** (`Camera.h:9–18`): `Position`, `Front`, `Up`, `Right`, `WorldUp` (all `glm::vec3`),
plus the scalars `Yaw`, `Pitch`, `MovementSpeed`, `MouseSensitivity`. State is public so Q1 can read
`camera.Position` directly for the `viewPos` uniform.

**Constructor** (`Camera.cpp:5–12`). Defaults: `Front = (0, -0.5, -1)` (immediately overwritten),
`MovementSpeed = 70.0f` (world units/second), `MouseSensitivity = 0.1f`. `WorldUp = (0,1,0)`,
`Yaw = -90.0f`, `Pitch = -20.0f` ("Look slightly down at the water"). It ends by calling
`updateCameraVectors()`, which recomputes a correct `Front` from the angles. (In Q1 the camera is
actually placed at `(0, 40, 120)` — `main.cpp:145` — via the `position` argument.)

**`updateCameraVectors()`** (`Camera.cpp:42–50`) — the heart of the orientation. Converts the two
Euler angles into a forward vector, then builds an orthonormal basis:

```
Front.x = cos(yaw) · cos(pitch)
Front.y = sin(pitch)
Front.z = sin(yaw) · cos(pitch)        (then normalized)

Right = normalize( Front × WorldUp )
Up    = normalize( Right × Front )
```

Angles are converted with `glm::radians`. Yaw is measured about the world Y axis; `yaw = -90°` points
down −Z. Recomputing `Right` from `WorldUp` (rather than carrying a roll) keeps the horizon level.
`#include <cmath>` was added explicitly (`Camera.cpp:3`) for `cos`/`sin` — previously only
transitively visible.

**`GetViewMatrix()`** (`Camera.cpp:14–16`): `return glm::lookAt(Position, Position + Front, Up)`.
Builds the world→view matrix. **Cross-ref:** the skybox draw strips the translation from this matrix
with `glm::mat4(glm::mat3(view))` (`main.cpp:894`) so the sky stays centered on the camera.

**`ProcessKeyboard(int direction, float deltaTime)`** (`Camera.cpp:18–26`). `velocity = MovementSpeed
· deltaTime`. The integer `direction` is a code, dispatched by Q1's key handling (`main.cpp:476–481`):

| code | meaning | effect | Q1 key |
|---|---|---|---|
| 0 | FORWARD | `Position += Front · v` | W |
| 1 | BACKWARD | `Position -= Front · v` | S |
| 2 | LEFT | `Position -= Right · v` | A |
| 3 | RIGHT | `Position += Right · v` | D |
| 4 | UP | `Position += Up · v` | Space |
| 5 | DOWN | `Position -= Up · v` | Left-Shift |

Movement is along the camera's own basis (true free-fly, not ground-locked).

**`ProcessMouseMovement(float xoffset, float yoffset)`** (`Camera.cpp:28–40`). Scales both offsets by
`MouseSensitivity`, adds to `Yaw`/`Pitch`, then **clamps pitch to `[-89°, +89°]`** (`Camera.cpp:36–37`)
to prevent gimbal flip at the poles, and re-derives the basis via `updateCameraVectors()`.
**Gotcha:** Q1 passes `yoffset = lastY - ypos` (`main.cpp:541`) — Y is inverted there because screen Y
grows downward — so the camera does not need an internal invert.

---

### `src/graphics/Shader.h` / `src/graphics/Shader.cpp`

**Purpose.** The project-wide GLSL program wrapper. Loads shader source from disk, compiles, links,
reports compile/link logs, and exposes typed uniform setters. **Used by every quarter.**

**Member.** `unsigned int ID` (`Shader.h:11`) — the GL program object name; `0` means construction
failed.

**Two public constructors:**
- `Shader(const char* vertexPath, const char* fragmentPath)` (`Shader.cpp:66–77`) — the standard
  raster pipeline: compile a `GL_VERTEX_SHADER` and a `GL_FRAGMENT_SHADER`, then link both.
- `explicit Shader(const char* computePath)` (`Shader.cpp:79–88`) — compute-only: compile a
  `GL_COMPUTE_SHADER` and link it alone. `explicit` prevents an accidental implicit conversion from a
  single string. **This is the path Q2's FFT passes and Q4's rain/disturbance passes use.**

Both wrap the work in `try/catch`; on a thrown `std::runtime_error` (a missing file) they log
`what()` and set `ID = 0` (`Shader.cpp:73–76, 84–87`).

**Private static helpers:**
- `loadFile(const char* path)` (`Shader.cpp:10–19`) — opens the file, throws
  `std::runtime_error("Failed to open shader file: …")` if it can't, otherwise slurps the whole file
  through a `std::stringstream` and returns the string.
- `compileShader(GLenum type, const std::string& source, const char* label)` (`Shader.cpp:21–38`) —
  `glCreateShader` → `glShaderSource` → `glCompileShader`; queries `GL_COMPILE_STATUS`; on failure it
  reads `GL_INFO_LOG_LENGTH`, sizes a string with `std::max(logLength, 1)` (never zero-length), fills
  it via `glGetShaderInfoLog`, and prints `ERROR::SHADER::COMPILATION_FAILED::<label>`. **Gotcha:** it
  still **returns the (failed) shader handle** rather than aborting — a failed compile produces a
  console error and an unusable program, not a crash.
- `linkProgram(const std::vector<unsigned int>& shaders, const char* label)` (`Shader.cpp:40–64`) —
  `glCreateProgram`, attaches each shader, `glLinkProgram`, checks `GL_LINK_STATUS` and logs on
  failure (same `std::max(logLength,1)` trick), then **detaches and deletes every shader**
  (`Shader.cpp:58–61`) — the linked program retains the compiled code, so the individual shader
  objects are no longer needed. Returns the program name.

**Destructor** (`Shader.cpp:90–94`): `glDeleteProgram(ID)` only if `ID != 0`.

**`use()`** (`Shader.cpp:96–98`): `glUseProgram(ID)`.

**Uniform setters** (`Shader.cpp:100–122`). Each calls `glGetUniformLocation(ID, name.c_str())` then
the matching `glUniform*`:
- `setMat4` → `glUniformMatrix4fv(..., 1, GL_FALSE, glm::value_ptr(mat))` — `GL_FALSE` = no transpose
  (GLM is already column-major, matching GLSL), `glm::value_ptr` exposes the contiguous 16 floats.
- `setVec2` / `setVec3` → `glUniform2fv` / `glUniform3fv` (note `setVec3` uses `&value[0]` directly;
  `setVec2` uses `glm::value_ptr` — equivalent).
- `setFloat` → `glUniform1f`, `setInt` → `glUniform1i`, `setUInt` → `glUniform1ui`.

**Gotcha:** these setters look up the uniform location *every call* (no caching) and silently do
nothing if the name is wrong or optimized out (`glGetUniformLocation` returns `-1`). Convenient, not
fast — fine at this scene's draw counts.

---

### `src/graphics/Mesh.h` / `src/graphics/Mesh.cpp`

**Purpose.** One drawable chunk of indexed geometry that **owns its GL buffer objects (RAII)**, plus
the material that skins it.

**`struct Vertex`** (`Mesh.h:10–14`): interleaved `glm::vec3 position`, `glm::vec3 normal`,
`glm::vec2 uv`. This exact layout is what `object.vert` reads at locations 0/1/2.

**`struct Material`** (`Mesh.h:19–25`): four GL texture ids — `albedo`, `normalMap`, `roughness`, `ao`
(each `0` when absent) — plus `glm::vec3 baseColor = (0.8, 0.8, 0.8)`, the flat base-color factor used
when there is no albedo texture (e.g. the untextured yacht/ship parts).

**Class members** (`Mesh.h:43–46`): `m_vao`, `m_vbo`, `m_ebo` (GL names, init `0`),
`GLsizei m_indexCount`, plus the public `Material material` (`Mesh.h:32`).

**Construction** (`Mesh.cpp:5–33`): records `m_indexCount = indices.size()`, then:
- `glGenVertexArrays` + two `glGenBuffers`; binds the VAO.
- Uploads vertices to the VBO (`GL_ARRAY_BUFFER`, `GL_STATIC_DRAW`) and indices to the EBO
  (`GL_ELEMENT_ARRAY_BUFFER`, `GL_STATIC_DRAW`).
- Sets three `glVertexAttribPointer` + `glEnableVertexAttribArray` calls using
  `offsetof(Vertex, …)` and `sizeof(Vertex)` as the stride: **location 0 = position (3 floats),
  1 = normal (3 floats), 2 = uv (2 floats)** (`Mesh.cpp:25–30`). The EBO binding is captured inside
  the VAO, so `draw()` doesn't rebind it.
- Unbinds the VAO.

**RAII / move semantics.** Non-copyable (`= delete`, `Mesh.h:35–36`); **movable** so a Mesh can live
in a `std::vector` (Model holds `std::vector<Mesh>`). The move ctor (`Mesh.cpp:47–52`) and move
assignment (`Mesh.cpp:54–64`) copy the handles + material, then **null the source's handles** so the
moved-from object's destructor is a no-op. `operator=` first calls `release()` on the existing handles
to avoid leaking. `release()` (`Mesh.cpp:39–45`) deletes EBO/VBO/VAO (if non-zero) and resets all to
`0`. Destructor just calls `release()`.

**`draw()`** (`Mesh.cpp:66–70`): bind VAO → `glDrawElements(GL_TRIANGLES, m_indexCount,
GL_UNSIGNED_INT, nullptr)` → unbind. **Note:** `Mesh::draw()` binds *no* material textures itself —
`Model::draw` is responsible for binding the material's maps to the texture units *before* calling
`mesh.draw()`.

---

### `src/graphics/Model.h` / `src/graphics/Model.cpp`

**Purpose.** The reusable solid-object loader. Loads a glTF/GLB file into one or more `Mesh`es via
**cgltf**, carries a transform (position / orientation quaternion / scale), bakes a local-space AABB,
builds the model matrix, and draws every mesh with material uniforms bound. Also constructs directly
from in-memory geometry (used by Q4's `RockGenerator`).

**Members** (`Model.h:49–55`):
- `std::vector<Mesh> m_meshes`.
- `glm::vec3 m_position = (0,0,0)`.
- `glm::vec3 m_scale = (1,1,1)`.
- `glm::quat m_orientation = (w=1,x=0,y=0,z=0)` (identity).
- `glm::vec3 m_aabbMin = (+1e9)`, `m_aabbMax = (-1e9)` — inverted sentinels so the first vertex
  initializes them via `min`/`max`.

**Constructors.**
- `explicit Model(const std::string& path)` (`Model.cpp:12–14`) → `loadFromFile(path)`.
- `Model(const std::vector<Vertex>&, const std::vector<unsigned int>&)` (`Model.cpp:16–18`) →
  `m_meshes.emplace_back(...)` — the procedural path used by Q4's rock generator. **Note:** this path
  does **not** populate the AABB, so `localCenter()` returns `(0,0,0)` for procedural models.

**`loaded()`** (`Model.h:25`): `!m_meshes.empty()`.

**Transform setters** (`Model.h:28–35`):
- `setPosition`, `setScale(float)` / `setScale(vec3)` (non-uniform stretch — used by cliffs).
- `setRotationY(float radians)` routes through the quaternion via
  `m_orientation = glm::angleAxis(radians, (0,1,0))`. The comment explains why: keeping yaw as the
  *same* quaternion lets buoyancy pitch/roll (Q4) and rolling-rock spin compose on top, instead of a
  separate yaw float fighting the quaternion.
- `setOrientation(const glm::quat&)` — set the full orientation directly (Q4 buoyancy + bank).

**`localCenter()`** (`Model.h:39–41`): returns `(0,0,0)` if the AABB is still inverted
(`m_aabbMax.x < m_aabbMin.x`), else `(m_aabbMin + m_aabbMax) · 0.5`. Used by Q4 to align the boat
wake to the *visible* hull center when a model's mesh sits off its glTF origin.

**`modelMatrix()`** (`Model.cpp:20–26`): builds **T · R · S**:
```
m = translate(I, m_position)        // T
m = m · mat4_cast(m_orientation)    // R  (quaternion → mat4)
m = scale(m, m_scale)               // S
```
Translate first, then rotate, then scale — so the object rotates about its own origin and is placed
at `m_position`.

**`draw(const Shader& shader)`** (`Model.cpp:28–55`):
1. `shader.setMat4("model", modelMatrix())`.
2. For each mesh, bind its material maps to **texture units 4–7** (`Model.cpp:35–42`):
   `GL_TEXTURE4`=albedo, `GL_TEXTURE5`=normalMap, `GL_TEXTURE6`=roughness, `GL_TEXTURE7`=ao
   (each `glBindTexture(GL_TEXTURE_2D, id)`; binding `0` is the safe "none" state). The comment is the
   contract: units 0–3 are reserved for the ocean's skybox/displacement/normal/disturbance, and
   `object.frag` reads the **skybox cubemap from unit 0**.
3. Set the matching sampler uniforms (`albedoMap=4, normalMap=5, roughnessMap=6, aoMap=7`), the three
   `int` flags `hasAlbedo` / `hasNormalMap` / `hasAO` (`1` iff the id is non-zero), and `baseColor`.
4. `mesh.draw()`.

**glTF loading — `loadFromFile`** (`Model.cpp:98–212`). `#define CGLTF_IMPLEMENTATION` (`Model.cpp:4`)
compiles cgltf into this TU.
1. `cgltf_parse_file` then `cgltf_load_buffers` (`Model.cpp:102, 108`); on failure, log and bail —
   leaving the model empty (so `loaded()` is false).
2. Compute `dir` from the path (`directoryOf`, `Model.cpp:71–74`) so relative texture URIs resolve;
   create a `std::map<const cgltf_image*, unsigned int> texCache` to upload each shared texture once.

**Anonymous-namespace helpers** (`Model.cpp:59–96`):
- `readFloats(accessor, out, components)` — reads a vec2/vec3 float accessor element-by-element via
  `cgltf_accessor_read_float` into a flat buffer.
- `directoryOf(path)` — the directory portion (up to and including the last `/` or `\`).
- `loadGltfTexture(tex, dir, srgb, cache)` (`Model.cpp:79–95`) — returns `0` if no texture/image;
  returns the cached id if already loaded; otherwise, for an **external URI**, builds `dir + uri` and
  calls `loadTexture2D(full, srgb)` (Q3 Texture). Embedded textures are skipped (Poly Haven uses
  external `.jpg`). Caches by image pointer.

**`processPrimitive(prim, world)` lambda** (`Model.cpp:120–184`):
- Skips non-triangle primitives.
- Finds the position / normal / first-texcoord accessors (`Model.cpp:126–131`); bails with no position.
- Computes the **normal matrix** `normalMat = mat3(transpose(inverse(world)))` (`Model.cpp:134`) so
  baked transforms (including non-uniform scale) don't skew normals.
- Reads positions/normals/uvs, then per vertex (`Model.cpp:143–153`): **bakes the node world matrix
  into the position** (`world * vec4(p,1)`), expands the AABB with `glm::min`/`glm::max`, transforms
  and normalizes the normal (or defaults to `(0,1,0)` if absent), copies the uv (or `(0,0)`).
- Reads indices via `cgltf_accessor_read_index` if present, else generates a trivial `0..n-1` index
  list (`Model.cpp:156–163`).
- `m_meshes.emplace_back(vertices, indices)` then fills the new mesh's `Material` from the glTF
  material (`Model.cpp:168–183`): from `pbr_metallic_roughness` it loads `base_color_texture` as
  **sRGB** albedo, `metallic_roughness_texture` as **linear** roughness, and copies `base_color_factor`
  into `baseColor`; the `normal_texture` and `occlusion_texture` are loaded as **linear** maps.

**Node-tree walk — `visit(node, parent)` lambda** (`Model.cpp:187–198`): reads the node's local
transform via `cgltf_node_transform_local` (handles either explicit matrix or TRS), composes
`world = parent · local`, processes each of the node's mesh primitives with that `world`, then recurses
into children. Roots come from `data->scene->nodes` if a scene exists, otherwise all parentless nodes
(`Model.cpp:201–207`). This is why multi-part models (yacht, ship) assemble correctly — every part's
node transform is baked into its vertices once at load, so a single `model` uniform places the whole
object. Finally logs the mesh/texture counts and `cgltf_free(data)`.

---

### `src/graphics/Texture.h` / `src/graphics/Texture.cpp`

**Purpose.** A single free function, `loadTexture2D(const std::string& path, bool srgb)`, that loads
an image file with stb_image and uploads it as a 2D GL texture; returns the GL id or `0` on failure.

**`loadTexture2D`** (`Texture.cpp:6–36`):
1. `stbi_set_flip_vertically_on_load(false)` — glTF UVs already have the origin at top-left, so **no**
   vertical flip (`Texture.cpp:8`).
2. `stbi_load(..., &channels, 0)` — `0` keeps the file's native channel count. On null, logs and
   returns `0`.
3. **Channel → format selection** (`Texture.cpp:17–20`):
   - default (3 channels): `srcFormat = GL_RGB`, `intFormat = srgb ? GL_SRGB8 : GL_RGB8`.
   - 1 channel: `GL_RED` / `GL_R8`.
   - 4 channels: `GL_RGBA` / (`GL_SRGB8_ALPHA8` if `srgb` else `GL_RGBA8`).
   The `srgb` flag selects an **sRGB internal format** so the GPU linearizes the texture on sample —
   correct for color/albedo maps; data maps (normal/roughness/AO) must be linear, so they pass
   `srgb=false`. **This is the contract that keeps the lighting maths in linear space** (the frag
   shader later re-encodes to sRGB).
4. `glGenTextures` → bind → `glTexImage2D(level 0, intFormat, w, h, 0, srcFormat, GL_UNSIGNED_BYTE,
   data)` → `glGenerateMipmap` (`Texture.cpp:22–27`).
5. Parameters (`Texture.cpp:29–32`): wrap S/T `GL_REPEAT`; min filter
   `GL_LINEAR_MIPMAP_LINEAR` (trilinear); mag filter `GL_LINEAR`.
6. `stbi_image_free(data)`; return the id.

**Gotcha:** the implementation note `// implementation is provided in main.cpp` (`Texture.cpp:3`) means
`STB_IMAGE_IMPLEMENTATION` is defined in Q1's `main.cpp`, not here — this TU only `#include`s the
header. Defining it twice would cause duplicate-symbol link errors.

---

### `assets/shaders/object.vert`

**Purpose.** Vertex stage for solid objects. `#version 460 core`. Inputs `aPos`/`aNormal`/`aUV` at
locations 0/1/2 (matching the `Vertex` struct). Uniforms `model`, `view`, `projection`.

Logic (`object.vert:14–22`):
```
worldPos = model * vec4(aPos, 1.0)
FragPos  = worldPos.xyz                                    // world-space position to frag
Normal   = normalize( transpose(inverse(mat3(model))) * aNormal )  // inverse-transpose normal matrix
UV       = aUV
gl_Position = projection * view * worldPos
```
The **inverse-transpose normal matrix** is recomputed here (in addition to the bake in `Model.cpp`)
so that any *runtime* non-uniform scale (the stretched cliff "mountains") does not skew the normals —
a plain `mat3(model)` would shear them. Outputs `FragPos` (world), `Normal` (world), `UV`.

---

### `assets/shaders/object.frag`

**Purpose.** Fragment shading for solid objects: sun diffuse + skybox environment ambient + dampened
specular, with optional normal mapping, glTF roughness/AO, and a final linear→sRGB encode.
`#version 460 core`.

**Uniforms** (`object.frag:8–19`): `viewPos`, `samplerCube skybox` (unit 0), `baseColor` fallback;
the four `sampler2D` maps `albedoMap`/`normalMap`/`roughnessMap`/`aoMap` (units 4–7) and the `int`
flags `hasAlbedo`/`hasNormalMap`/`hasAO`.

**Fixed sun direction** (`object.frag:24`):
```
const vec3 kSunDir = vec3(0.43193, 0.86386, 0.25932);
```
This is a **compile-time constant**, NOT driven by the water shader's `uSunDir` uniform. The comment
flags the consequence: it matches the water sun only at the *default* sun azimuth/elevation sliders;
moving the sun re-lights the water but **not** these objects.

**`applyNormalMap(N, worldPos, uv)`** (`object.frag:29–44`) — **Mikkelsen's cotangent-frame trick**:
derive a tangent basis from screen-space derivatives instead of precomputed tangents (glTF tangents
aren't read here). Steps:
```
dp1 = dFdx(worldPos);  dp2 = dFdy(worldPos)
duv1 = dFdx(uv);       duv2 = dFdy(uv)
dp2perp = cross(dp2, N);  dp1perp = cross(N, dp1)
T = dp2perp·duv1.x + dp1perp·duv2.x
B = dp2perp·duv1.y + dp1perp·duv2.y
invmax = inversesqrt( max(dot(T,T), dot(B,B)) )      // normalize the longer of T,B
TBN = mat3(T·invmax, B·invmax, N)
n = texture(normalMap, uv).xyz * 2 - 1                // [0,1] → [-1,1]
return normalize(TBN * n)
```
This reconstructs a per-pixel tangent frame, decodes the tangent-space normal, and rotates it into
world space.

**`main`** (`object.frag:46–82`):
- **Albedo:** `texture(albedoMap, UV).rgb` if `hasAlbedo`, else `baseColor`.
- **Normal:** `N = normalize(Normal)`, perturbed by `applyNormalMap` when `hasNormalMap`.
- **View dir:** `V = normalize(viewPos - FragPos)`.
- **Roughness:** uses the **green channel** of `roughnessMap` (glTF packs roughness in G), guarded by
  `textureSize(roughnessMap,0).x > 1` (a 1×1 dummy means "no map") else `0.85`.
- **AO:** `aoMap.r` if `hasAO`, else `1.0`.
- **Diffuse:** `diff = max(dot(N, kSunDir), 0.0)` (Lambert against the fixed sun).
- **Environment ambient:** `R = reflect(-V, N)`; `skyAmbient = texture(skybox, R).rgb` — samples the
  cubemap in the reflection direction so objects pick up the sky color.
- **Specular** (Blinn-Phong, roughness-damped): `H = normalize(kSunDir + V)`,
  `specPow = mix(64, 8, rough)` (smoother → tighter highlight),
  `spec = pow(max(dot(N,H),0), specPow) · (1-rough) · 0.5`.
- **Compose** (`object.frag:76–79`):
  ```
  ambient = 0.85 · albedo · skyAmbient
  color   = (ambient + albedo · diff · 1.2) · ao + vec3(spec)
  color   = pow(color, vec3(1/2.2))     // linear → sRGB encode
  ```
  Lighting happens in **linear** space (albedo arrived linear because the texture is an sRGB internal
  format), and the final gamma encode (`1/2.2`) converts back to display sRGB — without it the image
  renders too dark/muddy. Output `FragColor = vec4(color, 1.0)`.

---

### `assets/shaders/skybox.vert`

**Purpose.** Draw the environment cubemap on a unit cube centered on the camera, pushed to the far
plane. `#version 330 core`. Input `aPos` (location 0), uniforms `view`, `projection`, output
`TexCoords`.

Logic (`skybox.vert:9–15`): `TexCoords = aPos` — the cube's local positions double as the cubemap
direction vectors. Then:
```
pos = projection * view * vec4(aPos, 1.0)
gl_Position = pos.xyww;
```
The **`.xyww` swizzle** sets the clip-space `z = w`, so after the perspective divide `z/w = 1.0` — the
maximum depth (the far plane). This pairs with Q1's `glDepthFunc(GL_LEQUAL)` (`main.cpp:892`) so the
skybox passes the depth test only where no nearer geometry was drawn, i.e. it fills the background.
**Cross-ref:** Q1 strips the translation from the view matrix (`glm::mat4(glm::mat3(camera.GetViewMatrix())`,
`main.cpp:894`) so the cube stays centered on the camera — the sky never gets closer.

---

### `assets/shaders/skybox.frag`

**Purpose.** Trivial: sample the environment cubemap. `#version 330 core`. Uniform
`samplerCube skybox` (unit 0). `FragColor = texture(skybox, TexCoords)` (`skybox.frag:9–11`).
`TexCoords` is the interpolated cube direction from the vertex stage.

---

### `assets/shaders/debug_wireframe.frag`

**Purpose.** A flat-white fragment shader for wireframe inspection. `#version 330 core`.
`FragColor = vec4(1.0, 1.0, 1.0, 1.0)` (`debug_wireframe.frag:4–6`). **Cross-ref:** Q1 pairs this with
the **water vertex shader** `standard.vert` (`main.cpp:148`) — not `object.vert` — and toggles
`GL_LINE` polygon mode (`main.cpp:810` region) to render the displaced ocean mesh as unshaded
wireframe for debugging.

---

## ⚠️ Gotchas & invariants

- **Texture-unit map is a cross-quarter contract.** Units **0–3** belong to the ocean
  (skybox cubemap on **0**, displacement/normal/disturbance on 1–3); `Model::draw` binds object
  material maps to **4–7** (`Model.cpp:35–42`). `object.frag`/`skybox.frag` read `skybox` from unit
  0. Re-numbering any of these in one place silently corrupts another quarter's sampling.
- **`object.frag`'s `kSunDir` is hard-coded** (`object.frag:24`) and decoupled from the water's
  `uSunDir`. Changing the sun in the UI re-lights the water but not solid objects; to keep them
  consistent you must edit the constant by hand.
- **The skybox depth trick is two-sided.** `.xyww` (`skybox.vert:14`) only works *with* the
  `glDepthFunc(GL_LEQUAL)` switch in Q1 and the translation-stripped view matrix. Forgetting either
  (it must be restored to `GL_LESS` afterward, `main.cpp:901`) breaks background/foreground ordering.
- **sRGB pairing must match the map type.** Albedo loads with `srgb=true`; normal/roughness/AO with
  `srgb=false` (`Model.cpp:173–182`). Mismatch desaturates colors or corrupts the normal/roughness
  data. The frag shader's final `pow(color, 1/2.2)` assumes albedo arrived *linear* — both halves of
  the gamma pipeline must stay in sync.
- **`STB_IMAGE_IMPLEMENTATION` lives in `main.cpp`, not `Texture.cpp`.** `Texture.cpp` only includes
  the header (`Texture.cpp:3`). Defining the implementation here too would be a duplicate-symbol error.
- **`Window` is single-instance and non-movable.** Its destructor calls `glfwTerminate()` globally;
  copying/moving it (all `= delete`) would double-terminate GLFW.
- **`Mesh` is move-only.** It owns VAO/VBO/EBO; copying would double-free GL objects. Move nulls the
  source handles. `Model` relies on this to hold `std::vector<Mesh>`.
- **No runtime resize of the aspect ratio.** `framebuffer_size_callback` updates `glViewport` but not
  `m_width`/`m_height` (`Window.cpp:62–65`); Q1's projection aspect stays at the launch size.
- **The procedural `Model(vertices, indices)` constructor skips the AABB.** `localCenter()` returns
  `(0,0,0)` for procedural rocks — only `loadFromFile` bakes the bounds.
- **`Camera` movement is along its own basis, frame-rate-independent** (`MovementSpeed · deltaTime`),
  but the camera holds no velocity/inertia — it is purely positional. Pitch is clamped to ±89°.
- **Shader uniform setters fail silently.** A misspelled or unused uniform name yields location `-1`
  and a no-op (`Shader.cpp:100–122`); there is no warning. Likewise a failed compile/link only logs to
  stderr and leaves a broken program (`Shader.cpp:34, 55`).

---

## 🧠 Mental-model recap

- **Q3 = the studio rig:** window + GPU context (`Window`), tripod/viewfinder (`Camera`), CPU↔GPU
  adapter (`Shader`), and the prop department (`Texture` → `Mesh` → `Model`) plus the object/skybox
  shaders.
- **`Window` brackets all GL work** — it requests a 4.6 core context (so compute shaders are legal)
  and owns the one `GLFWwindow*`; it is single-instance and non-movable.
- **`Camera` turns two Euler angles into an orthonormal basis** (`updateCameraVectors`) and hands out a
  `lookAt` view matrix; `ProcessKeyboard` uses integer direction codes 0–5.
- **`Shader` is the one shader class everyone uses** — vertex+fragment for raster, compute-only for
  Q2/Q4 GPGPU passes; it loads/compiles/links with info logs and exposes `setX` uniform helpers.
- **`Mesh` owns GL buffers (RAII, move-only)** with a fixed `Vertex` layout (pos/normal/uv at 0/1/2);
  `Model` holds many meshes and bakes glTF node transforms into vertices at load.
- **`Model::draw` sets `model` and binds material maps to units 4–7**; `modelMatrix = T·R·S` with the
  orientation as a quaternion so Q4 buoyancy/spin can compose onto yaw.
- **The lighting pipeline is linear-in, sRGB-out:** sRGB albedo upload → light in linear → `pow(1/2.2)`
  encode at the end of `object.frag`.
- **Normal mapping needs no tangents** — `object.frag` reconstructs the TBN frame from screen-space
  derivatives (Mikkelsen cotangent trick).
- **The skybox is drawn last, at the far plane** via the `.xyww` depth trick + `GL_LEQUAL`, with the
  view matrix's translation removed so it stays camera-centered.
- **Texture units are a shared budget:** ocean owns 0–3 (skybox on 0), objects own 4–7 — don't collide.

---

## 📖 Glossary

- **OpenGL context** — the per-window bundle of GPU state GLFW creates and GLAD loads function pointers
  for; all GL calls require a current context.
- **Core profile** — the modern, deprecation-free subset of OpenGL (no fixed-function pipeline);
  requested here at version 4.6.
- **GLFW** — cross-platform library for windows, contexts, and input.
- **GLAD** — a loader that resolves OpenGL function pointers at runtime.
- **GLM** — header-only C++ math library mirroring GLSL types (`vec3`, `mat4`, `quat`).
- **VAO (Vertex Array Object)** — GL object recording vertex-attribute layout and the bound element
  buffer; bind it to set up a draw.
- **VBO (Vertex Buffer Object)** — GPU buffer holding vertex data (positions/normals/uvs).
- **EBO/IBO (Element/Index Buffer Object)** — GPU buffer of indices for `glDrawElements`.
- **RAII** — "Resource Acquisition Is Initialization"; tie a GPU resource's lifetime to a C++ object's
  destructor (used by `Mesh`/`Window`).
- **Move semantics** — transferring ownership of resources between objects without copying; lets the
  move-only `Mesh` live in a `std::vector`.
- **Texture unit** — a numbered slot (`GL_TEXTURE0…`) a sampler uniform reads from; bind a texture to a
  unit, set the sampler `int` to that number.
- **Mipmaps** — precomputed downscaled texture levels; trilinear filtering
  (`GL_LINEAR_MIPMAP_LINEAR`) blends between them to avoid shimmer.
- **sRGB / linear color** — display textures are stored gamma-encoded (sRGB); lighting math must run in
  linear space, so albedo is uploaded as an sRGB internal format (GPU linearizes on read) and the
  result is re-encoded with `pow(1/2.2)` before output.
- **Internal format** — how GL stores a texture on the GPU (`GL_SRGB8`, `GL_RGB8`, `GL_R8`, …),
  distinct from the source data format.
- **Uniform** — a shader global set from the CPU per draw (matrices, colors, sampler bindings).
- **Vertex / fragment / compute shader** — GPU program stages: vertex transforms each vertex,
  fragment shades each pixel, compute is general-purpose GPGPU (used by Q2/Q4).
- **Model matrix (T·R·S)** — translate × rotate × scale; places an object in world space.
- **View matrix** — world→camera transform; here `glm::lookAt(Position, Position+Front, Up)`.
- **Projection matrix** — camera→clip transform; the perspective matrix built in Q1.
- **Normal matrix** — `transpose(inverse(mat3(model)))`; transforms normals correctly under
  non-uniform scale.
- **Quaternion** — a 4-component rotation representation (`w,x,y,z`) that composes without gimbal lock;
  `Model`'s orientation is a `glm::quat`.
- **Euler angles (yaw/pitch)** — rotation about world axes; the camera stores yaw (about Y) and pitch
  (up/down), clamped to ±89°.
- **AABB** — Axis-Aligned Bounding Box; the min/max corner pair baked at model load, used for
  `localCenter()`.
- **glTF / GLB** — the standard 3D asset format (`.gltf` text + external buffers/textures, or `.glb`
  binary); parsed here by cgltf.
- **cgltf** — a small single-header glTF parser.
- **Accessor** — glTF's typed view into a buffer (positions, normals, uvs, indices).
- **Node tree** — glTF's scene hierarchy; each node has a local TRS/matrix that composes down the tree
  into a world transform.
- **PBR (metallic-roughness)** — the glTF material model; here only albedo, normal, roughness (G
  channel), and occlusion are consumed.
- **Cubemap (samplerCube)** — a six-faced texture sampled by a 3D direction vector; used for the sky
  and as object environment ambient.
- **TBN / tangent frame** — the tangent/bitangent/normal basis that rotates a tangent-space normal map
  into world space.
- **Cotangent-frame / Mikkelsen trick** — reconstructing the tangent frame from screen-space
  derivatives (`dFdx`/`dFdy`) so precomputed tangents aren't needed.
- **`dFdx` / `dFdy`** — GLSL screen-space partial derivatives of a value across a 2×2 pixel quad.
- **Blinn-Phong specular** — highlight model using the half-vector `H = normalize(L + V)` and
  `pow(N·H, shininess)`.
- **Lambert (diffuse)** — `max(N·L, 0)` shading from a directional light.
- **Depth test / `GL_LEQUAL` / `.xyww`** — the per-pixel depth comparison; the skybox sets clip
  `z=w` (depth 1.0) and uses `GL_LEQUAL` so it fills only the background.
- **Info log** — the driver's compile/link diagnostic string fetched with `glGetShaderInfoLog` /
  `glGetProgramInfoLog`.
- **`glm::value_ptr`** — returns a pointer to a GLM type's contiguous floats for `glUniform*` calls.
