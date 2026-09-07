# `ota` is in REQUIRES, not PRIV_REQUIRES: command.h holds an ota_session_t,
# so the driver header is part of this module's public interface.
idf_component_register(SRCS "src/command.c"
                            "src/command_upgrade.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw ota protocol
                       PRIV_REQUIRES esp_hw_support esp_system esp_rom freertos)

house_warnings(${COMPONENT_LIB})
