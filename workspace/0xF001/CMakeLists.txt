cmake_minimum_required(VERSION 3.22)

# R-BLD-01: -Werror on our code and never on the vendor SDK. Defined here so
# every component of ours can apply it to its own target; a vendor warning we
# cannot fix must not be able to stop our build.
function(house_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Werror
        -Wshadow -Wconversion -Wsign-conversion -Wundef -Wdouble-promotion
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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(app_updater)
