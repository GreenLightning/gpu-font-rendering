cmake_minimum_required(VERSION 3.10)

project(gpu-font-rendering)

add_executable(main "source/main.cpp")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)
