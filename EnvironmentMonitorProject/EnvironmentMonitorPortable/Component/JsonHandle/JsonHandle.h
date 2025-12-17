#ifndef JSON_HANDLE_H
#define JSON_HANDLE_H

#include <stdint.h>
#include "DataManager.h"

#include "LogicHandle.h"

extern bool checkChangeStatus;

MessageType getMessageType(const char* Json);
bool parseOTAJson(const char *json, OTAData *data);
bool parseDeviceStatusJson(const char *Json, deviceManager_t *data);
#endif // JSON_HANDLE_H
