cmake_minimum_required(VERSION 3.5)
project(dusk VERSION 1.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DUSK_ENABLE_SANITIZERS "enable sanitizers" OFF)

add_library(
  dusk

  dusk/dusk.h
  dusk/dusk_internal.h
  dusk/dusk_map.c
  dusk/dusk_string_builder.c
  dusk/dusk_allocator.c
  dusk/dusk_compiler.c
  dusk/dusk_type.c
  dusk/dusk_parser.c
  dusk/dusk_analysis.c
  dusk/dusk_ast_to_ir.c
  dusk/dusk_ir.c
  dusk/spirv.h)
target_include_directories(dusk PUBLIC dusk)
set_property(TARGET dusk PROPERTY COMPILE_WARNING_AS_ERROR ON)

add_executable(duskc duskc/duskc.c)
target_link_libraries(duskc PRIVATE dusk)
set_property(TARGET duskc PROPERTY COMPILE_WARNING_AS_ERROR ON)

if (NOT MSVC)
  target_compile_options(dusk PRIVATE -Wall -Wextra -Wno-unused-function)
endif()

if (DUSK_ENABLE_SANITIZERS)
  if (NOT MSVC)
    target_compile_options(dusk PUBLIC -fsanitize=undefined)
    target_link_options(dusk PUBLIC -fsanitize=undefined)
  endif()
endif()
