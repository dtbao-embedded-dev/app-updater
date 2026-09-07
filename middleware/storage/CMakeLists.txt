idf_component_register(SRCS "src/storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES nvs_flash)

house_warnings(${COMPONENT_LIB})
