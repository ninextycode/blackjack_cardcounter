
# CMake configuration for the blackjack_lib C++ sources
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(blackjack_cpp_lib LANGUAGES CXX)

# Find OpenMP
find_package(OpenMP REQUIRED)

# Profile-guided optimization options
set(PGO_PROFILE_DIR "/tmp/bjprof")
option(PGO_GENERATE "Build with -fprofile-generate" OFF)
option(PGO_USE "Build with -fprofile-use" OFF)

# Profiling drops runtime for edge estimation by about 20%
if(PGO_GENERATE)
    add_compile_options(-fprofile-generate=${PGO_PROFILE_DIR})
    add_link_options(-fprofile-generate=${PGO_PROFILE_DIR})
elseif(PGO_USE)
    add_compile_options(-fprofile-use=${PGO_PROFILE_DIR} -fprofile-correction)
    add_link_options(-fprofile-use=${PGO_PROFILE_DIR})
endif()

file(GLOB CPP_SOURCES_LIB "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB CPP_SOURCES_FLOOR_CEIL "${CMAKE_CURRENT_SOURCE_DIR}/src/floor_ceil_node/*.cpp")
list(APPEND CPP_SOURCES_LIB ${CPP_SOURCES_FLOOR_CEIL})
list(FILTER CPP_SOURCES_LIB EXCLUDE REGEX "(pybind|cpp_exec)")

# Find the cpp_exec file separately
file(GLOB CPP_EXEC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp_exec/*.cpp")
file(GLOB EDGE_EXEC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/edge_exec/*.cpp")

add_library(blackjack SHARED ${CPP_SOURCES_LIB})

target_include_directories(blackjack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(blackjack PUBLIC OpenMP::OpenMP_CXX)
# add json parsing
target_link_libraries(blackjack PUBLIC nlohmann_json::nlohmann_json)

add_executable(blackjack_exec ${CPP_EXEC_SOURCES})
target_link_libraries(blackjack_exec PRIVATE blackjack)

add_executable(edge_exec ${EDGE_EXEC_SOURCES})
target_link_libraries(edge_exec PRIVATE blackjack)

# Build python module with pybind11
pybind11_add_module(blackjack_py ${CMAKE_CURRENT_SOURCE_DIR}/src/pybind/pybind.cpp)
target_link_libraries(blackjack_py PRIVATE blackjack)

set_target_properties(blackjack_py PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY ${PYTHON_LIBRARY_OUTPUT_DIRECTORY}
)

