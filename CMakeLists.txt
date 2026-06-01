cmake_minimum_required(VERSION 3.20)
project(casc_daw_sdk
    VERSION 0.1.0
    DESCRIPTION "Single-header DAW integration SDK for the CASC plugin format"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Locate the CASC runtime (libcasc). By default we expect a sibling checkout
# at ../CASC; override with -DCASC_DIR=/path/to/CASC.
# ---------------------------------------------------------------------------
if(NOT DEFINED CASC_DIR)
    set(CASC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../CASC")
endif()
get_filename_component(CASC_DIR "${CASC_DIR}" ABSOLUTE)

if(NOT EXISTS "${CASC_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "CASC runtime not found at '${CASC_DIR}'.\n"
        "Clone the CASC repo as a sibling directory, or pass -DCASC_DIR=/path/to/CASC.")
endif()
message(STATUS "casc-daw-sdk: using CASC runtime at ${CASC_DIR}")

# Build only the library from CASC (skip its bridge/tools/tests/examples).
set(CASC_BUILD_BRIDGE OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${CASC_DIR}" "${CMAKE_BINARY_DIR}/casc-build" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The SDK itself is header-only (single header). Expose it as an INTERFACE lib.
# ---------------------------------------------------------------------------
add_library(casc_daw_sdk INTERFACE)
target_include_directories(casc_daw_sdk INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CASC_DIR}/include"
)
# Consumers link the runtime; the SDK orchestrates it.
target_link_libraries(casc_daw_sdk INTERFACE casc)

# ---------------------------------------------------------------------------
# Self-test: compiles the single header in implementation mode and exercises
# discovery + (headless) voice lifecycle. Proves the header builds clean.
# ---------------------------------------------------------------------------
option(CASC_SDK_BUILD_TESTS "Build the SDK self-test" ON)
if(CASC_SDK_BUILD_TESTS)
    enable_testing()
    add_executable(sdk_selftest tests/sdk_selftest.c)
    target_link_libraries(sdk_selftest PRIVATE casc_daw_sdk)
    add_test(NAME sdk_selftest COMMAND sdk_selftest)
endif()

# ---------------------------------------------------------------------------
# REAPER extension: reaper_casc (shared module). One source, all OSes.
# ---------------------------------------------------------------------------
option(CASC_SDK_BUILD_REAPER "Build the reaper_casc REAPER extension" ON)
if(CASC_SDK_BUILD_REAPER)
    add_library(reaper_casc MODULE reaper/reaper_casc.c)
    target_link_libraries(reaper_casc PRIVATE casc_daw_sdk)
    target_include_directories(reaper_casc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/reaper")
    set_target_properties(reaper_casc PROPERTIES PREFIX "")  # no "lib" prefix
    # REAPER expects reaper_<name>.<so|dylib|dll>
    if(APPLE)
        set_target_properties(reaper_casc PROPERTIES SUFFIX ".dylib")
    elseif(WIN32)
        set_target_properties(reaper_casc PROPERTIES SUFFIX ".dll")
    else()
        set_target_properties(reaper_casc PROPERTIES SUFFIX ".so")
    endif()
endif()
