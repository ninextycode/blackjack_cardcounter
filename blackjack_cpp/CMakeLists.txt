
# CMake configuration for the blackjack_lib C++ sources
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(blackjack_cpp_lib LANGUAGES CXX)

file(GLOB CPP_SOURCES_LIB "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER CPP_SOURCES_LIB EXCLUDE REGEX "(pybind|cpp_exec)")

# Find the cpp_exec file separately
file(GLOB CPP_EXEC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp_exec/*.cpp")

add_library(blackjack SHARED ${CPP_SOURCES_LIB})

target_include_directories(blackjack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(blackjack_exec ${CPP_EXEC_SOURCES})
target_link_libraries(blackjack_exec PRIVATE blackjack)

# Build python module with pybind11
pybind11_add_module(blackjack_py ${CMAKE_CURRENT_SOURCE_DIR}/src/pybind/pybind.cpp)
target_link_libraries(blackjack_py PRIVATE blackjack)

set_target_properties(blackjack_py PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY ${PYTHON_LIBRARY_OUTPUT_DIRECTORY}
)

