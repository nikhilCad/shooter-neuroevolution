cmake_minimum_required(VERSION 3.15)

project(raylib_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib REQUIRED)

add_executable(raylib_cpp main.cpp Player.cpp Enemy.cpp NeuralNetwork.cpp Evolution.cpp PlayerAgent.cpp)

target_link_libraries(raylib_cpp PRIVATE raylib)
