#include "esp_log.h"
#include "esp_system.h"
#include "sensor_sht.h"
#include "main_cultivo.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>


//Definiciones I2c
#define SHT30_ADDR 0x44 // Dirección I2C por defecto del SHT30
#define SHT30_READ 0x240B //Comandoi de lectura sin clock stretching y media repetibilidad

#define I2C_PORT_NUM_0 0
#define I2C_MASTER_SCL_ELPIN 22
#define I2C_MASTER_SDA_ELPIN 21
#define DATA_I2_LENGTH 100 //Data I2C Lenght
#define I2C_TIMEOUT_MS 1000 

//Handlers manejadores para el bus I2C y el dispositivo sht30
static i2c_master_dev_handle_t master_i2c_manejador = NULL;
static i2c_master_bus_handle_t bus_manejador = NULL;

static const char *TAG = "I2C_smol";
uint8_t i2c_data_buffer[6]; //Buffer de i2c, array de [6]
size_t i2c_bytes_read = 6;  //Cantidad de bytes a leer 

//Inicio de bus I2c y adición de dispositivo esclavo (Sensor SHT30)
esp_err_t i2c_master_init(void){

    ESP_LOGI(TAG, "Inicializando recursos I2C...");
    //--- Configuraciones I2C
    i2c_master_bus_config_t i2c_master_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_ELPIN,
        .sda_io_num = I2C_MASTER_SDA_ELPIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    //Creación bus I2C.
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_config, &bus_manejador));
    
    //---I2C config dispositivo sensor
    i2c_device_config_t dispo_i2c_config = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = SHT30_ADDR,
        .scl_speed_hz = 100000,
    };
    
    //Se aloja la info del dispositivo
    //i2c_master_dev_handle_t mastero_i2c_manejador;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_manejador, &dispo_i2c_config, &master_i2c_manejador));

    ESP_LOGI(TAG, "I2C - Inicio OK (Bus y SHT30)");

    return ESP_OK;
}

//esp_err_t i2c_read_sht(float *temp_sht, float *hum_sht){
esp_err_t i2c_read_sht(sht30_data_t *sht30_data){

    if (master_i2c_manejador == NULL) {
        ESP_LOGE(TAG, "El dispositivo SHT30 no está inicializado.");
        return ESP_FAIL;
    }

    uint8_t write_buffer_sht[2];                      // Buffer para enviar comando de lectura 0x240B
    write_buffer_sht[0] = (SHT30_READ >> 8) & 0xFF ; //Byte más significativo -0x24-
    write_buffer_sht[1] = SHT30_READ & 0xFF; //Byte menos significativo  -0x0B-

    //---Transmisión de datos I2C, se solicita medida---
    //esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms)
    ESP_ERROR_CHECK(i2c_master_transmit(master_i2c_manejador, write_buffer_sht, sizeof(write_buffer_sht), I2C_TIMEOUT_MS));

    //Comporbación de error de envío
    vTaskDelay(200 / portTICK_PERIOD_MS);
    //vTaskDelay(pdMS_TO_TICKS(15));
    
    //----Lectura de valores retornados----
    //Lectura de i2c con tiempo de espera indefinido
    //ESP_ERROR_CHECK(i2c_master_receive(dispo_i2c_manejador, *i2c_data_buffer, i2c_bytes_read, -1));//-1 indica espera indefinida

    uint8_t data[6]; // 6 bytes: 2 temp + CRC, 2 hum + CRC
    esp_err_t ret = i2c_master_receive(master_i2c_manejador, data, sizeof(data), I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al leer datos SHT30: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Procesar los datos (sin verificar CRC por simplicidad, pero recomendado en producción)
    uint16_t raw_temp = (data[0] << 8) | data[1];
    uint16_t raw_hum = (data[3] << 8) | data[4];

    // Cálculo de temperatura: (valor_sensor / 65535.0) * 175.0 - 45.0
    sht30_data->temperature = (float)raw_temp * 175.0f / 65535.0f - 45.0f;

    // Cálculo de humedad: (valor_sensor / 65535.0) * 100.0
    sht30_data->humidity = (float)raw_hum * 100.0f / 65535.0f;
    ESP_LOGI(TAG, "---- LECTURA SHT30 - HUM: %f  TEMP: %f", sht30_data->humidity, sht30_data->temperature);
    return ESP_OK;
}
