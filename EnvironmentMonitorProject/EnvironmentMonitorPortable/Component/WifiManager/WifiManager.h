#ifndef __WIFI_MANAGER_H__
#define __WIFI_MANAGER_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_http_server.h"

#include "cJSON.h"
#include "DataManager.h"

// WiFi event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_AP_MODE_BIT BIT2

// WiFi configuration
#define AP_SSID CONFIG_AP_SSID
#define AP_PASSWORD CONFIG_AP_PASSWORD
#define AP_CHANNEL CONFIG_AP_CHANNEL
#define AP_MAX_CONN 4
#define DNS_PORT 53
#define DNS_MAX_LEN 512



void wifi_init_sta(void);
void wifi_connect_task(void *pvParameters);
void wifi_manager_task(void *pvParameters);
bool is_wifi_connected(void);


#endif // __WIFI_MANAGER_H__