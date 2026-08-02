cmake_minimum_required(VERSION 3.16)
project(SurfaceMesher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# -march=native is deliberately avoided: the container that builds the image is
# not the machine that runs it, and an illegal-instruction crash on a cloud
# worker is far more expensive than the few percent it would buy.
set(SM_ARCH_FLAGS "" CACHE STRING "Extra architecture flags for the engine")
add_compile_options(-O3 -fno-math-errno ${SM_ARCH_FLAGS})

# Exact predicates depend on IEEE semantics and on integer overflow behaviour
# staying predictable, so fast-math and its friends stay off.
add_compile_options(-fno-fast-math)

find_package(OpenMP)
if(NOT OpenMP_CXX_FOUND AND APPLE)
  # Apple clang ships without the OpenMP runtime; Homebrew's libomp fills in.
  find_path(SM_OMP_INCLUDE omp.h HINTS /opt/homebrew/opt/libomp/include /usr/local/opt/libomp/include)
  find_library(SM_OMP_LIB omp HINTS /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib)
  if(SM_OMP_INCLUDE AND SM_OMP_LIB)
    add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
    set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
      INTERFACE_COMPILE_OPTIONS "-Xpreprocessor;-fopenmp"
      INTERFACE_INCLUDE_DIRECTORIES "${SM_OMP_INCLUDE}"
      INTERFACE_LINK_LIBRARIES "${SM_OMP_LIB}")
    set(OpenMP_CXX_FOUND TRUE)
    message(STATUS "OpenMP: Homebrew libomp at ${SM_OMP_LIB}")
  endif()
endif()

add_library(sm_core STATIC
  src/triangulator.cpp
  src/refine.cpp
  src/optimize.cpp
  src/mesher.cpp)

target_include_directories(sm_core PUBLIC include PRIVATE src)

if(OpenMP_CXX_FOUND)
  target_link_libraries(sm_core PUBLIC OpenMP::OpenMP_CXX)
  message(STATUS "OpenMP enabled")
else()
  message(STATUS "OpenMP not available; engine falls back to serial loops")
endif()

add_executable(sm_tests tests/test_core.cpp)
target_link_libraries(sm_tests PRIVATE sm_core)

add_executable(sm_bench bench/bench_core.cpp)
target_link_libraries(sm_bench PRIVATE sm_core)

enable_testing()
add_test(NAME core COMMAND sm_tests)

# The CAD front end is the only piece that needs OpenCASCADE. Keeping it
# optional means the core library, its tests and the benchmark still build on
# a machine with nothing but a compiler.
find_package(OpenCASCADE QUIET)
if(OpenCASCADE_FOUND)
  # Link only the toolkits the engine uses. OpenCASCADE_LIBRARIES pulls in the
  # whole distribution including the DRAW/Tcl test harness, one of which
  # installs an exit handler that discards main()'s return value - and the API
  # decides whether a job succeeded from that exit code. Filtering the list is
  # not enough because an unfiltered toolkit drags the harness back in through
  # its own dependencies. TARGET probing survives the 7.8 renames.
  set(SM_OCCT_CANDIDATES
      TKernel TKMath
      TKG2d TKG3d TKGeomBase TKGeomAlgo
      TKBRep TKTopAlgo TKPrim TKBO TKOffset
      TKShHealing TKMesh TKXSBase
      # Data exchange: 7.8+ names first, pre-7.8 equivalents after.
      TKDESTEP TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209
      TKDEIGES TKIGES)

  set(SM_OCCT_LIBS "")
  foreach(occt_lib ${SM_OCCT_CANDIDATES})
    if(TARGET ${occt_lib})
      list(APPEND SM_OCCT_LIBS ${occt_lib})
    endif()
  endforeach()
  message(STATUS "OpenCASCADE toolkits: ${SM_OCCT_LIBS}")

  add_executable(voronoi_mesh cad/main.cpp cad/occt_pipeline.cpp)
  target_include_directories(voronoi_mesh SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(voronoi_mesh PRIVATE sm_core ${SM_OCCT_LIBS})
  if(OpenMP_CXX_FOUND)
    target_link_libraries(voronoi_mesh PRIVATE OpenMP::OpenMP_CXX)
  endif()
  message(STATUS "OpenCASCADE found; building voronoi_mesh")
else()
  message(STATUS "OpenCASCADE not found; skipping the CAD front end")
endif()
