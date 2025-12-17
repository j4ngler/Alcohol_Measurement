#ifndef __SD_CARD_H__
#define __SD_CARD_H__
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"


#define MOUNT_POINT "/sdcard"



esp_err_t initSDCard(void);
esp_err_t writeSD_Card(const char *filename, const char *data);
esp_err_t writeFinalFileSD_Card(const char *filename, const char *data);
esp_err_t readSD_Card(const char *filename, char *out_buf, size_t buf_size);
esp_err_t renameSD_Card(const char *oldname, const char *newname);



#endif // __SD_CARD_H__