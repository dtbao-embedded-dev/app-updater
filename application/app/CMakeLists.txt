# This component replaces the ESP-IDF "main" component: the entry point lives
# in the application layer, so workspace/ holds no source of ours (R-RPO-10).
# Renaming main costs the automatic dependency list, so every dependency is
# named here on purpose.
idf_component_register(SRCS "src/app.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fw
                       PRIV_REQUIRES bsp
                                     cfg
                                     command
                                     protocol
                                     storage
                                     updater
                                     usb_cdc
                                     app_update
                                     esp_app_format
                                     esp_partition
                                     esp_timer
                                     esp_hw_support
                                     freertos
                                     nvs_flash)

house_warnings(${COMPONENT_LIB})

# The banner names the commit the image was built from. VERSION covers the
# release number (R-VER-01) and deliberately says nothing about which commit
# that number was cut on, which is the thing a field unit's log has to answer.
# Run here rather than at the top level because this is the only consumer, and
# as a PRIVATE definition so the vendor SDK never sees it.
execute_process(COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}/../.." rev-parse HEAD
                OUTPUT_VARIABLE app_git_commit
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE app_git_status)
if(NOT app_git_status EQUAL 0)
    # A tarball export or a machine with no git still has to build.
    set(app_git_commit "unknown")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_GIT_COMMIT="${app_git_commit}")
