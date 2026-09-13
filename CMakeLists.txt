# =============================================================================
# Star Wars: Force Commander - static recompilation
#
#   1. py -3 ../tools/tools/disasm/disasm32.py game/Focom.exe -o analysis/functions.json
#   2. py -3 gen_imports.py        # -> src/runtime/imports_gen.c
#   3. py -3 run_lift.py           # -> src/recomp/gen/
#   4. cmake -B build -G Ninja && cmake --build build
#   5. build/focom game/Focom.exe --run
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(focom C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen/recomp_*.c")
if(NOT GEN_SOURCES)
  message(FATAL_ERROR "src/recomp/gen is empty -- run `py -3 run_lift.py` first")
endif()

add_executable(focom
  src/runtime/recomp_runtime.c
  src/runtime/imports_gen.c
  ${CMAKE_SOURCE_DIR}/../tools/runtime/recomp32/image_loader.c
  ${GEN_SOURCES}
)

# recomp_types.h comes from the toolbox, not a fork of it.
target_include_directories(focom PRIVATE
  src/runtime
  src/recomp/gen
  ${CMAKE_SOURCE_DIR}/../tools/runtime/recomp32
)

# RECOMP_RETADDR=0u: a benign dummy return address, so a stack imbalance during
# bring-up cannot turn 0xDEAD0000 into a multi-gigabyte size argument.
target_compile_definitions(focom PRIVATE RECOMP_RETADDR=0u)

# The target wants 0x00400000..0x00C00000 for its own image, stack and heap, so
# the host must not be linked anywhere near there. A 64-bit host maps the 32-bit
# VAs 1:1 through (uintptr_t) casts and g_mem_base stays 0.
if(MSVC)
  target_link_options(focom PRIVATE /BASE:0x140000000 /FIXED:NO /STACK:0x1000000)
  target_compile_options(focom PRIVATE /W3 /wd4101 /wd4102 /wd4244 /wd4245
                                       /wd4305 /wd4306 /wd4311 /wd4312 /wd4996)
else()
  target_link_options(focom PRIVATE -Wl,--image-base,0x140000000
                                    -Wl,--stack,16777216)
  # Generated code trips a lot of benign warnings; the signal is in the lift
  # errors run_lift.py counts, not here.
  target_compile_options(focom PRIVATE
    # NOT -mno-sse: mingw's windows.h uses __builtin_ia32_sfence.
    -w -fno-strict-aliasing -fwrapv
    -Wno-unused-label -Wno-unused-variable)
endif()

target_link_libraries(focom PRIVATE gdi32 user32)

# Optional: per-function entry ring buffer, a poor-man's backtrace.
option(FOCOM_TRACE "Record a ring buffer of lifted function entries" OFF)
if(FOCOM_TRACE)
  target_compile_definitions(focom PRIVATE RECOMP_TRACE=1)
endif()
