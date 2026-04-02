
# cmake_minimum_required(VERSION 3.15)
# set(CMAKE_OSX_ARCHITECTURES "arm64")
# project(MesherEngine)
# set(CMAKE_BUILD_TYPE Release)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math -mtune=native")
# set(CMAKE_CXX_STANDARD 17)

# # Search for OpenCASCADE in Homebrew
# find_package(OpenCASCADE REQUIRED)

# add_executable(cad_test cad_test.cpp voronoi_core.cpp)

# # Link your exact OpenCASCADE setup
# target_include_directories(cad_test SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
# target_link_libraries(cad_test PRIVATE ${OpenCASCADE_LIBRARIES})


cmake_minimum_required(VERSION 3.15)
set(CMAKE_OSX_ARCHITECTURES "arm64")
project(MesherEngine)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math -mtune=native")
set(CMAKE_CXX_STANDARD 17)

# Search for OpenCASCADE in Homebrew
find_package(OpenCASCADE REQUIRED)

# CHANGED: Output binary is now named 'voronoi_mesh'
add_executable(voronoi_mesh cad_test.cpp voronoi_core.cpp)

# Link your exact OpenCASCADE setup
target_include_directories(voronoi_mesh SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(voronoi_mesh PRIVATE ${OpenCASCADE_LIBRARIES})