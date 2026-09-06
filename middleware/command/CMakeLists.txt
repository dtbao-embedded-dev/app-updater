idf_component_register(SRCS "src/command.c"
                            "src/command_upgrade.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw protocol
                       PRIV_REQUIRES app_update esp_app_format esp_partition esp_hw_support
                                     esp_system esp_rom freertos)

house_warnings(${COMPONENT_LIB})
