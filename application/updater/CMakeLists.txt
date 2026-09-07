idf_component_register(SRCS "src/updater.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       # `storage` is gone from this list: updater.c includes no header of
                       # it, and the module it named moved to driver/ in this commit.
                       # `ota_http`, `app_update` and `esp_partition` are equally unused
                       # today and deliberately left - the CHECKING and DOWNLOADING steps
                       # are written against them (see the TODOs in src/updater.c).
                       PRIV_REQUIRES ota_http app_update esp_partition)

house_warnings(${COMPONENT_LIB})
