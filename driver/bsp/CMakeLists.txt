idf_build_get_property(target IDF_TARGET)

# The per-chip half of this module lives in one file per MCU family, picked by
# IDF_TARGET (R-LIB-02). Adding a chip is adding a file here, never an #ifdef
# inside bsp.c. Checked explicitly so a new target fails with an instruction
# instead of a CMake complaint about a missing source.
set(BSP_PORT_SRC "src/port/bsp_${target}.c")
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${BSP_PORT_SRC}")
    message(FATAL_ERROR
            "driver/bsp has no port for IDF_TARGET '${target}'. "
            "Add ${BSP_PORT_SRC} implementing src/bsp_priv.h.")
endif()

idf_component_register(SRCS "src/bsp.c" "${BSP_PORT_SRC}"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       PRIV_REQUIRES esp_driver_gpio esp_hw_support spi_flash)

house_warnings(${COMPONENT_LIB})
