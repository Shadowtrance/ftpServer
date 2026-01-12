#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "filesystem.h"
#include "ftpServer.h"  // For VFS_NATIVE_INTERNAL_MP and VFS_NATIVE_EXTERNAL_MP

static const char *TAG = "FILESYSTEM";

// Configuration constants
#define FATFS_MAX_FILES 4
#define SD_ALLOCATION_UNIT (16 * 1024)
#define SD_MAX_FREQ_KHZ 20000
#define SPI_MAX_TRANSFER_SZ 4000

wl_handle_t mountFATFS(const char* partition_label, const char* mount_point) {
    ESP_LOGI(TAG, "Initializing FATFS on Builtin SPI Flash Memory");
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = FATFS_MAX_FILES,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };
    wl_handle_t s_wl_handle;
    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(mount_point, partition_label, &mount_config, &s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
        return -1;
    }

    uint64_t total=0, free=0;
    ret = esp_vfs_fat_info(mount_point, &total, &free);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get FATFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %llu, free: %llu", total, free);
    }
    ESP_LOGI(TAG, "Mount FATFS on %s", mount_point);
    ESP_LOGI(TAG, "s_wl_handle=%" PRIi32, s_wl_handle);
    return s_wl_handle;
}

esp_err_t mountSDCARD(const char* mount_point, sdmmc_card_t** out_card) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = FATFS_MAX_FILES,
        .allocation_unit_size = SD_ALLOCATION_UNIT,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    ESP_LOGI(TAG, "Initializing FATFS on SPI SDCARD");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SD_MAX_FREQ_KHZ;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SDCARD_MOSI_GPIO,
        .miso_io_num = CONFIG_SDCARD_MISO_GPIO,
        .sclk_io_num = CONFIG_SDCARD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .data_io_default_level = 0,
        .max_transfer_sz = SPI_MAX_TRANSFER_SZ,
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }
    sdspi_device_config_t device_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    device_config.host_id = (spi_host_device_t)host.slot;
    device_config.gpio_cs = (gpio_num_t)CONFIG_SDCARD_CS_GPIO;

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &device_config, &mount_config, out_card);
    ESP_LOGI(TAG, "esp_vfs_fat_sdspi_mount=%d", ret);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        spi_bus_free((spi_host_device_t)host.slot);
        return ret;
    }

    sdmmc_card_print_info(stdout, *out_card);
    ESP_LOGI(TAG, "Mounted SD card on %s", mount_point);
    return ret;
}

void unmountFATFS(const char* mount_point, wl_handle_t wl_handle) {
    if (wl_handle < 0 || !mount_point) {
        ESP_LOGW(TAG, "Invalid FATFS unmount parameters");
        return;
    }
    esp_err_t ret = esp_vfs_fat_spiflash_unmount_rw_wl(mount_point, wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount FATFS (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Unmounted FATFS from %s", mount_point);
    }
}

void unmountSDCARD(const char* mount_point, sdmmc_card_t* card) {
    if (!mount_point || !card) {
        ESP_LOGW(TAG, "Invalid SD card unmount parameters");
        return;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card (%s)", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Unmounted SD card from %s", mount_point);
    // Free SPI bus
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_free((spi_host_device_t)host.slot);
}

void log_storage_info() {
    uint64_t total, free;
    if (esp_vfs_fat_info(VFS_NATIVE_INTERNAL_MP, &total, &free) == ESP_OK) {
        ESP_LOGI(TAG, "Data storage: Total %.2f MB, Free %.2f MB", (double)total / (1024 * 1024), (double)free / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "Failed to get data storage info");
    }
    if (esp_vfs_fat_info(VFS_NATIVE_EXTERNAL_MP, &total, &free) == ESP_OK) {
        ESP_LOGI(TAG, "SD card storage: Total %.2f MB, Free %.2f MB", (double)total / (1024 * 1024), (double)free / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "Failed to get SD card storage info");
    }
}
