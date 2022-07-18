cmake_minimum_required(VERSION 3.15)

add_library(glad "src/glad.c")
target_include_directories(glad PUBLIC "include")

set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
