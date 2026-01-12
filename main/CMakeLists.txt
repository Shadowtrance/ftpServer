idf_component_register(SRCS "main.cpp"
                            "filesystem.cpp"
                            "ftpServer.cpp"
                            "ftpUiScreen.cpp"
                            "spinner_img.c"
                            "displayConfig.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl esp_lcd esp_lcd_touch_gt911 esp_lvgl_port fatfs driver
                       PRIV_REQUIRES esp_wifi nvs_flash esp_netif)