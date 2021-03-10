cmake_minimum_required(VERSION 3.1)
project(dusk VERSION 1.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
  dusk

  dusk/dusk.h
  dusk/dusk_internal.h
  dusk/dusk_map.c
  dusk/dusk_allocator.c
  dusk/dusk_compiler.c
  dusk/dusk_parser.c
  dusk/spirv.h)
target_include_directories(dusk PUBLIC dusk)

add_executable(duskc duskc/duskc.c)
target_link_libraries(duskc PRIVATE dusk)

if (NOT MSVC)
  target_compile_options(
    dusk
    PUBLIC
    -Wall -Wextra -Wno-unused-function
    -fsanitize=undefined
  )
  target_link_options(
    dusk
    PUBLIC
    -fsanitize=undefined
  )
endif()
