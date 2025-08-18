#include "miwifi.h"
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi_types.h"

#include "main_cultivo.h"


// FreeRTOS grupo de eventos
static EventGroupHandle_t wifi_grupo_eventos;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_smol";
static uint8_t intentos_num = 0; // Contador de reintentos de conexión



//Función callback que procesa los eventos de WiFi
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (intentos_num < ESP_INTENTOS_CONNECT) {
            esp_wifi_connect();
            intentos_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_grupo_eventos, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "OK IP ASIGNADA:" IPSTR, IP2STR(&event->ip_info.ip));
        intentos_num = 0;
        xEventGroupSetBits(wifi_grupo_eventos, WIFI_CONNECTED_BIT);
    }
}



//Función que inicializa WiFi en modo estación

void wifi_init_sta(void) {

    // ---------- inicialización de WiFi ----------
    ESP_LOGI(TAG, "----- Inicializando WiFi en modo STATION");
    
    // Inicialización de NVS se hace en config_inicial()

    // -------  Inicialización funciones red WiFi
    wifi_grupo_eventos = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());          //Inicia capa TCP/IP
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();  //Inicia instancia netif para estación

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); //Configuración general por defecto de wifi
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    // Configuración de la conexión WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MIWIFI_SSID,
            .password = MIWIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            //.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            //.sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            //.sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        }
    };
    

 //-----------  configuración SNTP
    ESP_LOGI(TAG, "----- Configurando SNTP");
   //esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("time.windows.com", "pool.ntp.org" ));
   
   /*
   //Configuración para aceptar servidor NTP ofrecido por DHCP 
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(0, {} );
    config.start = false;                       // start the SNTP service explicitly
    config.server_from_dhcp = true;             // accept the NTP offer from the DHCP server
    config.renew_servers_after_new_IP = true;   // let esp-netif update configured SNTP server(s) after receiving DHCP lease
    config.index_of_first_server = 1;           // updates from server num 1, leaving server 0 (from DHCP) intact
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;*/

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("0.south-america.pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));

    // Configuración del modo WiFi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    // Iniciar WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "----- WiFi iniciado en modo STATION");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(wifi_grupo_eventos,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGW(TAG, "----- connected to ap SSID:%s password:%s", MIWIFI_SSID, MIWIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "----- Failed to connect to SSID:%s, password:%s", MIWIFI_SSID, MIWIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

   
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG, "Error al sincronizar SNTP, timeout 10s");
    }else {
        ESP_LOGW(TAG, "SNTP sincronizado correctamente");
        flag_tiempo_sync = true; // Marca que el tiempo ha sido sincronizado
    }

    setenv("TZ", "America/Bogota", 1); // Set zona horaria
    tzset();
}


void sntp_tiempo_sincro(void)
{
    ESP_LOGW(TAG, "BLOQUEO hasta sincronizar SNTP - 10s timeout");
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG, "Error al sincronizar SNTP, timeout 10s");
    }else {
        ESP_LOGW(TAG, "SNTP sincronizado correctamente");
        flag_tiempo_sync = true; // Marca que el tiempo ha sido sincronizado
    }
}

/*
void wifi_main(void)
{
    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
         //If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
        //and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. 
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    wifi_init_sta();
}
*/