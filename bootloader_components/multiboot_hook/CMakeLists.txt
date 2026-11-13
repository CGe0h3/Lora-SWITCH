set(reqs bootloader_support esp_rom spi_flash)

# Resolve symlinks (the IDF 4.4 bootloader project links this folder)
get_filename_component(_self "${CMAKE_CURRENT_LIST_FILE}" REALPATH)
get_filename_component(_dir "${_self}" DIRECTORY)

idf_component_register(SRCS "hooks.c"
                       INCLUDE_DIRS "${_dir}/../../common"
                       REQUIRES ${reqs})

# Hooks are weak symbols in the bootloader: force the linker to keep this file.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")
