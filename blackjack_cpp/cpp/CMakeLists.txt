
# CMake configuration for the blackjack C++ sources
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(blackjack_cpp_lib LANGUAGES CXX)

# Source files
set(BJ_SOURCES
	blackjack_round.cpp
	cards.cpp
	game_tree.cpp
	hand.cpp
	rules.cpp
	shoe.cpp
)

add_library(blackjack ${BJ_SOURCES})

target_include_directories(blackjack
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_options(blackjack PRIVATE -Wall -Wextra -Wpedantic)

option(BUILD_SHARED_LIBS "Build libraries as shared" OFF)

message(STATUS "Configured blackjack library with sources: ${BJ_SOURCES}")

add_executable(blackjack_demo main.cpp)
target_link_libraries(blackjack_demo PRIVATE blackjack)
target_include_directories(blackjack_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
