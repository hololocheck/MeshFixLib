cmake_minimum_required(VERSION 3.13)
project(MeshFixCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mesh-fix-core
    src/mesh_fix.cpp
    src/diagnose.cpp
    src/stages.cpp
    src/hole_fill.cpp
    src/winding.cpp
    src/self_intersect.cpp
    src/voxel.cpp
    src/bindings.cpp
)

target_include_directories(mesh-fix-core PRIVATE src)

# Emscripten-specific settings
if(EMSCRIPTEN)
    set_target_properties(mesh-fix-core PROPERTIES
        SUFFIX ".js"
        OUTPUT_NAME "mesh-fix-core"
    )
    target_link_options(mesh-fix-core PRIVATE
        --bind
        "SHELL:-s WASM=1"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME=MeshFixCore"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s ENVIRONMENT=web,worker,node"
        "SHELL:-s NO_EXIT_RUNTIME=1"
        "SHELL:-s SINGLE_FILE=0"
        -O3
    )
endif()
