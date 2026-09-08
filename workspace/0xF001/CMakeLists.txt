cmake_minimum_required(VERSION 3.22)

# R-BLD-01: -Werror on our code and never on the vendor SDK. Defined here so
# every component of ours can apply it to its own target; a vendor warning we
# cannot fix must not be able to stop our build.
#
# -Wundef is deliberately ABSENT from this list, and this is the reason.
# It is a preprocessor warning: it fires where a header is included, not where
# the header lives, so a per-target flag cannot keep it off the vendor SDK.
# ESP-IDF's own log headers test BOOTLOADER_BUILD, CONFIG_LOG_COLORS_SUPPORT
# and others without defining them, so every file of ours that includes
# esp_log.h fails the build. Observed, not assumed - CI run 34031391115.
# The check still runs where it can work: test/host keeps it, and there no SDK
# header is in scope.
function(house_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Werror
        -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion
        -Wswitch-enum -Wpointer-arith -Wcast-align -Wmissing-prototypes
        -Wstrict-prototypes -Wold-style-definition)
endfunction()

# The three source layers of R-RPO-09, reached from here. No module of ours
# lives under workspace/ (R-RPO-10), and nothing is copied or symlinked: one
# driver/bsp/ compiles into every product that lists it.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../application"
    "${CMAKE_CURRENT_LIST_DIR}/../../middleware"
    "${CMAKE_CURRENT_LIST_DIR}/../../driver")

# There is no "main" component: application/app provides app_main(). Every
# component found is therefore built. To trim the first build, uncomment the
# line below - app pulls in what it REQUIRES and nothing else.
# set(COMPONENTS app)

# R-VER-01: one source of truth for the version. PROJECT_VER lands in the image
# header, so esp_app_get_description()->version is the number in VERSION and
# cannot drift from it (R-VER-02, R-VER-11).
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../VERSION" PROJECT_VER)
string(STRIP "${PROJECT_VER}" PROJECT_VER)

# And CMake has to be told to watch that file, because `file(READ)` does not
# register a dependency on what it reads. Without this line a version bump
# changes nothing until something else forces a reconfigure: the cache still
# holds the old PROJECT_VER, ninja has no reason to re-run cmake, and the image
# reports the previous number while VERSION reads the new one.
#
# Observed, not assumed: bumping 0.1.0 -> 0.1.1 and rebuilding left
# `esptool image-info` reporting `App version: 0.1.0`; deleting CMakeCache.txt
# and rebuilding reported 0.1.1. CI and the release workflow never saw it,
# because a fresh checkout configures from scratch - which is exactly what
# makes it a local-only trap that ships nothing wrong and wastes an afternoon.
#
# It also makes `versioning-and-release.md` step 6 honest. That step says a
# mismatch between the reported version and VERSION means "a copy escaped step
# 1", and sends the reader hunting for a hardcoded number. There is no such
# copy; there was a missing dependency.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/../../VERSION")

# The generated sdkconfig lives in the build tree, not next to this file.
# ESP-IDF reads sdkconfig.defaults only when sdkconfig does not exist, so a
# sdkconfig sitting in the source tree outlives every clean and silently
# ignores later edits to the defaults - which is how a 16 MB partition table
# gets built against a bootloader header that still says 4 MB. Down here it
# dies with the build directory, and `fullclean` means what it looks like.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(app_updater)
