idf_component_register(SRCS "src/ota.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update esp_app_format esp_partition)

house_warnings(${COMPONENT_LIB})
