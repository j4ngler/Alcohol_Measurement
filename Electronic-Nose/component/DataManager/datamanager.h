#ifndef __DATAMANAGER_H__
#define __DATAMANAGER_H__

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <inttypes.h>

#define ERROR_VALUE UINT32_MAX

struct dataSensor_st
{
    int timeStamp;
    float temperature;
    float humidity;
    float pressure;
    int16_t ADC_Value[4];
};

const char dataSensor_templateSaveToSDCard[] = "%d,%.2f,%.2f,%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "\n";

#endif