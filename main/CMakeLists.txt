if(IDF_VERSION_MAJOR GREATER_EQUAL 5)
    set(reqs driver esp_driver_gpio spi_flash esp_partition esp_system esp_timer nvs_flash)
else()
    set(reqs driver spi_flash esp_timer nvs_flash)
endif()

idf_component_register(SRCS "main.c" "oled.c" "board.c"
                       INCLUDE_DIRS "." "../common"
                       REQUIRES ${reqs})
