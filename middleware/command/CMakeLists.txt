# `bsp` and `ota` are in REQUIRES, not PRIV_REQUIRES: command.h holds an
# ota_session_t and the dispatcher's handler prototypes take a bsp_mac_kind_t,
# so both driver headers are part of this module's own interface.
#
# `coredump` is the other side of that line and stays PRIV_REQUIRES: nothing
# from coredump.h reaches command.h. The DUMP_READ answer is staged in a plain
# uint8_t array sized by PROTOCOL_DUMP_CHUNK_MAX, and the `state` byte travels
# as a uint8_t, so a consumer of this module never sees a coredump type.
idf_component_register(SRCS "src/command.c"
                            "src/command_dump.c"
                            "src/command_upgrade.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp fw ota protocol
                       PRIV_REQUIRES coredump)

house_warnings(${COMPONENT_LIB})
