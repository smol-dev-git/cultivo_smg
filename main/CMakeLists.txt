idf_component_register(SRCS "miwifi.c" "sensor_sht.c" "main_cultivo.c"
                    PRIV_REQUIRES spi_flash esp_driver_i2c
                    REQUIRES esp_driver_gpio esp_adc esp_wifi nvs_flash esp_event esp_netif
                    INCLUDE_DIRS "")
