idf_component_register(SRCS "src/protocol.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw)

house_warnings(${COMPONENT_LIB})
