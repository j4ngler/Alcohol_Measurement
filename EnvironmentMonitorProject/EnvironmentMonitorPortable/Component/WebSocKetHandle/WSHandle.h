#ifndef WS_HANDLE_H
#define WS_HANDLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_websocket_client.h"
#include "DataManager.h"
#include "wifiManager.h"
#include "FOTAManager.h"
#include "JsonHandle.h"

void SendSignalRegister(void);
void DataToSever(dataManager_t *Datamanager);
void WebSocket_Handler(void *pvParameter);
#endif // WS_HANDLE_H