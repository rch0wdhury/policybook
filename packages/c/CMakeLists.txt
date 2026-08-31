# libpolicybook — the C implementation of the Policybook registry.
#
# Built for people who run these policies in hot paths: embedded firmware,
# kernels, database engines, proxies (concept.md §12.2). No dependency beyond
# the C standard library, predictable memory, no hidden allocation.
#
#   cmake -S packages/c -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build && ctest --test-dir build --output-on-failure
#
# Options:
#   PB_SANITIZE = address | undefined | both   (Clang/GCC only)
#   PB_BUILD_TESTS = ON | OFF

cmake_minimum_required(VERSION 3.16)

project(policybook
    VERSION 0.1.0
    DESCRIPTION "Runnable decision policies for systems and AI infrastructure"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(PB_BUILD_TESTS "Build the test programs" ON)
option(PB_FUZZ "Build the libFuzzer harnesses (Clang only)" OFF)
set(PB_SANITIZE "" CACHE STRING "Sanitizers to enable: address, undefined, or both")

# Fuzzing without AddressSanitizer would miss the out-of-bounds accesses that
# are most of what a fuzzer is for, so it is implied.
if(PB_FUZZ AND NOT PB_SANITIZE)
    set(PB_SANITIZE "address")
endif()

# --- the library -------------------------------------------------------------

# Policy sources, assembled from policies/**/policy.c by scripts/assemble-c.ts.
set(PB_POLICY_SOURCES "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/policies.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/src/policies.cmake)
endif()

add_library(policybook STATIC
    src/rng.c
    src/allocator.c
    src/zipf.c
    src/ds/ilist.c
    src/ds/heap.c
    src/ds/map.c
    src/ds/ring.c
    src/cache/traces.c
    src/rate_limiter/traces.c
    src/retry/traces.c
    src/kv_cache/traces.c
    ${PB_POLICY_SOURCES})

target_include_directories(policybook PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# The trace generators use sqrt. Policy code does not link libm.
if(NOT MSVC)
    target_link_libraries(policybook PUBLIC m)
endif()

# --- warnings and floating point ---------------------------------------------
#
# -ffp-contract=off matters for correctness, not pedantry: with contraction on,
# a compiler may fuse a multiply and an add into one FMA with a single rounding,
# and a policy's decision would then differ from the TypeScript and Python
# implementations on the same inputs (concept.md §9).

if(MSVC)
    set(PB_WARNINGS /W4 /WX)
    set(PB_FP_FLAGS /fp:precise)
else()
    set(PB_WARNINGS -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion)
    set(PB_FP_FLAGS -ffp-contract=off)
endif()

target_compile_options(policybook PRIVATE ${PB_WARNINGS})
target_compile_options(policybook PUBLIC ${PB_FP_FLAGS})

# --- sanitizers --------------------------------------------------------------

if(PB_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "PB_SANITIZE is only supported with GCC and Clang")
    endif()

    if(PB_SANITIZE STREQUAL "address")
        set(PB_SANITIZE_FLAGS -fsanitize=address)
    elseif(PB_SANITIZE STREQUAL "undefined")
        set(PB_SANITIZE_FLAGS -fsanitize=undefined)
    elseif(PB_SANITIZE STREQUAL "both")
        set(PB_SANITIZE_FLAGS -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "PB_SANITIZE must be address, undefined, or both")
    endif()

    # -fno-sanitize-recover makes undefined behaviour abort rather than print
    # and carry on, so a sanitizer finding actually fails the test run.
    list(APPEND PB_SANITIZE_FLAGS -fno-omit-frame-pointer -fno-sanitize-recover=all -g)
    target_compile_options(policybook PUBLIC ${PB_SANITIZE_FLAGS})
    target_link_options(policybook PUBLIC ${PB_SANITIZE_FLAGS})
    message(STATUS "policybook: sanitizers enabled (${PB_SANITIZE})")
endif()

# --- tests -------------------------------------------------------------------

if(PB_BUILD_TESTS)
    include(CTest)
    enable_testing()

    # Add one test executable, wired with the library and warnings.
    function(pb_add_test name)
        add_executable(${name} ${ARGN})
        target_link_libraries(${name} PRIVATE policybook)
        target_compile_options(${name} PRIVATE ${PB_WARNINGS})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    pb_add_test(test_ds tests/test_ds.c)
    pb_add_test(cache_decision_parity tests/cache_decision_parity.c)
    pb_add_test(test_cache tests/test_cache.c)
    pb_add_test(test_kvcache tests/test_kvcache.c)
    pb_add_test(test_ratelimiter tests/test_ratelimiter.c)
    pb_add_test(test_retry tests/test_retry.c)

    # A measuring tool rather than a test: it prints the memory each
    # rate-limiter policy holds at the reference budget, for the domain
    # README's table. Built with the tests so it cannot rot.
    add_executable(measure_ratelimiter_memory tests/measure_ratelimiter_memory.c)
    target_link_libraries(measure_ratelimiter_memory PRIVATE policybook)
    target_compile_options(measure_ratelimiter_memory PRIVATE ${PB_WARNINGS})
    pb_add_test(test_rng tests/gen/test_rng.c)
    pb_add_test(test_trace_parity tests/gen/test_trace_parity.c)
    pb_add_test(test_ratelimiter_trace_parity tests/gen/test_ratelimiter_trace_parity.c)
    pb_add_test(test_retry_trace_parity tests/gen/test_retry_trace_parity.c)
    pb_add_test(test_kvcache_trace_parity tests/gen/test_kvcache_trace_parity.c)

    # The fuzzer's invariants, driven deterministically. This runs everywhere,
    # every build — libFuzzer itself is opt-in below.
    pb_add_test(test_fuzz_cache fuzz/fuzz_cache_standalone.c fuzz/fuzz_cache_core.c)
    target_include_directories(test_fuzz_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)

    pb_add_test(test_fuzz_ratelimiter
        fuzz/fuzz_ratelimiter_standalone.c fuzz/fuzz_ratelimiter_core.c)
    target_include_directories(test_fuzz_ratelimiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)

    pb_add_test(test_fuzz_kvcache fuzz/fuzz_kvcache_standalone.c fuzz/fuzz_kvcache_core.c)
    target_include_directories(test_fuzz_kvcache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)

    # Generated per-policy vector tests land here from T06 onward.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen/manifest.cmake)
        include(${CMAKE_CURRENT_SOURCE_DIR}/tests/gen/manifest.cmake)
    endif()
endif()

# --- libFuzzer harnesses -----------------------------------------------------

if(PB_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PB_FUZZ needs Clang; configure with CC=clang")
    endif()

    add_executable(fuzz_cache fuzz/fuzz_cache.c fuzz/fuzz_cache_core.c)
    target_include_directories(fuzz_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
    target_link_libraries(fuzz_cache PRIVATE policybook)
    target_compile_options(fuzz_cache PRIVATE ${PB_WARNINGS} -fsanitize=fuzzer)
    target_link_options(fuzz_cache PRIVATE -fsanitize=fuzzer)

    add_executable(fuzz_ratelimiter fuzz/fuzz_ratelimiter.c fuzz/fuzz_ratelimiter_core.c)
    target_include_directories(fuzz_ratelimiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
    target_link_libraries(fuzz_ratelimiter PRIVATE policybook)
    target_compile_options(fuzz_ratelimiter PRIVATE ${PB_WARNINGS} -fsanitize=fuzzer)
    target_link_options(fuzz_ratelimiter PRIVATE -fsanitize=fuzzer)

    add_executable(fuzz_kvcache fuzz/fuzz_kvcache.c fuzz/fuzz_kvcache_core.c)
    target_include_directories(fuzz_kvcache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
    target_link_libraries(fuzz_kvcache PRIVATE policybook)
    target_compile_options(fuzz_kvcache PRIVATE ${PB_WARNINGS} -fsanitize=fuzzer)
    target_link_options(fuzz_kvcache PRIVATE -fsanitize=fuzzer)
    message(STATUS "policybook: fuzz harnesses enabled")
endif()

# --- 32-bit build ------------------------------------------------------------
#
# Pointer width is the assumption most likely to be wrong in code that indexes
# with uint32_t and sizes with size_t, so the library is built and tested at 32
# bits too. Locally this needs gcc-multilib, which not every machine has; CI
# always has it (implement.md §1).

option(PB_M32 "Build for 32-bit x86 (needs multilib)" OFF)

if(PB_M32)
    if(MSVC)
        message(FATAL_ERROR "PB_M32 is a GCC/Clang option; use the MSVC x86 generator")
    endif()
    target_compile_options(policybook PUBLIC -m32)
    target_link_options(policybook PUBLIC -m32)
    message(STATUS "policybook: building 32-bit (-m32)")
endif()

# --- install -----------------------------------------------------------------
#
# So that `find_package(policybook)` and `pkg-config --cflags policybook` both
# work from a normal prefix. The examples are built this way rather than from
# the build tree, because that is how a reader will build them.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS policybook
    EXPORT policybookTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/policybook
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

# The amalgamated single header ships alongside the tree, for projects that
# would rather have one file than a library.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dist/policybook.h)
    install(FILES dist/policybook.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/policybook/dist)
endif()

install(EXPORT policybookTargets
    FILE policybookTargets.cmake
    NAMESPACE policybook::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/policybook)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/policybookConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/policybookConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/policybookConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/policybook)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/policybookConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/policybookConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/policybook)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/policybook.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/policybook.pc
    @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/policybook.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
