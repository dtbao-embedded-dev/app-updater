idf_component_register(SRCS "src/coredump.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES espcoredump esp_partition)

house_warnings(${COMPONENT_LIB})
