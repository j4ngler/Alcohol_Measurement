#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H
#include "SD_Card.h"
#include "DS3231Time.h"
#include "DataManager.h"
#include "WSHandle.h"
#include "WifiManager.h"
#include "ADS111x.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "sntp_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/semphr.h"
#include "WSHandle.h"


#define TAG_TASK_MANAGER "TaskManager"

void AllTasksRun(void);
void suspendAllTask(void);
void resumeAllTask(void);
void TaskManagerInit(void);


#endif // TASK_MANAGER_H