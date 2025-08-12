#ifndef SENSOR_SHT_H
#define SENSOR_SHT_H
#include "esp_err.h"
#include "driver/i2c_master.h"


/*
Estructura para almacenar los datos del sensor SHT30
 */
typedef struct {
    float temperatura; // Temperatura en grados Celsius
    float humedad;    // Humedad relativa en porcentaje
} sht30_data_t;


//Inicialización de recursos y bus I2C
esp_err_t i2c_master_init(void);

/*
 Realiza una medición de temperatura y humedad en modo "single shot" (sin clock stretching)
 y lee los datos del sensor SHT30.
 */
esp_err_t i2c_read_sht(sht30_data_t *datos);
//esp_err_t i2c_read_sht(i2c_port_t i2c_port, float *temp_sht, float *hum_sht);

#endif // SENSOR_SHT_H