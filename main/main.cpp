#include "ftpServer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lvgl.h"
#include "displayConfig.h"
#include "ftpUiScreen.h"
#include "filesystem.h"

static const char* TAG = "[MAIN]";

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Global handles
static FtpServer::Server* ftpServer = nullptr;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool screen_created = false;
static bool sdcard_was_present = false;
sdmmc_card_t* sdcard = nullptr;
wl_handle_t wl_handle;

#define CHECKPOINT(msg) \
    do { \
        ESP_LOGI(TAG, "%s | Heap: %lu", msg, esp_get_free_heap_size()); \
        vTaskDelay(pdMS_TO_TICKS(50)); \
    } while(0)

// Safe logging - only log to screen if it exists
static void safe_log(const char* message) {
    ESP_LOGI(TAG, "%s", message);
    if (screen_created) {
        lv_lock();
        addLog(message);
        lv_unlock();
    }
}

// Time sync notification callback
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Time synchronized");
    safe_log("Time synchronized");
}

// Initialize SNTP
static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    
    // Set timezone (adjust for your location)
    setenv("TZ", CONFIG_TIMEZONE, 1);
    tzset();
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi started, connecting...");
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi... (%d/10)", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to WiFi");
            safe_log("WiFi connection failed");
        }
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        // Log IP to screen
        char ip_msg[64];
        snprintf(ip_msg, sizeof(ip_msg), "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        safe_log(ip_msg);

        snprintf(ip_msg, sizeof(ip_msg), IPSTR, IP2STR(&event->ip_info.ip));
        if (screen_created) {
            lv_lock();
            update_ip_label(ip_msg);
            lv_unlock();
        }
        
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi with event handlers
static void wifi_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                &wifi_event_handler, nullptr));
    
    // Configure WiFi
    wifi_config_t wifi_config = {};
    strlcpy((char*)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization complete");
}

static void ftp_control_handler(bool start) {
    if (!ftpServer) {
        reset_ftp_operation_flag();
        return;
    }
    
    if (start) {
        ESP_LOGI(TAG, "User requested FTP start");
        ftpServer->start();
        
        // Wait a moment then check if it started
        vTaskDelay(pdMS_TO_TICKS(500));
        
        if (ftpServer->isEnabled()) {
            lv_lock();
            addLog("#00ff00 [OK] FTP server started#");
            update_status("Ready");
            lv_unlock();
            
            char ftp_msg[64];
            snprintf(ftp_msg, sizeof(ftp_msg), "User: %s | Port: 21", CONFIG_FTP_USER);
            lv_lock();
            addLog(ftp_msg);
            lv_unlock();
        } else {
            lv_lock();
            addLog("#ff0000 [!!] FTP failed to start#");
            update_status("Error");
            set_server_switch_state(false);  // Turn switch back off
            lv_unlock();
        }
        
    } else {
        ESP_LOGI(TAG, "User requested FTP stop");
        ftpServer->stop();
        lv_lock();
        addLog("#00ff00 [OK] FTP server stopped#");
        update_status("Stopped");
        lv_unlock();
    }

    reset_ftp_operation_flag();
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== FTP Application Starting ===");
    
    // ========================================
    // Phase 1: Initialize Core Systems
    // ========================================
    CHECKPOINT("Starting Phase 1");
    ESP_LOGI(TAG, "Phase 1: Core initialization");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
    CHECKPOINT("Phase 1 complete");
    
    // ========================================
    // Phase 2: Initialize Display & LVGL
    // ========================================
    CHECKPOINT("Starting Phase 2");
    ESP_LOGI(TAG, "Phase 2: Display initialization");
    
    suntonEsp32s3LcdInit();
    
    ESP_LOGI(TAG, "Display initialized");
    CHECKPOINT("Phase 2 complete");
    
    // ========================================
    // Phase 3: Create UI Screen (BEFORE addLog calls)
    // ========================================
    CHECKPOINT("Starting Phase 3");
    ESP_LOGI(TAG, "Phase 3: Creating UI");
    
    create_screen_ftp();
    screen_created = true;  // Now safe to call addLog()

    register_ftp_control_callback(ftp_control_handler);

    start_time_update_timer();
    
    ESP_LOGI(TAG, "FTP screen created");
    lv_lock();
    addLog("=== System Starting ===");
    lv_unlock();
    CHECKPOINT("Phase 3 complete");

    // ========================================
    // Phase 4: Initialize Storage
    // ========================================
    CHECKPOINT("Starting Phase 4");
    ESP_LOGI(TAG, "Phase 4: Storage initialization");
    lv_lock();
    addLog("Initializing storage...");
    lv_unlock();
    
    bool has_storage = false;
    // Initialize internal flash storage (/data) and SD card (/sdcard)
    wl_handle = mountFATFS("data", "/data");
    if (wl_handle >= 0) has_storage = true;
    if (mountSDCARD("/sdcard", &sdcard) == ESP_OK) has_storage = true;

    if (!has_storage) {
        ESP_LOGE(TAG, "No storage available");
        lv_lock();
        addLog("#ff0000 [!!] No storage - cannot start FTP#");
        lv_unlock();
        return;
    }
    
    ESP_LOGI(TAG, "Storage initialized");
    if (has_storage) {
        log_storage_info();
    }
    CHECKPOINT("Phase 4 complete");

    // ========================================
    // Phase 5: Initialize WiFi
    // ========================================
    CHECKPOINT("Starting Phase 5");
    ESP_LOGI(TAG, "Phase 5: WiFi initialization");
    lv_lock();
    addLog("Connecting to WiFi...");
    lv_unlock();
    
    wifi_init();
    
    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            pdMS_TO_TICKS(30000));  // 30 second timeout
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", CONFIG_WIFI_SSID);
        lv_lock();
        addLog("#00ff00 [OK] WiFi connected#");
        lv_unlock();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi SSID:%s", CONFIG_WIFI_SSID);
        lv_lock();
        addLog("#ff0000 [!!] WiFi failed#");
        lv_unlock();
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        lv_lock();
        addLog("#ff0000 [!!] WiFi timeout#");
        lv_unlock();
    }
    CHECKPOINT("Phase 5 complete");
    
    // ========================================
    // Phase 6: Initialize Time Sync
    // ========================================
    CHECKPOINT("Starting Phase 6");
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Phase 6: Time synchronization");
        lv_lock();
        addLog("Syncing time...");
        lv_unlock();
        initialize_sntp();
    }
    CHECKPOINT("Phase 6 complete");

    // ========================================
    // Phase 7: Start FTP Server
    // ========================================
    CHECKPOINT("Starting Phase 7");
    ESP_LOGI(TAG, "Phase 7: Creating FTP server instance");

    ftpServer = new FtpServer::Server();

    if (!ftpServer) {
        ESP_LOGE(TAG, "Failed to allocate FTP server!");
        lv_lock();
        addLog("#ff0000 [!!] ERROR: FTP alloc failed#");
        lv_unlock();
        return;
    }

    ftpServer->register_screen_log_callback([](const char* msg) {
        lv_lock();
        addLog(msg);
        lv_unlock();
    });

    ESP_LOGI(TAG, "FTP server ready (stopped)");
    lv_lock();
    addLog("#00ff00 [OK] FTP server ready#");
    update_status("Stopped");
    lv_unlock();
    CHECKPOINT("Phase 7 complete");

    // ========================================
    // Phase 8: Main Monitoring Loop
    // ========================================
    
    ESP_LOGI(TAG, "=== System Ready - Entering Main Loop ===");
    lv_lock();
    addLog("#00ff00 [OK] === System Ready ===#");
    lv_unlock();
    
    int last_ftp_state = -1;
    uint32_t iteration = 0;
    
    while (1) {
        int ftp_state = ftpServer->getState();

        // Log FTP state changes
        if (ftp_state != last_ftp_state) {
            switch (ftp_state & 0xFF) {
                case FtpServer::Server::E_FTP_STE_DISABLED:
                    ESP_LOGI(TAG, "FTP: Disabled");
                    lv_lock();
                    addLog("#ffaa00 [--] FTP: Disabled#");
                    update_status("Disabled");
                    lv_unlock();
                    break;
                    
                case FtpServer::Server::E_FTP_STE_READY:
                    ESP_LOGI(TAG, "FTP: Ready");
                    lv_lock();
                    addLog("#00ff00 [OK] FTP: Ready#");
                    update_status("Ready");
                    lv_unlock();
                    break;
                    
                case FtpServer::Server::E_FTP_STE_CONNECTED:
                    ESP_LOGI(TAG, "FTP: Client Connected");
                    lv_lock();
                    addLog("#00ff00 [**] FTP Client Connected#");
                    update_status("Client Connected");
                    lv_unlock();
                    break;
                    
                case FtpServer::Server::E_FTP_STE_CONTINUE_FILE_TX:
                    ESP_LOGI(TAG, "FTP: Sending file");
                    lv_lock();
                    addLog("#00ffff [>>] Sending file...#");
                    update_status("Sending File");
                    lv_unlock();
                    break;
                    
                case FtpServer::Server::E_FTP_STE_CONTINUE_FILE_RX:
                    ESP_LOGI(TAG, "FTP: Receiving file");
                    lv_lock();
                    addLog("#00ffff [<<] Receiving file...#");
                    update_status("Receiving File");
                    lv_unlock();
                    break;
                    
                case FtpServer::Server::E_FTP_STE_END_TRANSFER:
                    ESP_LOGI(TAG, "FTP: Transfer complete");
                    lv_lock();
                    addLog("#00ff00 [OK] Transfer complete#");
                    update_status("Ready");
                    lv_unlock();
                    break;
            }
            last_ftp_state = ftp_state;
        }

        if (iteration % 10 == 0 && sdcard) {
            esp_err_t ret = sdmmc_get_status(sdcard);

            if (ret != ESP_OK && sdcard_was_present) {
                ESP_LOGW(TAG, "SD Card removed or inaccessible!");
                lv_lock();
                addLog("#ff8800 [!!] SD Card removed!#");
                lv_unlock();
                sdcard_was_present = false;
            } else if (ret == ESP_OK && !sdcard_was_present) {
                ESP_LOGI(TAG, "SD Card is accessible");
                lv_lock();
                addLog("#00ff00 [OK] SD Card accessible#");
                lv_unlock();
                sdcard_was_present = true;
            }
        }

        iteration++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Cleanup (never reached)
    ESP_LOGI(TAG, "Shutting down...");
    ftpServer->stop();
    delete ftpServer;
    ftpServer = nullptr;
}
