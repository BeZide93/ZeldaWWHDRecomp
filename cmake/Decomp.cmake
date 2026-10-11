# Decomp build (WWHD_DECOMP=ON, docs/decomp-build.md): the game functions of the decompilation (the
# `decomp` submodule) are compiled and linked into the game, and run in place of the recompiled code.
#
# wwhd_decomp_configure(): run at configure time, before the game code library is set up.
#   tools/decomp_build/gen.py writes the function list, the runtime glue (hooks.cpp) and the unit list
#   (units.cmake) into ${CMAKE_BINARY_DIR}/decomp, and GEN_DIR is checked: it must be recompiled with
#   that list (recomp.py --decomp-hooks), which it records as GEN_DIR/decomp_hooks.txt.
# wwhd_decomp_link(target): every game unit becomes one object file: its sources compiled with hidden
#   visibility and merged with `ld -r`, which turns hidden symbols into local ones. Units then never see
#   each other's classes, inline helpers or gabi state, as in the decompilation's verification harness,
#   where each unit is its own program. These objects, the runtime part (runtime/src/decomp) and
#   hooks.cpp go into the target.
set(WWHD_DECOMP_DIR "${CMAKE_SOURCE_DIR}/decomp" CACHE PATH "decomp build: the decompilation (git submodule decomp)")
set(WWHD_DECOMP_OUT "${CMAKE_BINARY_DIR}/decomp")

function(wwhd_decomp_configure)
  if(NOT APPLE)
    # the unit isolation needs ld64's `ld -r` (hidden -> local); other platforms need their own step
    message(FATAL_ERROR "WWHD_DECOMP: the decomp build is available on macOS only so far")
  endif()
  if(NOT EXISTS "${WWHD_DECOMP_DIR}/tools/verify/include/gabi.h")
    message(FATAL_ERROR "WWHD_DECOMP: no decompilation in ${WWHD_DECOMP_DIR}; run: git submodule update --init decomp")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/gen.py"
                          "${WWHD_DECOMP_DIR}" "${WWHD_DECOMP_OUT}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "WWHD_DECOMP: tools/decomp_build/gen.py failed:\n${out}\n${err}")
  endif()
  message(STATUS "${out}")
  include("${WWHD_DECOMP_OUT}/units.cmake")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WWHD_DECOMP_GEN_DEPENDS})
  set(list "${WWHD_DECOMP_OUT}/decomp_hooks.txt")
  string(CONCAT fix "Translate the game code with the decomp build's function list:\n"
         "  python3 tools/recomp/recomp.py game/code/cking.rpx ${GEN_DIR} --decomp-hooks ${list}\n"
         "(keep the normal build's GEN_DIR as it is; e.g. -DGEN_DIR=build/gen-decomp for this build)")
  if(NOT EXISTS "${GEN_DIR}/decomp_hooks.txt")
    message(FATAL_ERROR "WWHD_DECOMP: ${GEN_DIR} is not decomp build code.\n${fix}")
  endif()
  file(SHA256 "${GEN_DIR}/decomp_hooks.txt" have)
  file(SHA256 "${list}" want)
  if(NOT have STREQUAL want)
    message(FATAL_ERROR "WWHD_DECOMP: ${GEN_DIR} was made with another decomp function list.\n${fix}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GEN_DIR}/decomp_hooks.txt")
  set(WWHD_DECOMP_UNIT_COUNT ${WWHD_DECOMP_UNIT_COUNT} PARENT_SCOPE)
  foreach(i RANGE 1 ${WWHD_DECOMP_UNIT_COUNT})
    math(EXPR k "${i} - 1")
    set(WWHD_DECOMP_UNIT_${k}_NAME "${WWHD_DECOMP_UNIT_${k}_NAME}" PARENT_SCOPE)
    set(WWHD_DECOMP_UNIT_${k}_SOURCES "${WWHD_DECOMP_UNIT_${k}_SOURCES}" PARENT_SCOPE)
  endforeach()
endfunction()

function(wwhd_decomp_link target)
  set(inc "${CMAKE_SOURCE_DIR}/runtime/include" "${WWHD_DECOMP_DIR}/tools/verify/include"
          "${WWHD_DECOMP_DIR}/tools/verify/src" "${WWHD_DECOMP_DIR}/wwhd_src/include")
  # as the decompilation's harness compiles them (exact floating point: no contraction, no fast math);
  # these options come after the configuration's, so they win
  set(opts -O2 -g0 -w -fvisibility=hidden -fvisibility-inlines-hidden -ffp-contract=off -fno-fast-math
           -fno-strict-aliasing -include "${CMAKE_SOURCE_DIR}/tools/decomp_build/frames_vis.h")
  set(objs)
  file(MAKE_DIRECTORY "${WWHD_DECOMP_OUT}/units")
  foreach(i RANGE 1 ${WWHD_DECOMP_UNIT_COUNT})
    math(EXPR k "${i} - 1")
    set(lib wwhd_decomp_unit_${k})
    add_library(${lib} OBJECT ${WWHD_DECOMP_UNIT_${k}_SOURCES} "${WWHD_DECOMP_OUT}/glue/${k}.cpp")
    target_include_directories(${lib} PRIVATE ${inc})
    target_compile_options(${lib} PRIVATE ${opts})
    set_target_properties(${lib} PROPERTIES CXX_EXTENSIONS OFF EXCLUDE_FROM_ALL ON)
    set(o "${WWHD_DECOMP_OUT}/units/${WWHD_DECOMP_UNIT_${k}_NAME}.o")
    add_custom_command(OUTPUT "${o}"
      COMMAND "${CMAKE_LINKER}" -r -o "${o}" "$<TARGET_OBJECTS:${lib}>"
      DEPENDS ${lib} "$<TARGET_OBJECTS:${lib}>"
      COMMAND_EXPAND_LISTS VERBATIM)
    list(APPEND objs "${o}")
  endforeach()
  set_source_files_properties(${objs} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${WWHD_DECOMP_OUT}/hooks.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${WWHD_DECOMP_DIR}/tools/verify/include")
  target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/runtime/src/decomp/decomp.cpp"
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/frames.cpp" "${WWHD_DECOMP_OUT}/hooks.cpp" ${objs})
  target_compile_definitions(${target} PRIVATE WWHD_DECOMP=1)
endfunction()
