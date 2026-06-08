# Building RealWaterSimulator on Linux

> A real-time GPU ocean simulator (OpenGL 4.6 + C++17): a 512-resolution FFT sea inside
> a ring of photoreal coastal cliffs, with rain, interactive ripples, loadable glTF models,
> and moving vehicles (jet-ski / yacht / big-ship). See `PROJECT_STATUS.md` for the full
> architecture and developer handoff.

## Requirements

- GPU with **OpenGL 4.6** support (NVIDIA, AMD, or Intel Iris Xe+). Needs compute shaders,
  SSBOs, and image load/store — the FFT ocean is GPU-compute based.
- GCC 9+ or Clang 10+ (C++17, including `<filesystem>`)
- CMake 3.15+
- X11 display server (Wayland via XWayland also works)

---

## 1. Install system dependencies

### Ubuntu / Debian / Linux Mint

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxxf86vm-dev \
    xorg-dev
```

### Arch Linux / Manjaro

```bash
sudo pacman -S --needed \
    base-devel cmake mesa libx11 libxrandr libxinerama libxcursor libxi libxxf86vm
```

### Fedora / RHEL / CentOS Stream

```bash
sudo dnf install -y \
    gcc-c++ cmake mesa-libGL-devel libX11-devel libXrandr-devel \
    libXinerama-devel libXcursor-devel libXi-devel libXxf86vm-devel
```

> All other dependencies (GLFW, GLAD, GLM, STB, cgltf) are **vendored under `external/`** and
> built/included automatically — no system install needed.

---

## 2. Get the project

```bash
git clone <repo-url> RealWaterSimulator
cd RealWaterSimulator
```

The repository includes the 3D assets it needs to run (`assets/models/`, `assets/textures/`).

---

## 3. Build

Use a dedicated `build_linux/` directory (the tracked `build/` may contain stale Windows
artifacts; keeping them separate avoids collisions):

```bash
mkdir -p build_linux
cd build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

> Use `-DCMAKE_BUILD_TYPE=Debug` if you need debug symbols.

---

## 4. Run

**You must run the executable from inside the build directory** so the relative `../assets/`
paths resolve correctly (all shaders, textures, and models are loaded at runtime by relative
path):

```bash
# you are already in build_linux/ from step 3
./RealWaterSimulator
```

First run prints the detected OpenGL/GLSL version and the loaded models, e.g.:
```
OpenGL version: 4.6.0 NVIDIA ...
Model: loaded ../assets/models/yacht/scene.gltf -> 23 mesh(es), 0 texture(s)
```

---

## 5. Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Move camera |
| Space / Left-Shift | Move up / down |
| Mouse | Look around |
| **P** | **Toggle vehicle movement (stop / move)** |
| C | Fire an interactive wave disturbance ~40 m ahead |
| T or V | Toggle ocean wireframe |
| K / L | Wind speed -/+ 0.5 m/s (range 5–10) |
| N / M | Choppiness up / down |
| Up / Down arrows | Wave height scale +/- |
| Left / Right arrows | Time scale -/+ |
| R | Start recording frames to `frames/*.png` (600 frames) |
| Esc | Exit |

### Turn recorded frames into a video
Frames are captured at the 60 FPS fixed timestep:
```bash
ffmpeg -framerate 60 -start_number 0 -i frames/frame_%d.png \
    -c:v libx264 -pix_fmt yuv420p -crf 18 output.mp4
```

---

## Troubleshooting

### "GLSL version not supported" / black screen / FFT looks wrong
Your GPU or driver does not fully support OpenGL 4.6 compute. On NVIDIA install the
proprietary driver (`ubuntu-drivers devices` to list versions):
```bash
sudo apt-get install nvidia-driver-535   # or newer
```
On AMD ensure Mesa 21.0+ (`sudo apt-get install mesa-vulkan-drivers`).

### "cannot open shared object file: libGL.so"
```bash
sudo apt-get install libgl1-mesa-glx
```

### CMake cannot find OpenGL
Ensure `libgl1-mesa-dev` is installed, then clean-configure:
```bash
rm -rf build_linux && mkdir build_linux && cd build_linux && cmake ..
```

### GLFW compilation fails with missing X11 headers
```bash
sudo apt-get install xorg-dev
```

### `<filesystem>` link error on very old toolchains
GCC < 9 needs `-lstdc++fs`. The project targets GCC 9+ where `<filesystem>` is built in; if
you must use an older compiler, add `target_link_libraries(... stdc++fs)` in `CMakeLists.txt`.

### Slow performance / low FPS
Make sure it's a Release build and the discrete GPU is active:
```bash
glxinfo | grep "OpenGL renderer"     # must NOT say llvmpipe/softpipe
```
On NVIDIA Optimus laptops:
```bash
prime-run ./RealWaterSimulator
# or: __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./RealWaterSimulator
```

---

## Project structure (high level)

```
RealWaterSimulator/
├── assets/
│   ├── shaders/    GLSL: ocean (standard + fft_*.comp), object, terrain, rain, skybox
│   ├── textures/   skybox cubemaps + tiling terrain textures
│   └── models/     glTF models (rocks, coastal cliff, jet-ski, yacht, big-ship)
├── external/       Vendored: glad, glfw, glm, stb, cgltf
├── src/
│   ├── main.cpp    Scene setup, render loop, input, vehicles
│   ├── core/       Window (GLFW), Camera
│   ├── graphics/   Shader, Mesh, Model (glTF loader), Texture, Terrain, RockGenerator
│   ├── ocean/      GPUFFTOcean (Stockham FFT), OceanMesh, GPUDisturbance
│   └── water/      WaterSimulation, RainSystem
├── CMakeLists.txt  Cross-platform build (Windows + Linux)
├── LINUX_BUILD.md  This file
└── PROJECT_STATUS.md  Full architecture & developer handoff
```

For building on Windows and the full architecture, see **`PROJECT_STATUS.md`**.
