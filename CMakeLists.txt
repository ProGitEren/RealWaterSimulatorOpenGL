cmake_minimum_required(VERSION 3.15)
project(RealWaterSimulator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/core/Window.cpp
    src/core/Camera.cpp
    src/graphics/Shader.cpp
    src/graphics/Mesh.cpp
    src/graphics/Model.cpp
    src/graphics/Texture.cpp
    src/graphics/RockGenerator.cpp
    src/ocean/GPUFFTOcean.cpp
    src/ocean/OceanMesh.cpp
    src/ocean/GPUDisturbance.cpp
    src/water/GPURain.cpp
    src/water/BoatPhysics.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
)

find_package(OpenGL REQUIRED)
add_subdirectory(external/glfw)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    external
    external/glad/include
    external/imgui
    external/imgui/backends
)

target_sources(${PROJECT_NAME} PRIVATE
    external/glad/src/glad.c
    # Dear ImGui (vendored v1.91.9) + GLFW/OpenGL3 backends
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    OpenGL::GL
)

# Linux: GLAD uses dlopen, GLFW uses pthreads — both require explicit linking
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl pthread)
    # Recommended: enable full warnings on GCC/Clang
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
