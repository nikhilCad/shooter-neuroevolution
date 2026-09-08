cmake_minimum_required(VERSION 3.15)

project(raylib_cpp)

# Default to an optimized build — the evolution sweep is compute-heavy and
# an unoptimized build makes it dramatically slower. Override with
# -DCMAKE_BUILD_TYPE=Debug if you need debug symbols.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib REQUIRED)

add_executable(raylib_cpp
    main.cpp
    Player.cpp
    Enemy.cpp
    Genome.cpp
    Evolution.cpp
    PlayerAgent.cpp
    Simulation.cpp
    HistoryGraph.cpp
    GenomeVisualizer.cpp
    ParameterSweep.cpp
)

target_link_libraries(raylib_cpp PRIVATE raylib)
