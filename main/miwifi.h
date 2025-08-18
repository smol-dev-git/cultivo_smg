#ifndef MIWIFI_H
#define MIWIFI_H



#define ESP_INTENTOS_CONNECT  10
#define MIWIFI_SSID "Herrera Molina"
#define MIWIFI_PASS  "1112787000"


// Declara la función de inicialización para que otros archivos la puedan ver
void wifi_init_sta(void);
void sntp_tiempo_sincro(void);

#endif // MIWIFI_H