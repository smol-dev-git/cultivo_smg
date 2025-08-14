/* --- Proyecto Módulo Asistente en el cultivo de cannabis medicinal ---
* - TFM Máster en sistemas electrónicos para entornos inteligentes
* - Estudiante/desarrollador: Ing Santiago Molina Gomez
* - Universidad de Málaga, España
*/
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "main_cultivo.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#include "i2c_types.h"
#include "driver/i2c_master.h"
#include "sensor_sht.h"
#include "hal/adc_types.h"



#include <stdlib.h>
#include <string.h>
#include "esp_adc/adc_oneshot.h"
#include "soc/soc_caps.h"

//--------------- Definición de variables

static const char *TAG = "SM_CULTIVO";
static const char *TAG_ACTUADORES = "ACTU-";
static const char *TAG_DEBUG = "DEBUG-";
static const char *TAG_SENSORES = "SENSO-";


static TimerHandle_t timer_manejador;

int humedad_ambiente_int = 0;
int humedad_ambiente_ext = 0;
float temperatura = 0;
bool modo_Auto_ON = false;
bool estado_caudal = false;
bool estado_ventilar = false;
bool estado_luz;

bool flag_error_i2c = false; //Error en inicialización recursos i2c_master_init()

stru_lec_sensores_t lecturas;  //Estructura general que almacena los valores leídos
stru_umbrales_var_t umbrales_actuadores;
bool array_accion_actuadores[pos_actuadores_tamaño] = {false}; //Array [0,0,0,0] que contiene los estados de los actuadores
sht30_data_t lecturas_sht30;

static adc_oneshot_unit_handle_t adc1_handle; //Manejador del canal ADC
static int adc_raw[10];
float lectura_adc; // Variable para almacenar lectura de función del ADC

TaskHandle_t tareaprincipal_manejador;

time_t hora_sistema_sensores; // variable para almacenar la hora del sistema
char strftime_buf[64]; //Conversión de hora de sistema
struct tm timeinfo; //Hora del sistema estructura de tiempo


//-- Configuraciones iniciales
void config_inicial(void){

    ESP_LOGI(TAG, "Inicializando sistema ESP32...");

    //Configuración de pines
    gpio_set_direction(PIN_VENTILAR, GPIO_MODE_OUTPUT); //P25
    gpio_set_direction(PIN_ILUMINAR, GPIO_MODE_OUTPUT); //P26
    gpio_set_direction(PIN_REGAR, GPIO_MODE_OUTPUT); //P27
    gpio_set_direction(PIN_FERTILIZAR, GPIO_MODE_OUTPUT); //P32
    gpio_set_direction(PIN_SENSOR_CAUDAL, GPIO_MODE_INPUT); //P14
    
    //--- Inicialización I2C, bus y esclavo SHT30
   esp_err_t ret = i2c_master_init();
   if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al inciar la comunicacion I2C: %s", esp_err_to_name(ret)); 
        flag_error_i2c = true;
    } 

    adc_init();

    //Set Umbrales para iniciar operaciones
    set_umbrales_actuadores();
}

//Inicialización del ADC
esp_err_t adc_init(void){

//-------------ADC1 Ini---------------//

    
    //Config a añadir al manejador.
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config Canal---------------//
    adc_oneshot_chan_cfg_t config_channel = {
        .atten = ADC_ATTEN_DB_12, //Máxima atenuación para lecturas hasta 3.3V
        .bitwidth = ADC_BITWIDTH_10, //10 Bits de resolución
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_7, &config_channel)); //ADC1, Canal 7 pin GPIO35   

    //adc_continuous_channel_to_io(ADC_UNIT_1, ADC_CHANNEL_7, *io_adc);
    //ESP_LOGW("TAG", " --- IO ADC Channel 1: %i", *io_adc);

    return ESP_OK;
}

void set_umbrales_actuadores(void){
    
    umbrales_actuadores.umb_hum_suelo= 30 ;
    umbrales_actuadores.umb_temp = 35.5; 
    umbrales_actuadores.umb_hora_luz_off= 3;
    umbrales_actuadores.umb_hora_luz_on =18 ;
    umbrales_actuadores.umb_hum_interna_mayor = 30;
    
    ESP_LOGW("TAG", "HARDCODE UMBRALES DE VARIABLES");
    ESP_LOGW("TAG", "Umbrales ----> Umb.HumSuelo = %f  Umb.Temp=%f  Umb.Humedad=%f HrON:%i HrOFF:%i", umbrales_actuadores.umb_hum_suelo, umbrales_actuadores.umb_temp, umbrales_actuadores.umb_hum_interna_mayor, umbrales_actuadores.umb_hora_luz_on, umbrales_actuadores.umb_hora_luz_off);
}


void tarea_principal(){
    u_int8_t count = 0;
    while(1){
        //Espera de notificación de timer
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        tiempo_sistema();
        count ++;
        if(count == 3){
            count = 0;
            sensores();
            actuadores();
        }
        //vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}



void app_main(void)
{
    printf("--- MODULO ASISTENTE DE CULTIVO ---\n");
    printf("-Iniciando placa ESP32...-\n\n\n");
    config_inicial();

    xTaskCreate(tarea_principal, "main_task", 2048, NULL, 1, &tareaprincipal_manejador);

    timer_manejador = xTimerCreate("Timer_Smol", 5*configTICK_RATE_HZ, pdTRUE, (void *) 1, timer_funcion);
    
    if (timer_manejador == NULL)
    {
        ESP_LOGE("Timer_Smol", "Error Grave iniciando timer del sistema para tiempo");
    }
    xTimerStart(timer_manejador, 0);

    while (true)
    {
        //CENTRO TAREA MAIN -ESPERA INFINITA-
        vTaskDelay(portMAX_DELAY);
    }

    esp_restart();
}

void timer_funcion(TimerHandle_t pxTimer){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    printf("\n\n\n\nTIMER notify from ISR -5s-\n");
    vTaskNotifyGiveFromISR(tareaprincipal_manejador, &xHigherPriorityTaskWoken); //Cambio de contexto tarea prioritaria
}



//-- Función que verifica el llamado a la acción y ejecuta para los actuadores y salidas del sistema
void actuadores(void){
//----- Lógica de actuadores según variables
    /*
    lecturas.humedad_ext = 0;
    lecturas.humedad_int = 0;
    lecturas.temperatura = 0;
    lecturas.presencia_caudal = 0;
    lecturas.humedad_suelo = 0;
    */

    //DEBUG ----- Prueba Fertilización sin activar  riego
    //ESP_LOGW(TAG_DEBUG, "Ingresando a actuar_fertilizar() error esperado "); 
    //array_accion_actuadores[pos_array_FERTILIZAR] = true;
    //actuar_fertilizar(array_accion_actuadores[pos_array_FERTILIZAR]);
    //DEBUG


    //DEBUG ----- Prueba Fertilización activando riego
    //ESP_LOGW(TAG_DEBUG, "Ingresando a actuar_fertilizar Riego esperado.");
    //array_accion_actuadores[pos_array_REGAR] = true;
    //actuar_regar();
    //actuar_fertilizar();
    //DEBUG

    //------- MODO AUTOMÁTICO -----------//
    if (modo_Auto_ON == true){    
        if(lecturas.humedad_suelo < umbrales_actuadores.umb_hum_suelo){
           
        }
        //Si la humedad interna es mayo que la externaa
        if((lecturas.humedad_int - lecturas.humedad_ext) > umbrales_actuadores.umb_hum_interna_mayor){

        }
        //Si la temperatura es mayor al umbral
        if(lecturas.temperatura > umbrales_actuadores.umb_temp){

        }
    }
    
    /* -----Cambio de estados de actuadores en base a Array array_accion_actuadores
    gpio_set_level(PIN_VENTILAR, array_accion_actuadores[pos_array_VENTILAR]);
    gpio_set_level(PIN_ILUMINAR, array_accion_actuadores[pos_array_ILUMINAR]);
    gpio_set_level(PIN_REGAR, array_accion_actuadores[pos_array_REGAR]);
    gpio_set_level(PIN_FERTILIZAR, array_accion_actuadores[pos_array_FERTILIZAR]);
    */
    
}


//TODO:
//-Return o goto en error en flag error i2c
//-Factorizar casos de error para mejor visualización y manejo
//-Logs de lecturas SHT, factorizar hacia dentro de funciòno SHT,no dejar en sensores()
//-- Obtebnción de hora del sistema y Lectura de sensores cada N segundos



void tiempo_sistema(){
    time(&hora_sistema_sensores); //Captura de hora POSIX del sistema
    setenv("TZ", "COT-5", 1); // Set zona horaria
    tzset();
    localtime_r(&hora_sistema_sensores, &timeinfo);
    int sistema_segundos = timeinfo.tm_sec; //Tiempo del sistema en segundos
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo); //print tiempo  a cadena de texto 
    ESP_LOGI(TAG, "Hora Sistema: %s   -SECS- :  %d", strftime_buf, sistema_segundos);
}

stru_lec_sensores_t  sensores(void){
    esp_err_t ret = ESP_OK;

    lecturas.humedad_ext = 0;
    lecturas.humedad_int = 0;
    lecturas.temperatura = 0;
    lecturas.presencia_caudal = 0;
    lecturas.humedad_suelo = 0;

    //------------------ LECTURA sensor humedad suelo ------------------
    humedad_suelo_lectura(&lectura_adc);

    //Verificacion de valor correcot humedad del suelo
    if (lectura_adc >= 0.2 && lectura_adc <= 3.3 ){
        lectura_adc = lecturas.humedad_suelo;      
    }else{
        ESP_LOGW(TAG, "Error Lectura ADC - Humedad del suelo fuera de rango -");
        lecturas.humedad_suelo = -0.1;
    } 

    //------------------ LECTURA I2C Sensor SHT30 humedad y temperatura ambiental  ------------------
    if (flag_error_i2c){
        ESP_LOGE(TAG_SENSORES, "Error I2C SHT30 - Imposible leer temperatura y humedad ambiental");
        lecturas.temperatura = -0.1;
        lecturas.humedad_int = -0.1;
        //TODO
        //Pendiente "Return" o "goto" tras error
    }

    ret = i2c_read_sht(&lecturas_sht30);
    if(ret == ESP_OK){
        lecturas.humedad_int = lecturas_sht30.humedad;
        lecturas.temperatura = lecturas_sht30.temperatura;
        ESP_LOGI(TAG_SENSORES, "Lectura i2c_read_sht() Ok - Se asignan valores de SHT30 - HUM %f   TEMP %f", lecturas.humedad_int, lecturas.temperatura);
    }else{
        ESP_LOGE(TAG_SENSORES, "Error al leer valores SHT30 "); 
        lecturas.humedad_int = -0.1;
        lecturas.temperatura = -0.1;
        return lecturas;
    }
     

    //ESP_LOGI(TAG, "LECTURA SENSORES ----> Hum_ext:%f - Hum_int: %f - Temp: %f - Caudal: %s", lecturas.humedad_ext, lecturas.humedad_int, lecturas.temperatura, lecturas.presencia_caudal ? "true" : "false"  );
    //ESP_LOGI(TAG, "LECTURA SENSORES ----> Hum_ext:off - Hum_int: %f - Temp: %f - Hum-Suelo: %f  Caudal: %s", lecturas.humedad_int, lecturas.temperatura, lecturas.humedad_suelo, lecturas.presencia_caudal ? "true" : "false"  );
    //ESP_LOGI(TAG_ACTUADORES, "ACCION ACTUADORES ----> Iluminar:%s - Ventilar:%s - Regar:%s, Fertilizar:%s", array_accion_actuadores[pos_array_ILUMINAR] ? "ON" : "OFF", array_accion_actuadores[pos_array_VENTILAR] ? "ON" : "OFF", array_accion_actuadores[pos_array_REGAR] ? "ON" : "OFF", array_accion_actuadores[pos_array_FERTILIZAR] ? "ON" : "OFF");
    
    return lecturas;
}


esp_err_t humedad_suelo_lectura(float *lectura_adc){
    
    static const char *TAG_ADC = "ADC_smol";

    if (lectura_adc == NULL) {
        ESP_LOGE(TAG_ADC, "Error: Puntero de lectura ADC es NULL.");
        return ESP_FAIL; // O el código de error apropiado
    }

    //Ojo con el manejo de errores en esta sección y la posterior consulta al ADC.
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_7, &adc_raw[0]));

    *lectura_adc = (adc_raw[0] * 3.3) / 1024;
    ESP_LOGI(TAG_ADC, "--- LECTURA ADC_%d Channel[%d]   Raw Data: %d   Voltage:%f", ADC_UNIT_1 + 1, ADC_CHANNEL_7, adc_raw[0], *lectura_adc );
    
    return ESP_OK;
}

void actuar_ventilar(bool estado_act){

    gpio_set_level(PIN_VENTILAR, estado_act ? 0 : 1);
    ESP_LOGI(TAG_ACTUADORES, "Ventilar: %s", estado_act ? "ON" : "OFF");

}
void actuar_iluminar(bool estado_act){
    
    gpio_set_level(PIN_ILUMINAR, estado_act ? 0 : 1);
    ESP_LOGI(TAG_ACTUADORES, "Iluminar: %s", estado_act ? "ON" : "OFF");

}
void actuar_regar(){
        
    gpio_set_level(PIN_REGAR, array_accion_actuadores[pos_array_REGAR] ? 1 : 0);
    ESP_LOGI(TAG_ACTUADORES, "Regar: %s", array_accion_actuadores[pos_array_REGAR] ? "ON" : "OFF");

}
void actuar_fertilizar(){
    
    ESP_LOGI(TAG_ACTUADORES, "actuar_fertilizar(bool)....");
    

    //Comprobación estado bomba de riego y caudal para fertilizar
    ESP_LOGI(TAG_ACTUADORES, "Verificando bomba de riego...");
    if (!array_accion_actuadores[pos_array_REGAR]){
        ESP_LOGW(TAG_ACTUADORES, "Riego no activo, Imposible efectuar fertilizacion");
        return;
    }else if(!gpio_get_level(PIN_SENSOR_CAUDAL)){
        ESP_LOGE(TAG_ACTUADORES, "Sin caudal de riego (P14), Imposible fertilizar");
        return;
    }else ESP_LOGW(TAG_ACTUADORES, "Caudal de riego OK(P14), Inciando Fertilización");

    gpio_set_level(PIN_FERTILIZAR, array_accion_actuadores[pos_array_FERTILIZAR] ? 1 : 0);
    ESP_LOGI(TAG_ACTUADORES, "Fertilizar: %s", array_accion_actuadores[pos_array_FERTILIZAR] ? "ON" : "OFF");
}

void chipinfo(void){
     /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    /*if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }*/
    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
        (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

/*
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();*/

}