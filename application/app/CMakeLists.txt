# This component replaces the ESP-IDF "main" component: the entry point lives
# in the application layer, so workspace/ holds no source of ours (R-RPO-10).
# Renaming main costs the automatic dependency list, so every dependency is
# named here on purpose.
idf_component_register(SRCS "src/app.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES bsp
                                     storage
                                     updater
                                     app_update
                                     esp_app_format
                                     esp_partition
                                     esp_timer
                                     freertos
                                     nvs_flash)

house_warnings(${COMPONENT_LIB})
