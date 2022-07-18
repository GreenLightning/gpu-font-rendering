cmake_minimum_required(VERSION 3.15)

project(gpu-font-rendering)

find_package(OpenGL REQUIRED)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory("dependencies/glfw")

add_subdirectory("dependencies/glad")
add_subdirectory("dependencies/freetype")

add_executable(main "source/main.cpp")
target_link_libraries(main OpenGL::GL glfw glad freetype)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)
