idf_component_register(SRCS "src/storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES nvs_flash esp_rom)

house_warnings(${COMPONENT_LIB})
