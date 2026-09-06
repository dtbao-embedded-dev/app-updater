idf_component_register(SRCS "src/ota_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES esp_http_client esp-tls)

house_warnings(${COMPONENT_LIB})
