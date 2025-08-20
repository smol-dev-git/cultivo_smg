/* --- Proyecto Módulo Asistente en el cultivo de cannabis medicinal ---
* - TFM Máster en sistemas electrónicos para entornos inteligentes
* - Estudiante/desarrollador: Ing Santiago Molina Gomez
* - Universidad de Málaga, España
*/
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
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
#include "nvs_flash.h"
#include "hal/adc_types.h"

//Cabezeras propias desarrolladas
#include "miwifi.h"
#include "sensor_sht.h"


#include "esp_adc/adc_oneshot.h"
#include "soc/soc_caps.h"


//--------------- Definición de variables

static const char *TAG = "SM_CULTIVO";
static const char *TAG_ACTUADORES = "ACTU-";
static const char *TAG_DEBUG = "DEBUG-";
static const char *TAG_SENSORES = "SENSO-";


static TimerHandle_t timer_manejador;


//Variables principales del invernadero
int humedad_ambiente_int = 0;
int humedad_ambiente_ext = 0;
float temperatura = 0;
bool modo_Auto_ON = false;
bool estado_caudal = false;
bool estado_ventilar = false;
bool estado_luz;

//Flag error
bool flag_error_i2c = false; //Error en inicialización recursos i2c_master_init()
bool flag_tiempo_sync = false; 

//Variables de operación del sistema
stru_lec_sensores_t lecturas;  //Estructura general que almacena los valores leídos
stru_umbrales_var_t umbrales_actuadores;
bool array_accion_actuadores[pos_actuadores_tamaño] = {false}; //Array [0,0,0,0] que contiene los estados de los actuadores
sht30_data_t lecturas_sht30;
time_t fertilizaciones[3] = {0, 0, 0}; //Array con las fertilizaciones programadas, 0 indica sin programar

static adc_oneshot_unit_handle_t adc1_handle; //Manejador del canal ADC
static int adc_raw[10];
int lectura_adc; // Variable para almacenar lectura de función del ADC

TaskHandle_t tareaprincipal_manejador;
TaskHandle_t tarearegar_manejador;
TaskHandle_t tareaventilar_manejador;
TaskHandle_t tareawifi_manejador;

time_t hora_sistema; // variable para almacenar la hora del sistema
char strftime_buf[64]; //Conversión de hora de sistema
struct tm timeinfo; //Hora del sistema estructura de tiempo

time_t hora_sistema_ajustado_col; // variable para almacenar la hora del sistema ajustada a Colombia
struct tm tiempo_ajustado; //Hora del sistema estructura de tiempo
struct tm timeinfo_utc; //Hora del sistema estructura de tiempo UTC
char buf_ajustado[64];
char buf[64];


//-- Configuraciones iniciales
void config_inicial(void){

    ESP_LOGI(TAG, "Inicializando config_inicial ESP32...");

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

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    //Set Umbrales para iniciar operaciones
    set_umbrales_actuadores();

    // Búsqueda de valores programados de fertilizaciones en NVS 
    buscar_fertilizaciones_nvs();
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


/*Tarea que activa PIN_VENTILAR - 25; y asigan valor a array_accion_actuadores[pos_array_VENTILAR]
LA función se bloquea a la espera de TaskNotifyWait; siempre se debe actualizar por código, el estado de la posición array anterior a 1.
La función tiene tiempo de espera con la finalidad de actuar mediante "pulsos" de x tiempo en sus salidas.
*/
void tarea_ventilar(){
    uint32_t valor_notificacion;

    while(1){
    
        //Notificacion con espera de tiempo
        ESP_LOGW("task_ventilar", "TAREA VENTILAR BLOQUEADA");
        //BaseType_t xResult = xTaskNotifyWait(0, ULONG_MAX, &valor_notificacion, pdMS_TO_TICKS( TIEMPO_VENTILAR_ON * 1000));
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        //Paso de bloqueo por notificación, es decir, se quizo activar ventilar por notificación
         //if (xResult == pdTRUE) {
         if (array_accion_actuadores[pos_array_VENTILAR] == true) {
            gpio_set_level(PIN_VENTILAR, array_accion_actuadores[pos_array_VENTILAR] ? 1 : 0);
            ESP_LOGW("task_ventilar", "ACTIVACION Ventilacion  VENTILACION: %s", array_accion_actuadores[pos_array_VENTILAR] ? "ON" : "OFF");

         }else{ //Paso de bloqueo por expiración de tiempo
            
            gpio_set_level(PIN_VENTILAR, array_accion_actuadores[pos_array_VENTILAR] ? 1 : 0);
            ESP_LOGW("task_ventilar", "DESACTIVAR Ventilacion  VENTILACION: %s", array_accion_actuadores[pos_array_VENTILAR] ? "ON" : "OFF");
        }
        
    }
}


void tarea_regar(){
    uint32_t valor_notificacion;
    while(1){
        //Notificacion con espera de tiempo
        ESP_LOGW("task_regar", "TAREA REGAR BLOQUEADA---- 20s o Notify");
        BaseType_t xResult = xTaskNotifyWait(0, ULONG_MAX, &valor_notificacion, pdMS_TO_TICKS( TIEMPO_REGAR_ON * 1000));
        //ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        //Paso de bloqueo por notificación, es decir, se quizo activar venitlar por notificación
         if (xResult == pdTRUE) {
            gpio_set_level(PIN_REGAR, array_accion_actuadores[pos_array_REGAR] ? 1 : 0);
            ESP_LOGW("task_regar", "ACTIVACION TAREA_Ventilar VENTILACION: %s", array_accion_actuadores[pos_array_REGAR] ? "ON" : "OFF");

         }else{ //Paso de bloqueo por expiración de tiempo
            array_accion_actuadores[pos_array_REGAR] = false;
            gpio_set_level(PIN_REGAR, array_accion_actuadores[pos_array_REGAR] ? 1 : 0);
            ESP_LOGW("task_regar", "TIEMPO EXPIRADO EN TAREA_Regar RIEGO: %s", array_accion_actuadores[pos_array_REGAR] ? "ON" : "OFF");
        }
    }
}

void tarea_wifi(){
    ESP_LOGI(TAG, "Tarea WiFi iniciada");
    wifi_init_sta();

    ESP_LOGW(TAG, "Tarea WiFi SUSPENDIDA INDEFINIDA -----");
    uint8_t contador = 0;
    while(1){
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera de 1 segundo
        contador++;
        if (contador >= 10  && !flag_tiempo_sync) {
            sntp_tiempo_sincro();
            contador = 0;
        }
    }
}

void tarea_principal(){
    u_int8_t count = 0;
    //BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    while(1){
        //Espera de notificación de timer
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(flag_tiempo_sync){
            tiempo_sistema(); //Actualiza hora del sistema
        }else printf("Tiempo no sincronizado, no se imprime\n");
        count ++;
        if(count == 3){
            printf("\n\nCONTEO 3 EN TAREA PRINCIPAL, SE EJECUTA LÓGICA\n");
            count = 0;
            sensores();
            actuadores();
            printf("UNLOCK Prueba TAREA  VENTILAR \n");
            xTaskNotifyGive(tareaventilar_manejador); //Cambio de contexto tarea prioritaria
        }

        //Hardocde de almacenado de fertilizaciones, Ya se han asignado fertilizaiones a la NVS
        /*
        if(count == 10 ){
             //hardcode_fertilizaciones();
             count = 0
        }*/
        
    }
}



void app_main(void)
{
    printf("--- MODULO ASISTENTE DE CULTIVO ---\n");
    printf("-Iniciando placa ESP32...-\n\n\n");
    config_inicial();

    xTaskCreate(tarea_principal, "main_task", 4096, NULL, 4, &tareaprincipal_manejador);
    xTaskCreate(tarea_ventilar, "ventilar_task", 2048, NULL, 2, &tareaventilar_manejador);
    xTaskCreate(tarea_regar, "regar_task", 2048, NULL, 2, &tarearegar_manejador);
    xTaskCreate(tarea_wifi, "wifi_task", 4096, NULL, 2, &tareawifi_manejador);
    

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
    printf("\n\nTIMER notify from ISR -5s-\n");
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
    
    typedef struct {        //Prototipo de Estructura de Umbrales de variables
    float umb_hum_suelo;
    float umb_hum_interna_mayor;
    float umb_temp;
    int umb_hora_luz_on;
    int umb_hora_luz_off;
    } stru_umbrales_var_t;
    array_accion_actuadores[pos_actuadores_tamaño]
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
        
        //Si la humedad interna es mayo que la externaa
        if((lecturas.humedad_int - lecturas.humedad_ext) > umbrales_actuadores.umb_hum_interna_mayor){
            actuar_ventilar(true);
        }

        //Si la temperatura es mayor al umbral
        if(lecturas.temperatura > umbrales_actuadores.umb_temp){
            actuar_ventilar(true);
        }else{
            //actuar_ventilar(false);
        }
       
        if(lecturas.humedad_suelo < umbrales_actuadores.umb_hum_suelo){
            actuar_regar(true);
        }else{
            //actuar_regar(false);
        }

    }
    
    /* -----Cambio de estados de actuadores en base a Array array_accion_actuadores
    gpio_set_level(PIN_VENTILAR, array_accion_actuadores[pos_array_VENTILAR]);
    gpio_set_level(PIN_ILUMINAR, array_accion_actuadores[pos_array_ILUMINAR]);
    gpio_set_level(PIN_REGAR, array_accion_actuadores[pos_array_REGAR]);
    gpio_set_level(PIN_FERTILIZAR, array_accion_actuadores[pos_array_FERTILIZAR]);
    */
    
}


void tiempo_sistema(){

    time(&hora_sistema); //Captura de hora POSIX del sistema
    localtime_r(&hora_sistema, &timeinfo);
    int sistema_segundos = timeinfo.tm_sec; //Tiempo del sistema en segundos
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo); //print tiempo  a cadena de texto 
    ESP_LOGI(TAG, "Hora Sistema: %s   -SECS- :  %d", strftime_buf, sistema_segundos);


    if(flag_tiempo_sync){
        time_t hora_sistema_ajustado_col = hora_sistema - (5 * 3600);
        gmtime_r(&hora_sistema_ajustado_col, &tiempo_ajustado);
        strftime(buf_ajustado, sizeof(buf_ajustado), "%c", &tiempo_ajustado);
        ESP_LOGE(TAG, "Hora Colombia (ajustada manual): %s", buf_ajustado);
    }
    


}

void sensores(void){
    esp_err_t ret = ESP_OK;

    //Reset de estructura con valores leídos
    lecturas.humedad_ext = 0;
    lecturas.humedad_int = 0;
    lecturas.temperatura = 0;
    lecturas.presencia_caudal = 0;
    lecturas.humedad_suelo = 0;

    //------------------ LECTURA Caudal en línea de riego ------------------
    lecturas.presencia_caudal=gpio_get_level(PIN_SENSOR_CAUDAL); //Lectura de sensor presencia de caudal

    //------------------ LECTURA sensor humedad suelo ------------------
    humedad_suelo_lectura(&lectura_adc);

    //Verificacion de valor correcot humedad del suelo
    if (lectura_adc >= 5 && lectura_adc <= 100 ){
        lectura_adc = lecturas.humedad_suelo;  
        ESP_LOGW(TAG_SENSORES, "Lectura ADC() - Se asigna valor %i (Pendiente Escala)  ", lectura_adc);    
        //Debería ser un entero dado que ya se habla de un valor de humedad
        //ESP_LOGW(TAG_SENSORES, "Lectura ADC() - Se asigna valor %f (Pendiente Escala)  ", lecturas.humedad_suelo);    
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

    //------------------ LECTURA SHT30 I2C, humedad y Temperatura ------------------
    ret = i2c_read_sht(&lecturas_sht30);
    if(ret == ESP_OK){
        lecturas.humedad_int = lecturas_sht30.humedad;
        lecturas.temperatura = lecturas_sht30.temperatura;
        ESP_LOGI(TAG_SENSORES, "Lectura i2c_read_sht() Ok - Se asignan valores de SHT30 - HUM %f   TEMP %f", lecturas.humedad_int, lecturas.temperatura);
    }else{
        ESP_LOGE(TAG_SENSORES, "Error al leer valores SHT30 "); 
        lecturas.humedad_int = -0.1;
        lecturas.temperatura = -0.1;
    }
     
    ESP_LOGW(TAG, "LECTURA SENSORES ----> Hum_ext:off - Hum_int: %f - Temp: %f - Caudal: %s -Hum_suelo: %i", lecturas.humedad_int, lecturas.temperatura, lecturas.presencia_caudal ? "SI" : "NO", lectura_adc);
    //ESP_LOGI(TAG, "LECTURA SENSORES ----> Hum_ext:off - Hum_int: %f - Temp: %f - Hum-Suelo: %f  Caudal: %s", lecturas.humedad_int, lecturas.temperatura, lecturas.humedad_suelo, lecturas.presencia_caudal ? "true" : "false"  );
    //ESP_LOGI(TAG_ACTUADORES, "ACCION ACTUADORES ----> Iluminar:%s - Ventilar:%s - Regar:%s, Fertilizar:%s", array_accion_actuadores[pos_array_ILUMINAR] ? "ON" : "OFF", array_accion_actuadores[pos_array_VENTILAR] ? "ON" : "OFF", array_accion_actuadores[pos_array_REGAR] ? "ON" : "OFF", array_accion_actuadores[pos_array_FERTILIZAR] ? "ON" : "OFF");
}


esp_err_t humedad_suelo_lectura(int *lectura_adc){
    

    const float Vmin = 0.2;  // Por ejemplo, 1.0 V
    const float Vmax = 3.0;  // Por ejemplo, 2.8 V
    float lec_adc_digital;
    float float_adc_100;

    static const char *TAG_ADC = "ADC_smol";

    if (lectura_adc == NULL) {
        ESP_LOGE(TAG_ADC, "Error: Puntero de lectura ADC es NULL.");
        return ESP_FAIL; // O el código de error apropiado
    }

    //Ojo con el manejo de errores en esta sección y la posterior consulta al ADC.
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_7, &adc_raw[0]));

    lec_adc_digital = (adc_raw[0] * 3.3) / 1024;
    float_adc_100 = ((lec_adc_digital - Vmin) / (Vmax - Vmin)) * 100.0;
    *lectura_adc = (int)float_adc_100;
    ESP_LOGI(TAG_ADC, "--- LECTURA ADC %d_Channel[%d]   Raw Data: %d   Voltage:%i", ADC_UNIT_1 + 1, ADC_CHANNEL_7, adc_raw[0], *lectura_adc );
    
    return ESP_OK;
}

/* Función que activa e inactiva VENTILACIÓN en base a variable;
Se actualiza estado de array actuadores;
Notifica por TaskNotifyGive a tarea de ventilación.
*/
void actuar_ventilar(bool estado_act){
    if(estado_act){ // Activación de pulso de ventilación en tarea ventilar
    array_accion_actuadores[pos_array_VENTILAR]=true;
    xTaskNotifyGive(tareaventilar_manejador); 
    }else{          //Desactivación de pin directamente sin espera de finalización de tarea
        array_accion_actuadores[pos_array_VENTILAR]=false;
        gpio_set_level(PIN_VENTILAR, 0);
    }
    ESP_LOGI(TAG_ACTUADORES, "Ventilar: %s", estado_act ? "ON" : "OFF");
}

/* Función que activa e inactiva RIEGO en base a variable;
 Se actualiza estado de array actuadores;
  Notifica por TaskNotifyGive a tarea de pulso de riego
*/
void actuar_regar(bool estado_act){
    if(estado_act){
        array_accion_actuadores[pos_array_REGAR]=true;
        xTaskNotifyGive(tarearegar_manejador); //Cambio de contexto tarea prioritaria
    }else{
        array_accion_actuadores[pos_array_REGAR]=false;
        gpio_set_level(PIN_REGAR, 0);
    }
    ESP_LOGI(TAG_ACTUADORES, "Regar: %s", estado_act ? "ON" : "OFF");
}


void actuar_iluminar(bool estado_act){
    gpio_set_level(PIN_ILUMINAR, estado_act ? 1 : 0);
    ESP_LOGI(TAG_ACTUADORES, "Iluminar: %s", array_accion_actuadores[pos_array_ILUMINAR] ? "ON" : "OFF");
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


void hardcode_fertilizaciones() {
    time_t now;
    time(&now);
    fertilizaciones[0] = now + 300; // 1 día después
    fertilizaciones[1] = now + 1 * 3600; // 2 días después
    fertilizaciones[2] = now + 2 * 3600; // 3 días después
    

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("fertiliz", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        for (int i = 0; i < 3; i++) {
            char key[16];
            snprintf(key, sizeof(key), "ferti_%d", i);
            nvs_set_i64(nvs_handle, key, fertilizaciones[i]);
        }
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Error abriendo NVS para fertilizaciones");
    }
}

void exec_fertilizaciones() {
    time_t now;
    time(&now);
    for (int i = 0; i < 3; i++) {
        if (fertilizaciones[i] != 0 && fertilizaciones[i] <= now) {
            ESP_LOGI(TAG, "Ejecutando fertilización #%d", i+1);
            //actuar_fertilizar();
            fertilizaciones[i] = 0; // Marca como ejecutada
        }else{
            ESP_LOGI(TAG, "Fertilización(#%d) = 0", i+1);
        }
    }
}

void exec_fertilizaciones2() {
    time_t now;
    time(&now);
    struct tm tm_now, tm_prog;

    localtime_r(&now, &tm_now);

    for (int i = 0; i < 3; i++) {
        if (fertilizaciones[i] != 0) {
            localtime_r(&fertilizaciones[i], &tm_prog);

             // Comprobar si la fertilización está vencida (más de 15 días)
            double dias_diferencia = difftime(now, fertilizaciones[i]) / (60 * 60 * 24);
            if (dias_diferencia > 15.0) {
                ESP_LOGE(TAG, "Fertilización #%d vencida (más de 15 días). Se elimina.", i+1);
                fertilizaciones[i] = 0;
                // Actualiza la NVS
                nvs_handle_t nvs_handle;
                if (nvs_open("fertiliz", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                    char key[16];
                    snprintf(key, sizeof(key), "ferti_%d", i);
                    nvs_set_i64(nvs_handle, key, 0);
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                }
                continue;
            }

            if (tm_now.tm_year == tm_prog.tm_year &&
                tm_now.tm_mon  == tm_prog.tm_mon  &&
                tm_now.tm_mday == tm_prog.tm_mday &&
                tm_now.tm_hour == tm_prog.tm_hour &&
                tm_now.tm_min  == tm_prog.tm_min) {

                ESP_LOGI(TAG, "Ejecutando fertilización #%d", i+1);
                actuar_fertilizar();
                fertilizaciones[i] = 0; // Marca como ejecutada

                // Opcional: Actualiza la NVS
                nvs_handle_t nvs_handle;
                if (nvs_open("fertiliz", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                    char key[16];
                    snprintf(key, sizeof(key), "ferti_%d", i);
                    nvs_set_i64(nvs_handle, key, 0);
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                }
            }
        }
    }
}


void buscar_fertilizaciones_nvs() {
    ESP_LOGI(TAG, "Accediendo a NVS-fertili para consulta de fetilizaciones programadas");
    nvs_handle_t nvs_handle;
    char buf[64];
    struct tm tm_info;
    esp_err_t err = nvs_open("fertiliz", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        for (int i = 0; i < 3; i++) {
            char key[16];
            snprintf(key, sizeof(key), "ferti_%d", i);
            int64_t valor = 0;
            if (nvs_get_i64(nvs_handle, key, &valor) == ESP_OK) {
                fertilizaciones[i] = (time_t)valor;
            } else {
                fertilizaciones[i] = 0;
            }
        }
        nvs_close(nvs_handle);

        for (int i = 0; i < 3; i++) {
            if (fertilizaciones[i] != 0) {
                localtime_r(&fertilizaciones[i], &tm_info);
                strftime(buf, sizeof(buf), "%c", &tm_info);
                printf( "Fertilización #%d programada para: %s \n", i+1, buf);
            }else printf( "Fertilización #%d no agendada; valor = 0 %s \n", i+1, buf);
        }
  
    } else {
        ESP_LOGE(TAG, "Error accediendo a NVS-fertiliz para fertilizaciones; sin valores detectados");
        for (int i = 0; i < 3; i++) {
            fertilizaciones[i] = 0;
        }
    }
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