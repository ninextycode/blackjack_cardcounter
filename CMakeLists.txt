cmake_minimum_required(VERSION 3.10)

project(blackjack_cpp_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PYTHON_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python_lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Add optimization flags for all build types
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -march=native -flto -ffast-math -funroll-all-loops -fmodulo-sched -fstrict-aliasing -DNDEBUG")
add_subdirectory(extern/pybind11)
add_subdirectory(extern/json)
add_subdirectory(blackjack_cpp)
