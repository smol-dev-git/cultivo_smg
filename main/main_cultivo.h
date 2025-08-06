#ifndef MAIN_CULTIVO_H
#define MAIN_CULTIVO_H

//Definicion Pines
#define PIN_VENTILAR 25 
#define PIN_ILUMINAR 26
#define PIN_REGAR 27  
#define PIN_FERTILIZAR 32 
#define PIN_SENSOR_RIEGO 14


//Definición de límites

#define LIM_HUM_AMB_MAX 100.0
#define LIM_HUM_AMB_MIN 30.0
#define LIM_TEMP_MAX 40.0
#define LIM_TEMP_MIN 10.0 
#define LIM_HUM_SUELO_MAX 0
#define LIM_HUM_SUELO_MIN 0


//Funciones de actuadores
void actuar_iluminar(bool); //Activador Iluminación
void actuar_ventilar(bool); //Activador Ventilación
void actuar_regar(bool);    //Activador Bomba de Riego
void actuar_fertilizar(bool); //Activador bomba fertilizante
void chipinfo(void); //Información del chip

void config_inicial(void);
void actuadores(void); 
bool sensor_caudal_riego(void);

esp_err_t humedad_suelo_lectura(float *hum_suelo); //Lectura de la humedad del suelo
void set_umbrales_actuadores(void); //Set de umbrales param el inicio y tras solicitud del usuario por GUI
esp_err_t  adc_init(void); // función para iniacializar el ADC



typedef struct {        //Prototipo de Estructura de lecturas de sensores
    float temperatura;
    float humedad_int;
    float humedad_ext;
    int humedad_suelo;
    bool presencia_caudal;
} stru_lec_sensores_t;

typedef struct {        //Prototipo de Estructura de lecturas de sensores
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




stru_lec_sensores_t  sensores(void);

#endif 