idf_component_register(SRCS "src/updater.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES ota_http storage app_update esp_partition)

house_warnings(${COMPONENT_LIB})
