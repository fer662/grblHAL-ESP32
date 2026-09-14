idf_component_register(SRCS "usb_probe.c" INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_uart esp_hw_support esp_system esp_timer spi_flash)
