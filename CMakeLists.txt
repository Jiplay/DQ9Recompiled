cmake_minimum_required(VERSION 3.20)
project(DQ9Recomp VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NDSRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ndsrecomp" CACHE PATH
    "Path to the ndsrecomp framework checkout")
if(NOT EXISTS "${NDSRECOMP_ROOT}/recompiler/CMakeLists.txt")
    message(FATAL_ERROR "ndsrecomp not found at ${NDSRECOMP_ROOT}")
endif()
include("${NDSRECOMP_ROOT}/cmake/NdsCompilerCache.cmake")
include("${NDSRECOMP_ROOT}/cmake/NdsJobPools.cmake")
add_subdirectory("${NDSRECOMP_ROOT}/recompiler"
                 "${CMAKE_BINARY_DIR}/ndsrecomp-recompiler")

set(DQ9_ROM "${CMAKE_CURRENT_SOURCE_DIR}/dq9.nds" CACHE FILEPATH
    "Path to the verified Europe YDQP ROM")
set(DQ9_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
set(DQ9_INPUT_DIR "${DQ9_GENERATED_DIR}/inputs")
set(DQ9_RECOMP_DIR "${DQ9_GENERATED_DIR}/recomp")
# Optional Tier-3 coverage seeds (same JSON schema as the MPH project).
set(DQ9_COVERAGE_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/coverage/entry-points.json")

if(WIN32)
    set(_py "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe")
else()
    set(_py "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python3")
endif()
if(EXISTS "${_py}")
    set(DQ9_PYTHON "${_py}" CACHE FILEPATH "Python with ndspy installed")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(DQ9_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH "Python with ndspy installed")
endif()

set(DQ9_PREP_OUTPUTS
    "${DQ9_INPUT_DIR}/arm9.bin" "${DQ9_INPUT_DIR}/arm7.bin"
    "${DQ9_INPUT_DIR}/arm9.toml" "${DQ9_INPUT_DIR}/arm7.toml"
    "${DQ9_INPUT_DIR}/overlays.json")
set(_cov_arg)
if(EXISTS "${DQ9_COVERAGE_SEEDS}")
    set(_cov_arg --coverage "${DQ9_COVERAGE_SEEDS}")
endif()
add_custom_command(
    OUTPUT ${DQ9_PREP_OUTPUTS}
    COMMAND "${DQ9_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_dq9.py"
            --rom "${DQ9_ROM}" --out "${DQ9_INPUT_DIR}" ${_cov_arg}
    DEPENDS "${DQ9_ROM}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_dq9.py"
    COMMENT "Verifying YDQP and preparing main/overlay code images")
add_custom_target(dq9_prepare DEPENDS ${DQ9_PREP_OUTPUTS})

# Main closures. Shard counts are initial guesses; tune after first build.
set(DQ9_ARM9_SOURCES)
foreach(shard RANGE 0 15)
    string(LENGTH "${shard}" _l)
    if(_l EQUAL 1)
        set(shard "0${shard}")
    endif()
    list(APPEND DQ9_ARM9_SOURCES "${DQ9_RECOMP_DIR}/dq9_arm9_${shard}.c")
endforeach()
list(APPEND DQ9_ARM9_SOURCES "${DQ9_RECOMP_DIR}/dq9_arm9_dispatch.c")
set(DQ9_ARM7_SOURCES)
foreach(shard RANGE 0 3)
    list(APPEND DQ9_ARM7_SOURCES "${DQ9_RECOMP_DIR}/dq9_arm7_0${shard}.c")
endforeach()
list(APPEND DQ9_ARM7_SOURCES "${DQ9_RECOMP_DIR}/dq9_arm7_dispatch.c")

foreach(_cpu arm9;arm7)
    string(TOUPPER ${_cpu} _CPU)
    if(_cpu STREQUAL arm9)
        set(_shards 16)
    else()
        set(_shards 4)
    endif()
    add_custom_command(
        OUTPUT "${DQ9_RECOMP_DIR}/dq9_${_cpu}.stamp"
        BYPRODUCTS ${DQ9_${_CPU}_SOURCES} "${DQ9_RECOMP_DIR}/dq9_${_cpu}.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DQ9_RECOMP_DIR}"
        COMMAND $<TARGET_FILE:nds_recompile>
                --config "${DQ9_INPUT_DIR}/${_cpu}.toml"
                --bin "${DQ9_INPUT_DIR}/${_cpu}.bin"
                --out "${DQ9_RECOMP_DIR}"
                --bank dq9_${_cpu}
                --shards ${_shards}
                --stable-address-shards
                --max-function-bytes 512
                --validate-live-bytes
        COMMAND ${CMAKE_COMMAND} -E touch "${DQ9_RECOMP_DIR}/dq9_${_cpu}.stamp"
        DEPENDS nds_recompile ${DQ9_PREP_OUTPUTS}
        COMMENT "Recompiling the YDQP ${_CPU} main closure")
endforeach()

add_custom_target(dq9_generate_banks
    DEPENDS "${DQ9_RECOMP_DIR}/dq9_arm9.stamp" "${DQ9_RECOMP_DIR}/dq9_arm7.stamp")
add_library(dq9_recompiled_banks STATIC ${DQ9_ARM9_SOURCES} ${DQ9_ARM7_SOURCES})
add_dependencies(dq9_recompiled_banks dq9_generate_banks)
ndsrecomp_limit_generated_compiles(dq9_recompiled_banks)
ndsrecomp_limit_link(dq9_recompiled_banks)
set_source_files_properties(${DQ9_ARM9_SOURCES} PROPERTIES COMPILE_DEFINITIONS "NDS_STATIC_CPU=0")
set_source_files_properties(${DQ9_ARM7_SOURCES} PROPERTIES COMPILE_DEFINITIONS "NDS_STATIC_CPU=1")
target_include_directories(dq9_recompiled_banks PRIVATE
    "${DQ9_RECOMP_DIR}" "${NDSRECOMP_ROOT}/recompiler/armv4t"
    "${NDSRECOMP_ROOT}/external/arm-recomp-core/common")
if(NOT MSVC)
    target_compile_options(dq9_recompiled_banks PRIVATE -w -O2 -g0)
endif()

add_executable(dq9_romcheck src/main.cpp "${NDSRECOMP_ROOT}/recompiler/support/sha1.cpp")
set_target_properties(dq9_romcheck PROPERTIES OUTPUT_NAME "DQ9Recomp")
target_include_directories(dq9_romcheck PRIVATE "${NDSRECOMP_ROOT}/recompiler/support")

add_custom_target(dq9recomp DEPENDS dq9_romcheck dq9_recompiled_banks)
