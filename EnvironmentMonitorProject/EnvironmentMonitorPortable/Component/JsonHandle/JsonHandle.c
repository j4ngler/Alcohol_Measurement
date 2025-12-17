#include "JsonHandle.h"

bool checkChangeStatus = false;


MessageType getMessageType(const char *Json)
{
    if (strstr(Json, "type\":\"ota\""))
        return TYPE_OTA;
    return TYPE_UNKNOWN;
}

bool parseDeviceStatusJson(const char *Json, deviceManager_t *data)
{
    if (!Json || !data)
        return false;

    cJSON *root = cJSON_Parse(Json);
    if (!root)
    {
        printf("❌ JSON parse failed\n");
        return false;
    }

    const cJSON *type = cJSON_GetObjectItem(root, "type");
    const cJSON *device = cJSON_GetObjectItem(root, "device");
    const cJSON *status = cJSON_GetObjectItem(root, "status");

    if (!cJSON_IsString(type) || strcmp(type->valuestring, "device-status") != 0 ||
        !cJSON_IsString(device) || !status)
    {
        printf("❌ Invalid JSON format\n");
        cJSON_Delete(root);
        return false;
    }

    // Reset flag
    checkChangeStatus = false;

    const char *dev = device->valuestring;

    // Chấp nhận status là bool hoặc number (nhiều backend trả về kiểu 0/1 thay vì true/false)
    bool new_status = false;
    if (cJSON_IsBool(status))
        new_status = cJSON_IsTrue(status);
    else if (cJSON_IsNumber(status))
        new_status = status->valueint ? true : false;
    else
    {
        printf("❌ status field is not boolean/number\n");
        cJSON_Delete(root);
        return false;
    }

    // Nhận diện thiết bị
    enum
    {
        DEVICE_UNKNOWN,
        DEVICE_OTA
    } deviceType = DEVICE_UNKNOWN;

    if (strcmp(dev, "OTA") == 0)
        deviceType = DEVICE_OTA;

    switch (deviceType)
    {
    case DEVICE_OTA:
        // if (data->modeActive != (Mode_t)new_status)
        // {
        //     data->modeActive = (Mode_t)new_status;
        //     checkChangeStatus = true;
        // }
        break;
    default:
        printf("❌ Unknown device: %s\n", dev);
        cJSON_Delete(root);
        return false;
    }

    printf("✅ Updated device: %s -> %d\n", dev, new_status);
    cJSON_Delete(root);
    return true;
}



bool parseOTAJson(const char *json, OTAData *data)
{
    if (!json || !data)
    {
        printf("❌ Null parameter\n");
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root)
    {
        printf("❌ Failed to parse JSON\n");
        return false;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *index = cJSON_GetObjectItem(root, "index");
    cJSON *percent = cJSON_GetObjectItem(root, "Percent");
    cJSON *line = cJSON_GetObjectItem(root, "line");

    if (!cJSON_IsString(type) || strcmp(type->valuestring, "ota") != 0 ||
        !cJSON_IsNumber(index) || !cJSON_IsNumber(percent) || !cJSON_IsString(line))
    {
        printf("❌ Missing or invalid ota fields\n");
        cJSON_Delete(root);
        return false;
    }

    data->index = index->valueint;
    data->percent = percent->valueint;
    size_t len = strlen(line->valuestring);
    data->line = malloc(len + 1);
    if (data->line)
    {
        strcpy(data->line, line->valuestring); // ĐÃ ĐỦ AN TOÀN, luôn null-terminated
    }
    else
    {
        printf("❌ malloc failed for OTA line\n");
        cJSON_Delete(root);
        return false;
    }

    //printf("✅ Parsed OTA percent=%d, line=%s\n",data->percent, data->line);

    cJSON_Delete(root);
    return true;
}
