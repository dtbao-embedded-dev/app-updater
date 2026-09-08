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

# The generated sdkconfig lives in the build tree, not next to this file.
# ESP-IDF reads sdkconfig.defaults only when sdkconfig does not exist, so a
# sdkconfig sitting in the source tree outlives every clean and silently
# ignores later edits to the defaults - which is how a 16 MB partition table
# gets built against a bootloader header that still says 4 MB. Down here it
# dies with the build directory, and `fullclean` means what it looks like.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(app_updater)
