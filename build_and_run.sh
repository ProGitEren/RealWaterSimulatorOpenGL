#!/usr/bin/env bash
cd /home/bora/Projects/learning/RealWaterSimulatorOpenGL/build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF
make -j$(nproc)
./RealWaterSimulator