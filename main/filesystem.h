#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

wl_handle_t mountFATFS(const char* partition_label, const char* mount_point);
void unmountFATFS(const char* mount_point, wl_handle_t wl_handle);
esp_err_t mountSDCARD(const char* mount_point, sdmmc_card_t** card);
void unmountSDCARD(const char* mount_point, sdmmc_card_t* card);
void log_storage_info();

#endif /* FILESYSTEM_H */