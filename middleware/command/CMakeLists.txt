# `bsp` and `ota` are in REQUIRES, not PRIV_REQUIRES: command.h holds an
# ota_session_t and the dispatcher's handler prototypes take a bsp_mac_kind_t,
# so both driver headers are part of this module's own interface.
idf_component_register(SRCS "src/command.c"
                            "src/command_upgrade.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp fw ota protocol)

house_warnings(${COMPONENT_LIB})
