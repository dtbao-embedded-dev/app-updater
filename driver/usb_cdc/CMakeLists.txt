idf_component_register(SRCS "src/usb_cdc.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_tinyusb freertos)

house_warnings(${COMPONENT_LIB})
