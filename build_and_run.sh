cd /home/bora/Projects/learning/RealWaterSimulatorOpenGL/build_linux # path to working directory here
cmake .. -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF
make -j$(nproc)
./RealWaterSimulator