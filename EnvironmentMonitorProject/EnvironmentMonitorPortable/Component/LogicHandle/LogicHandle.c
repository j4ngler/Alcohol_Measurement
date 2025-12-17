#include "LogicHandle.h"

uint8_t charArray_to_uint8(char *arr, uint8_t length)
{
    uint8_t result = 0;
    for (uint8_t i = 0; i < length; i++)
    {
        if (arr[i] >= '0' && arr[i] <= '9')
        {
            result = result * 10 + (arr[i] - '0');
        }
        else
        {
            // Ký t? không h?p l?, tr? v? 0 ho?c x? lý l?i tùy ý b?n
            return 0;
        }
    }
    return result;
}
uint8_t hexDigitToValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return 0;
}
bool containsASCII(const char *dataInput, const char *chars)
{
    if (!dataInput || !chars)
        return false;

    while (*dataInput)
    {
        const char *p = chars;
        while (*p)
        {
            if (*dataInput == *p)
                return true;
            p++;
        }
        dataInput++;
    }
    return false;
}

void convertCharToHex(uint8_t *arrayOutPut, const char *arrayInPut, uint8_t length)
{
    // Gi? s? length là s? ký t? trong arrayInPut và ph?i là s? ch?n
    uint8_t idx = 0;
    if (arrayInPut[0] == ':')
        idx = 1;
    uint8_t outputIndex = 0;

    for (idx = 0; idx < length; idx += 2)
    {
        char high = arrayInPut[idx];
        char low = arrayInPut[idx + 1];

        uint8_t byteVal = (hexDigitToValue(high) << 4) | hexDigitToValue(low);
        arrayOutPut[outputIndex++] = byteVal;
    }
}


void readSensorData(dataManager_t *dataManager)
{
    // // Read BME280 sensor data (Temperature, Humidity, Pressure)
    // // This function will be implemented to read directly from BME280 sensor
    // // For now, we'll set placeholder values
    // dataManager->Temperature_Value = 25.5;
    // dataManager->Humidity_Value = 60.2;
    // dataManager->Pressure_Value = 1013.2;
    
    // // Read PMS7003 sensor data (PM1.0, PM2.5, PM10)
    // // This function will be implemented to read directly from PMS7003 sensor
    // // For now, we'll set placeholder values
    // dataManager->PM1_0_Value = 15;
    // dataManager->PM2_5_Value = 25;
    // dataManager->PM10_Value = 30;
    
    // // Set device status (no longer needed for device control)
    // strcpy(dataManager->statusDevice, "000000");
}
