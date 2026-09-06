idf_component_register(SRCS "src/bsp.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_hw_support)

house_warnings(${COMPONENT_LIB})
