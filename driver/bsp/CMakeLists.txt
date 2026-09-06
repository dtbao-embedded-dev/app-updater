idf_component_register(SRCS "src/bsp.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_hw_support spi_flash)

house_warnings(${COMPONENT_LIB})
