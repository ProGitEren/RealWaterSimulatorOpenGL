# Building RealWaterSimulator on Linux

## Requirements

- GPU with OpenGL 4.6 support (NVIDIA, AMD, or Intel Iris Xe+)
- GCC 9+ or Clang 10+
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
    base-devel \
    cmake \
    mesa \
    libx11 \
    libxrandr \
    libxinerama \
    libxcursor \
    libxi \
    libxxf86vm
```

### Fedora / RHEL / CentOS Stream

```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    mesa-libGL-devel \
    libX11-devel \
    libXrandr-devel \
    libXinerama-devel \
    libXcursor-devel \
    libXi-devel \
    libXxf86vm-devel
```

---

## 2. Unzip and enter the project

```bash
unzip RealWaterSimulator.zip
cd RealWaterSimulator
```

---

## 3. Build

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

> Use `cmake .. -DCMAKE_BUILD_TYPE=Debug` if you need debug symbols.

---

## 4. Run

**You must run the executable from inside the `build/` directory** so the relative paths to `../assets/` resolve correctly:

```bash
# You are already in build/ from step 3
./RealWaterSimulator
```

If you moved away from build/:

```bash
cd build
./RealWaterSimulator
```

---

## 5. Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Move camera |
| Mouse | Look around |
| Scroll wheel | Zoom |
| K / L | Wind speed -/+ 0.5 m/s |
| N / M | Choppiness down / up |
| Up / Down arrows | Wave height scale |
| Left / Right arrows | Time scale |
| C | Fire interactive disturbance |
| V | Toggle wireframe |
| Esc | Exit |

---

## Troubleshooting

### "GLSL version not supported" or black screen
Your GPU or driver does not support OpenGL 4.6. On NVIDIA, install the proprietary driver:
```bash
# Ubuntu (check available versions first with: ubuntu-drivers devices)
sudo apt-get install nvidia-driver-535
```
On AMD, make sure Mesa 21.0+ is installed:
```bash
sudo apt-get install mesa-vulkan-drivers
```

### "cannot open shared object file: libGL.so"
```bash
sudo apt-get install libgl1-mesa-glx
```

### CMake cannot find OpenGL
Make sure `libgl1-mesa-dev` is installed. Then clean and re-run cmake:
```bash
rm -rf build && mkdir build && cd build && cmake ..
```

### GLFW compilation fails with missing X11 headers
Install the full xorg dev package:
```bash
sudo apt-get install xorg-dev
```

### Slow performance / low FPS
Make sure you are running the Release build:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```
Also verify your GPU is being used (not software rendering):
```bash
glxinfo | grep "OpenGL renderer"
```
If it says "llvmpipe" or "softpipe", your discrete GPU is not active. On laptops with NVIDIA Optimus, use:
```bash
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./RealWaterSimulator
```
Or install and use `prime-run`:
```bash
prime-run ./RealWaterSimulator
```

---

## Project structure

```
RealWaterSimulator/
├── assets/
│   ├── shaders/          GLSL shaders (loaded at runtime via ../assets/)
│   └── textures/         Skybox cubemap textures
├── external/
│   ├── glad/             OpenGL function loader (compiled as part of project)
│   ├── glfw/             GLFW windowing library (compiled as part of project)
│   ├── glm/              GLM math library (header-only)
│   └── stb/              STB image loader (header-only)
├── src/
│   ├── main.cpp
│   ├── core/             Window, Camera
│   ├── graphics/         Shader loader
│   ├── ocean/            FFT ocean, GPU disturbance, ocean mesh
│   └── water/            Rain system, water simulation
├── CMakeLists.txt        Cross-platform build (Windows + Linux)
└── LINUX_BUILD.md        This file
```
