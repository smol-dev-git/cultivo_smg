idf_component_register(SRCS "sensor_sht.c" "main_cultivo.c"
                    PRIV_REQUIRES spi_flash esp_driver_i2c
                    REQUIRES esp_driver_gpio "esp_adc"
                    INCLUDE_DIRS "")
