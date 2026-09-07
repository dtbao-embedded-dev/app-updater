idf_component_register(SRCS "src/cfg.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES esp_rom)

house_warnings(${COMPONENT_LIB})
