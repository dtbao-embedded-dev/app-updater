# Host test harness (R-TST-02: pure logic must run without a board).
#
# The tests themselves live next to their modules (R-TST-07); this directory
# only holds what it takes to compile and run them on a PC: the Unity runner,
# `fake/` - host implementations of our own driver contracts, which is where a
# test of middleware belongs - and `stub/`, the shrinking set of vendor headers
# that middleware still reaches for directly.
#
#   cmake -S test/host -B build/host && cmake --build build/host && ctest --test-dir build/host
#
# or, the same thing with the paths filled in:  python docs/scripts/tool-esp.py test
cmake_minimum_required(VERSION 3.16)
project(app_updater_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

# clang-tidy reads this to learn the exact flags each file was compiled with,
# which is the only honest way to analyse C. Written to the build directory.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# R-SAN-08: the host suite under ASan/UBSan catches the undefined behaviour a
# target build hides - signed overflow, misaligned access, a read past a
# buffer. Off by default because it needs the sanitizer runtimes, which a
# MinGW toolchain does not ship; CI turns it on.
option(SANITIZE "Build the host tests with ASan and UBSan" OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# --- Unity ---------------------------------------------------------------
# Preferred source is the copy ESP-IDF already vendors, so a developer who can
# build the firmware can run the tests with nothing extra installed. Override
# with -DUNITY_DIR=<dir containing unity.c> for any other checkout.
if(NOT UNITY_DIR)
    # IDF_PATH arrives with backslashes on Windows, and CMake reads `\.` and
    # `\v` in a string as escape sequences - `E:\.espressif\v6.1\esp-idf` is a
    # parse error, not a path. TO_CMAKE_PATH is what makes it one.
    file(TO_CMAKE_PATH "$ENV{IDF_PATH}" idf_path)
    foreach(candidate
            "${idf_path}/components/unity/unity/src"
            "${idf_path}/components/unity/unity")
        if(EXISTS "${candidate}/unity.c")
            set(UNITY_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT UNITY_DIR OR NOT EXISTS "${UNITY_DIR}/unity.c")
    message(FATAL_ERROR
        "Unity not found.\n"
        "  Export ESP-IDF so IDF_PATH points at a checkout that vendors it, or\n"
        "  pass -DUNITY_DIR=<directory containing unity.c>.")
endif()
message(STATUS "Unity: ${UNITY_DIR}")

# Vendor code: never built with -Werror (R-BLD-01).
add_library(unity STATIC "${UNITY_DIR}/unity.c")
target_include_directories(unity PUBLIC "${UNITY_DIR}")

# --- Our code + its tests ------------------------------------------------
add_executable(host_tests
    runner.c
    "${REPO_ROOT}/middleware/fw/src/fw.c"
    "${REPO_ROOT}/middleware/fw/test/test_fw.c"
    "${REPO_ROOT}/middleware/protocol/src/protocol.c"
    "${REPO_ROOT}/middleware/protocol/test/test_protocol.c"
    "${REPO_ROOT}/middleware/command/src/command.c"
    "${REPO_ROOT}/middleware/command/src/command_upgrade.c"
    "${REPO_ROOT}/middleware/command/test/test_command.c"
    "${REPO_ROOT}/middleware/cfg/src/cfg.c"
    "${REPO_ROOT}/middleware/cfg/test/test_cfg.c"
    fake/bsp_fake.c
    fake/nvs_fake.c
    fake/ota_fake.c
    "${REPO_ROOT}/driver/storage/src/storage.c"
    "${REPO_ROOT}/driver/storage/test/test_storage.c"
    "${REPO_ROOT}/application/updater/src/updater.c"
    "${REPO_ROOT}/application/updater/test/test_updater.c")

target_include_directories(host_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/stub"
    "${CMAKE_CURRENT_LIST_DIR}/fake"
    "${REPO_ROOT}/driver/bsp/include"
    "${REPO_ROOT}/driver/ota/include"
    "${REPO_ROOT}/driver/storage/include"
    "${REPO_ROOT}/middleware/fw/include"
    "${REPO_ROOT}/middleware/protocol/include"
    "${REPO_ROOT}/middleware/command/include"
    "${REPO_ROOT}/middleware/cfg/include"
    "${REPO_ROOT}/application/updater/include")

target_link_libraries(host_tests PRIVATE unity)

# The same warning set the firmware is built with, so a host test cannot pass
# on code the target build would reject. That equivalence IS the point of this
# harness, and MSVC cannot provide it - fail rather than quietly test less.
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
        "This harness needs GCC or Clang, not ${CMAKE_C_COMPILER_ID}: it runs the
"
        "  firmware's own warning set, which MSVC does not accept.
"
        "  On Windows:  cmake -S test/host -B build/host -G Ninja
"
        "  (docs/scripts/tool-esp.py test does this for you.)")
endif()

target_compile_options(host_tests PRIVATE
    -Wall -Wextra -Werror
    -Wshadow -Wconversion -Wsign-conversion -Wundef -Wdouble-promotion
    -Wswitch-enum -Wpointer-arith -Wcast-align -Wmissing-prototypes
    -Wstrict-prototypes -Wold-style-definition)

if(SANITIZE)
    # Applied to the test target only: Unity is vendor code (R-SAN-03), and the
    # trap-on-error flag is what turns a silent UB report into a red build.
    target_compile_options(host_tests PRIVATE
        -fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer -g)
    target_link_options(host_tests PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME host_tests COMMAND host_tests)
