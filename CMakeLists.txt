cmake_minimum_required(VERSION 3.20)
project(looping_mcp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MinGW: link the C++ runtime statically so the binaries do not depend on whichever
# libstdc++ DLL happens to be first on PATH (and the CLI ships as one .exe).
if(MINGW)
  add_link_options(-static)
endif()

add_subdirectory(core)
add_subdirectory(cli)

option(LOO_BUILD_TESTS "Build tests" ON)
if(LOO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
