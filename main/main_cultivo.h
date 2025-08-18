#ifndef MAIN_CULTIVO_H
#define MAIN_CULTIVO_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//Definicion Pines
#define PIN_VENTILAR 25 
#define PIN_ILUMINAR 26
#define PIN_REGAR 27  
#define PIN_FERTILIZAR 32 
#define PIN_SENSOR_CAUDAL 14


//Definición de límites

#define LIM_HUM_AMB_MAX 100.0
#define LIM_HUM_AMB_MIN 30.0
#define LIM_TEMP_MAX 40.0
#define LIM_TEMP_MIN 10.0 
#define LIM_HUM_SUELO_MAX 0
#define LIM_HUM_SUELO_MIN 0

//Defines de tiempo de acción
#define TIEMPO_VENTILAR_ON 20  //Define de segundos ventilacion, podria ser variable
#define TIEMPO_REGAR_ON 22  //Define de segundos riego, podria ser variable

//Variables globales

extern bool flag_error_i2c; //Variable para detección de erro en comunicación I2C con el sensor SHT30
extern bool flag_tiempo_sync; //Flag activa una vez se haya sincronizado el tiempo con SNTP

void timer_funcion(TimerHandle_t);

//Funciones de actuadores
void actuar_iluminar(bool); //Activador Iluminación
void actuar_ventilar(bool); //Activador Ventilación
void actuar_regar(bool);    //Funcion actuadora bomba de riego Pin27
void actuar_fertilizar(); //Funcion actuadora bomba fertilizante Pin32
void chipinfo(void); //Información del chip

void config_inicial(void);
void actuadores(void); 

void tiempo_sistema();//Cálculo de hora del sistema

esp_err_t humedad_suelo_lectura(int *hum_suelo); //Lectura de la humedad del suelo
void set_umbrales_actuadores(void); //Set de umbrales param el inicio y tras solicitud del usuario por GUI
esp_err_t  adc_init(void); // función para iniacializar el ADC

void sensores(void);



typedef struct {        //Prototipo de Estructura de lecturas de sensores
    float temperatura;
    float humedad_int;
    float humedad_ext;
    int humedad_suelo;
    bool presencia_caudal;
} stru_lec_sensores_t;

typedef struct {        //Prototipo de Estructura de Umbrales de variables
    float umb_hum_suelo;
    float umb_hum_interna_mayor;
    float umb_temp;
    int umb_hora_luz_on;
    int umb_hora_luz_off;
} stru_umbrales_var_t;


typedef enum {
    pos_array_VENTILAR = 0,
    pos_array_ILUMINAR,
    pos_array_REGAR,
    pos_array_FERTILIZAR,
    pos_actuadores_tamaño, // Esto nos da el tamaño total del array
} actuadores_index;






#endif 