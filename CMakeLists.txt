cmake_minimum_required(VERSION 3.20)
project(DQ9Recompiled VERSION 0.0.1 LANGUAGES C CXX)

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
add_custom_command(
    OUTPUT ${DQ9_PREP_OUTPUTS}
    COMMAND "${DQ9_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_dq9.py"
            --rom "${DQ9_ROM}" --out "${DQ9_INPUT_DIR}"
    DEPENDS "${DQ9_ROM}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_dq9.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ov029_unprotect.py"
    COMMENT "Verifying YDQP and preparing main/overlay code images")
add_custom_target(dq9_prepare DEPENDS ${DQ9_PREP_OUTPUTS})

# The firmware's boot code runs before the cartridge does, but only with
# game.toml's boot = "lle": the default generated firmware boots the cartridge
# directly and needs no dump, so these banks are opt-in. The dumps they come
# from are the user's own, so they exist only when those are present, and are
# generated from whichever dump the runner will boot. Decoding takes
# milliseconds and decides which parts exist, so it happens at configure time.
option(DQ9_FIRMWARE_BANKS
    "Generate banks for a firmware dump's boot code (for boot = \"lle\")" OFF)
set(DQ9_FIRMWARE "${NDSRECOMP_ROOT}/bios/firmware.bin" CACHE FILEPATH
    "Firmware dump to generate boot banks from")
set(DQ9_BIOS7 "${NDSRECOMP_ROOT}/bios/biosnds7.rom" CACHE FILEPATH
    "ARM7 BIOS holding the key that decodes the firmware")
set(DQ9_FIRMWARE_DIR "${DQ9_INPUT_DIR}/firmware")
set(DQ9_FIRMWARE_PARTS)
set(_fw_dep)
if(DQ9_FIRMWARE_BANKS AND EXISTS "${DQ9_FIRMWARE}" AND EXISTS "${DQ9_BIOS7}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${DQ9_FIRMWARE}" "${DQ9_BIOS7}"
        "${NDSRECOMP_ROOT}/tools/fw_extract.py")
    execute_process(
        COMMAND "${DQ9_PYTHON}" -I "${NDSRECOMP_ROOT}/tools/fw_extract.py"
                "${DQ9_FIRMWARE}" "${DQ9_BIOS7}" --out "${DQ9_FIRMWARE_DIR}"
        RESULT_VARIABLE _fw_result OUTPUT_VARIABLE _fw_output
        ERROR_VARIABLE _fw_output)
    if(NOT _fw_result EQUAL 0)
        message(FATAL_ERROR "Firmware decode failed:\n${_fw_output}")
    endif()
    file(READ "${DQ9_FIRMWARE_DIR}/parts.json" _fw_json)
    string(JSON _fw_count LENGTH "${_fw_json}")
    math(EXPR _fw_last "${_fw_count} - 1")
    foreach(_fw_index RANGE 0 ${_fw_last})
        string(JSON _fw_name GET "${_fw_json}" ${_fw_index} name)
        list(APPEND DQ9_FIRMWARE_PARTS "${_fw_name}")
    endforeach()
    set(_fw_dep "${DQ9_FIRMWARE_DIR}/parts.json")
    message(STATUS "Firmware boot banks: ${DQ9_FIRMWARE_PARTS}")
else()
    if(DQ9_FIRMWARE_BANKS)
        message(FATAL_ERROR "DQ9_FIRMWARE_BANKS needs ${DQ9_FIRMWARE} and ${DQ9_BIOS7}")
    endif()
    # The scan picks firmware parts up from parts.json and the runner globs
    # every bank in the directory, so a previous opt-in's output must go.
    file(REMOVE_RECURSE "${DQ9_FIRMWARE_DIR}")
    file(GLOB _fw_stale "${DQ9_RECOMP_DIR}/dq9_fw_*")
    if(_fw_stale)
        file(REMOVE ${_fw_stale})
    endif()
    message(STATUS "Firmware boot banks: off (generated firmware, direct boot)")
endif()

# Whole-ROM static analysis (tools/static_scan_dq9.py, ~10 s, no gameplay):
# every image is scanned with `nds_recompile --static-scan` and the images
# exchange cross-image call targets until the set converges. The banks are
# built from this scan alone, starting from the ROM entry points. Outputs are
# rewritten only when their content changes.
set(DQ9_ANALYSIS_DIR "${DQ9_GENERATED_DIR}/analysis")
set(DQ9_ANALYSIS_MANIFEST "${DQ9_ANALYSIS_DIR}/manifest.json")
add_custom_command(
    OUTPUT "${DQ9_ANALYSIS_MANIFEST}"
    COMMAND "${DQ9_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/static_scan_dq9.py"
            --recompiler $<TARGET_FILE:nds_recompile>
            --inputs "${DQ9_INPUT_DIR}" --out "${DQ9_ANALYSIS_DIR}"
    DEPENDS nds_recompile ${DQ9_PREP_OUTPUTS} ${_fw_dep}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/static_scan_dq9.py"
    COMMENT "Static whole-ROM scan of YDQP (main, ARM7, overlays, firmware)")
add_custom_target(dq9_analysis DEPENDS "${DQ9_ANALYSIS_MANIFEST}")

# Banks as "<bank>:<shards>:<image>[:separate]". <image> names the analysis
# config (generated/analysis/<image>.toml) and its code image. Shard counts are
# ~2,000 scanned functions per shard (static-scan report, 2026-11-04), at
# least 2. Overlays the scan finds no code in (ov007, ov018, ov022, ov032:
# one function each, no static-init table) and DSi-only overlays at or above
# 0x02400000 get no bank. Overlays sharing a load address are safe: every
# row is gated by --validate-live-bytes.
#
# "separate" keeps every function validating on its own instead of coalescing
# neighbours, for code whose surroundings change while it runs: ov029 decrypts
# and re-encrypts one region at a time, and the firmware's last stage wipes
# memory right up to the routine doing the wiping.
set(DQ9_BANKS
    "dq9_arm9:24:arm9"
    "dq9_arm7:4:arm7"
    "dq9_arm9_ov000:6:ov000" "dq9_arm9_ov001:3:ov001" "dq9_arm9_ov002:3:ov002"
    "dq9_arm9_ov003:5:ov003" "dq9_arm9_ov004:4:ov004" "dq9_arm9_ov005:2:ov005"
    "dq9_arm9_ov006:2:ov006" "dq9_arm9_ov008:2:ov008" "dq9_arm9_ov009:2:ov009"
    "dq9_arm9_ov010:2:ov010" "dq9_arm9_ov011:2:ov011" "dq9_arm9_ov012:2:ov012"
    "dq9_arm9_ov013:2:ov013" "dq9_arm9_ov014:2:ov014" "dq9_arm9_ov015:2:ov015"
    "dq9_arm9_ov016:2:ov016" "dq9_arm9_ov017:9:ov017" "dq9_arm9_ov019:2:ov019"
    "dq9_arm9_ov020:2:ov020" "dq9_arm9_ov021:2:ov021" "dq9_arm9_ov023:4:ov023"
    "dq9_arm9_ov024:4:ov024" "dq9_arm9_ov025:3:ov025" "dq9_arm9_ov026:2:ov026"
    "dq9_arm9_ov027:2:ov027" "dq9_arm9_ov028:2:ov028"
    "dq9_arm9_ov029:2:ov029:separate"
    "dq9_arm9_ov030:2:ov030" "dq9_arm9_ov031:8:ov031" "dq9_arm9_ov033:2:ov033"
    "dq9_arm9_ov034:2:ov034")
# Named to sort after every game bank, and sorted among themselves, because
# this list has to stay in registration order.
list(SORT DQ9_FIRMWARE_PARTS)
foreach(_fw_part IN LISTS DQ9_FIRMWARE_PARTS)
    string(REGEX MATCH "[79]$" _fw_cpu "${_fw_part}")
    string(REGEX REPLACE "[79]$" "" _fw_role "${_fw_part}")
    list(APPEND DQ9_BANKS
        "dq9_fw_arm${_fw_cpu}_${_fw_role}:4:fw${_fw_part}:separate")
endforeach()
# Superblocks (--coalesce-fallthroughs): adjacent functions inside one 4 KiB
# page are emitted as one host function, removing the dispatch round trip at
# each artificial fall-through. A merge must never run across an address a
# bank registered EARLIER owns (dispatch is first-match-wins), so every bank
# gets the dispatch tables of the same-CPU banks before it in the runner's
# alphabetical registration order as --preceding-dispatch. DQ9_BANKS is kept
# in that order. A superfluous preceding row only suppresses a merge.
# Mid-instruction hooks (ndsrecomp/docs/midasm-hooks.md). A bank with a
# manifest at config/hooks/<bank>.toml is generated with it; the handlers go
# in src/hooks/ and link into the runner through NDS_TITLE_HOOK_SOURCES.
# DQ9_HOOKS=OFF generates every bank without hooks: the reference build a
# hook's effect is measured against.
option(DQ9_HOOKS "Bake config/hooks/<bank>.toml into the generated banks" ON)
set(DQ9_HOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config/hooks")
file(GLOB DQ9_HOOK_MANIFESTS CONFIGURE_DEPENDS "${DQ9_HOOK_DIR}/*.toml")
set(_dq9_bank_names)
foreach(_spec IN LISTS DQ9_BANKS)
    string(REGEX REPLACE ":.*" "" _name "${_spec}")
    list(APPEND _dq9_bank_names "${_name}")
endforeach()
foreach(_manifest IN LISTS DQ9_HOOK_MANIFESTS)
    get_filename_component(_name "${_manifest}" NAME_WE)
    if(NOT _name IN_LIST _dq9_bank_names)
        message(FATAL_ERROR
            "${_manifest} names no bank; hook manifests are "
            "config/hooks/<bank>.toml for a bank in DQ9_BANKS")
    endif()
endforeach()

set(DQ9_ARM9_SOURCES)
set(DQ9_ARM7_SOURCES)
set(DQ9_BANK_STAMPS)
set(_preceding_arm9_args)
set(_preceding_arm9_stamps)
set(_preceding_arm7_args)
set(_preceding_arm7_stamps)
foreach(_spec IN LISTS DQ9_BANKS)
    string(REPLACE ":" ";" _spec "${_spec}")
    list(GET _spec 0 _bank)
    list(GET _spec 1 _shards)
    list(GET _spec 2 _image)
    set(_merge --coalesce-fallthroughs)
    list(LENGTH _spec _spec_fields)
    if(_spec_fields GREATER 3)
        set(_merge)
    endif()
    if(_image MATCHES "^ov([0-9]+)$")
        set(_bin "${DQ9_INPUT_DIR}/overlays/arm9_overlay_${CMAKE_MATCH_1}.bin")
    elseif(_image MATCHES "^fw(.+)$")
        set(_bin "${DQ9_FIRMWARE_DIR}/${CMAKE_MATCH_1}.bin")
    else()
        set(_bin "${DQ9_INPUT_DIR}/${_image}.bin")
    endif()
    set(_sources)
    math(EXPR _last "${_shards} - 1")
    foreach(_shard RANGE 0 ${_last})
        if(_shard LESS 10)
            set(_shard "0${_shard}")
        endif()
        list(APPEND _sources "${DQ9_RECOMP_DIR}/${_bank}_${_shard}.c")
    endforeach()
    list(APPEND _sources "${DQ9_RECOMP_DIR}/${_bank}_dispatch.c")
    set(_stamp "${DQ9_RECOMP_DIR}/${_bank}.stamp")
    if(_bank MATCHES "_arm7")
        set(_cpu arm7)
    else()
        set(_cpu arm9)
    endif()
    set(_hooks)
    set(_hook_dep)
    set(_hook_manifest "${DQ9_HOOK_DIR}/${_bank}.toml")
    if(DQ9_HOOKS AND _hook_manifest IN_LIST DQ9_HOOK_MANIFESTS)
        set(_hooks --hooks "${_hook_manifest}")
        set(_hook_dep "${_hook_manifest}")
    endif()
    set(_preceding_args ${_preceding_${_cpu}_args})
    set(_preceding_stamps ${_preceding_${_cpu}_stamps})
    # gen_bank.py keys the stamp on the recompiler binary, config, candidates,
    # image and flags: an unchanged input set skips the (10+ min) regen, and a
    # changed one rewrites only shards whose bytes differ.
    add_custom_command(
        OUTPUT "${_stamp}"
        BYPRODUCTS ${_sources} "${DQ9_RECOMP_DIR}/${_bank}.h"
        COMMAND "${DQ9_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_bank.py"
                --recompiler $<TARGET_FILE:nds_recompile>
                --out "${DQ9_RECOMP_DIR}" --bank ${_bank} --stamp "${_stamp}" --
                --config "${DQ9_ANALYSIS_DIR}/${_image}.toml"
                --bin "${_bin}"
                --static-scan
                --extra-candidates "${DQ9_ANALYSIS_DIR}/${_image}.candidates.txt"
                --shards ${_shards}
                --stable-address-shards
                --max-function-bytes 512
                --validate-live-bytes
                ${_merge}
                ${_hooks}
                ${_preceding_args}
        DEPENDS nds_recompile "${DQ9_ANALYSIS_MANIFEST}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_bank.py"
                ${_hook_dep}
                ${_preceding_stamps}
        COMMENT "Recompiling YDQP bank ${_bank}")
    if(_cpu STREQUAL arm7)
        list(APPEND DQ9_ARM7_SOURCES ${_sources})
    else()
        list(APPEND DQ9_ARM9_SOURCES ${_sources})
    endif()
    list(APPEND _preceding_${_cpu}_args
         --preceding-dispatch "${DQ9_RECOMP_DIR}/${_bank}_dispatch.c")
    list(APPEND _preceding_${_cpu}_stamps "${_stamp}")
    list(APPEND DQ9_BANK_STAMPS "${_stamp}")
endforeach()

add_custom_target(dq9_generate_banks DEPENDS ${DQ9_BANK_STAMPS})
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
set_target_properties(dq9_romcheck PROPERTIES OUTPUT_NAME "DQ9Recompiled")
target_include_directories(dq9_romcheck PRIVATE "${NDSRECOMP_ROOT}/recompiler/support")

add_custom_target(dq9recompiled DEPENDS dq9_romcheck dq9_recompiled_banks)
