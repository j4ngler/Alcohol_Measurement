#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define KEY_OTA 0xAB

enum
{

  OTA_MODE_COMMAND = 0xFE,
  NORMAL_MODE_COMMAND = 0xFF,
};

#define END_LINE_DATA 0xEE

typedef struct
{
  char *type;
  uint16_t index;
  uint8_t percent;
  uint16_t length;
  char *line;
} OTAData;

typedef struct
{
  uint8_t Flag_OTA;

  uint8_t Flag_Request_OTA;

} Flag_Signal_Control_t;

typedef struct
{
  float Temperature_Value;
  float Humidity_Value;
  float Pressure_Value;
  float PM1_0_Value;
  float PM2_5_Value;
  float PM10_Value;
  char statusDevice[6];
  char Timestamp[20];


  char NameFileSD[40];
  char FormatDataToSD[70];

  
  char CurfirmwareVersion[10];
  char NewfirmwareVersion[10];
  char firmwareFileName[100];

  char firmwareUrl[128];

  bool isNewFirmware;

  char firmwareVersion[10];

} dataManager_t;

typedef enum
{
  DEVICE_UNKNOWN,
  DEVICE_OTA
} DeviceType;

typedef enum
{
  NONE_DEVICE
} Device_t;

typedef enum
{
  TYPE_UNKNOWN,
  TYPE_OTA,
  TYPE_DEVICE_STATUS
} MessageType;


typedef struct
{

  bool wifiStatus;
  bool ServerStatus;

  char *type;

} deviceManager_t;

typedef enum
{

  SCREEN_APP = 1,
  SCREEN_BOOTLOADER

} Current_Screen_t;

typedef struct
{
  char *JsonRegisterCommand;
  char *JsonDataCommand;
} JsonCommand_t;

// Dữ liệu đầu vào dạng: "Txx.xHxx.xPxx.xxM1xxxM2xxxM3xxxSxxxxx"

#endif // DATA_MANAGER_H