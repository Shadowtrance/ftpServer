// Improved FTP Screen for 800x480 display - LVGL 9.4 Compatible
// With FTP server ON/OFF toggle switch

#define LV_USE_PRIVATE_API 1

#include "ftpUiScreen.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <mutex>
#include <atomic>
#include "spinner_img.h"

static const char* TAG = "[UI]";

// Screen dimensions
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

// UI Layout constants - Responsive based on screen size
#if SCREEN_WIDTH >= 800
    // Large screen (800x480 and above)
    #define HEADER_HEIGHT_PX 60
    #define INFO_BAR_HEIGHT_PX 50
    #define LOG_PANEL_SPACING 5
    #define LOG_PANEL_TITLE_HEIGHT 30
    #define LOG_PANEL_PADDING 10
    #define LOG_PANEL_HEIGHT (SCREEN_HEIGHT - HEADER_HEIGHT_PX - INFO_BAR_HEIGHT_PX - LOG_PANEL_SPACING)
    #define LOG_TEXTAREA_HEIGHT_PX (LOG_PANEL_HEIGHT - LOG_PANEL_TITLE_HEIGHT - LOG_PANEL_PADDING)
    #define TITLE_FONT &lv_font_montserrat_28
    #define STATUS_FONT &lv_font_montserrat_18
    #define INFO_FONT &lv_font_montserrat_16
    #define LOG_FONT &lv_font_montserrat_14
    #define PORT_FONT &lv_font_montserrat_14
    #define BUTTON_WIDTH 120
    #define BUTTON_HEIGHT 40
    #define SWITCH_WIDTH 80
    #define SWITCH_HEIGHT 40
#elif SCREEN_WIDTH >= 320
    // Small screen (320x240)
    #define HEADER_HEIGHT_PX 40
    #define INFO_BAR_HEIGHT_PX 35
    #define LOG_PANEL_SPACING 5
    #define LOG_PANEL_TITLE_HEIGHT 20
    #define LOG_PANEL_PADDING 5
    #define LOG_PANEL_HEIGHT (SCREEN_HEIGHT - HEADER_HEIGHT_PX - INFO_BAR_HEIGHT_PX - LOG_PANEL_SPACING)
    #define LOG_TEXTAREA_HEIGHT_PX (LOG_PANEL_HEIGHT - LOG_PANEL_TITLE_HEIGHT - LOG_PANEL_PADDING)
    #define TITLE_FONT &lv_font_montserrat_20
    #define STATUS_FONT &lv_font_montserrat_16
    #define INFO_FONT &lv_font_montserrat_12
    #define LOG_FONT &lv_font_montserrat_12
    #define PORT_FONT &lv_font_montserrat_10
    #define BUTTON_WIDTH 60
    #define BUTTON_HEIGHT 25
    #define SWITCH_WIDTH 50
    #define SWITCH_HEIGHT 25
#else
    // Tiny screen fallback
    #define HEADER_HEIGHT_PX 35
    #define INFO_BAR_HEIGHT_PX 30
    #define LOG_PANEL_SPACING 3
    #define LOG_PANEL_TITLE_HEIGHT 15
    #define LOG_PANEL_PADDING 5
    #define LOG_PANEL_HEIGHT (SCREEN_HEIGHT - HEADER_HEIGHT_PX - INFO_BAR_HEIGHT_PX - LOG_PANEL_SPACING)
    #define LOG_TEXTAREA_HEIGHT_PX (LOG_PANEL_HEIGHT - LOG_PANEL_TITLE_HEIGHT - LOG_PANEL_PADDING)
    #define TITLE_FONT &lv_font_montserrat_16
    #define STATUS_FONT &lv_font_montserrat_14
    #define INFO_FONT &lv_font_montserrat_10
    #define LOG_FONT &lv_font_montserrat_10
    #define PORT_FONT &lv_font_montserrat_10
    #define BUTTON_WIDTH 50
    #define BUTTON_HEIGHT 20
    #define SWITCH_WIDTH 40
    #define SWITCH_HEIGHT 20
#endif

#define SPINNER_ROTATION_DEGREES 3600
#define SPINNER_ANIMATION_DURATION_MS 800

// UI Colors
#define UI_COLOR_HEADER 0x2d2d2d
#define UI_COLOR_TITLE_TEXT 0x00ff00
#define UI_COLOR_STATUS_TEXT 0xffaa00
#define UI_COLOR_BG_DARK 0x0d0d0d

// UI objects
static lv_obj_t* screen_ftp = nullptr;
static lv_obj_t* log_textarea = nullptr;
static lv_obj_t* status_label = nullptr;
static lv_obj_t* ip_label = nullptr;
static lv_obj_t* time_label = nullptr;
static lv_obj_t* port_label = nullptr;
static lv_obj_t* server_switch = nullptr;
static lv_obj_t* spinner = nullptr;
static lv_obj_t* clear_log_btn = nullptr;

// Timer handle for time updates
static lv_timer_t* time_update_timer = nullptr;

static std::atomic<bool> ftp_operation_in_progress{false};

// Log buffer management
#define MAX_LOG_LINES 50
#define MAX_LINE_LENGTH 100
static char log_buffer[MAX_LOG_LINES * MAX_LINE_LENGTH];
static int log_line_count = 0;
static std::mutex logMutex;

// FTP control callback (defined by user)
void (*ftp_control_callback)(bool start) = nullptr;

void reset_ftp_operation_flag(void) {
    ftp_operation_in_progress.store(false);
}

// Register callback for FTP control
void register_ftp_control_callback(void (*callback)(bool start)) {
    ftp_control_callback = callback;
}

// Update time label periodically
void update_time_label(void) {
    if (!time_label) return;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    lv_label_set_text(time_label, time_str);
}

// Update IP label
// NOTE: Caller must hold lv_lock() before calling this function
void update_ip_label(const char* ip) {
    if (!ip_label) return;
    
    char ip_str[64];
    snprintf(ip_str, sizeof(ip_str), "IP: %s", ip);
    lv_label_set_text(ip_label, ip_str);
}

// Update status label
// NOTE: Caller must hold lv_lock() before calling this function
void update_status(const char* status) {
    if (!status_label) return;

    lv_label_set_text(status_label, status);

    // Show spinner when active
    if (spinner) {
        if (strcmp(status, "Starting...") == 0 || 
            strcmp(status, "Stopping...") == 0 ||
            strcmp(status, "Ready") == 0 ||
            strstr(status, "File") != nullptr) {
            lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Set switch state without triggering callback
// NOTE: Caller must hold lv_lock() before calling this function
void set_server_switch_state(bool enabled) {
    if (!server_switch) return;
    if (enabled) {
        lv_obj_add_state(server_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(server_switch, LV_STATE_CHECKED);
    }
}

// Optimized addLog with line limit and auto-scroll
void addLog(const char* message) {
    if (!log_textarea || !message) return;
    
    // Get current timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char timestamp[16];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
    
    // Format log line with timestamp
    char log_line[MAX_LINE_LENGTH];
    snprintf(log_line, sizeof(log_line), "[%s] %s\n", timestamp, message);

    // Lock buffer manipulation
    {
        std::lock_guard<std::mutex> lock(logMutex);

        size_t current_len = strlen(log_buffer);
        size_t line_len = strlen(log_line);
        size_t max_len = sizeof(log_buffer) - 1;

        // Keep removing oldest lines until we have space OR we've cleared everything
        while (current_len + line_len > max_len && log_line_count > 0) {
            char* first_newline = strchr(log_buffer, '\n');
            if (first_newline) {
                size_t bytes_to_remove = (first_newline - log_buffer) + 1;
                memmove(log_buffer, first_newline + 1, strlen(first_newline + 1) + 1);
                log_line_count--;
                current_len -= bytes_to_remove;
            } else {
                // No newline found, clear everything
                log_buffer[0] = '\0';
                log_line_count = 0;
                current_len = 0;
                break;
            }
        }

        // Also enforce MAX_LOG_LINES limit
        while (log_line_count >= MAX_LOG_LINES) {
            char* first_newline = strchr(log_buffer, '\n');
            if (first_newline) {
                memmove(log_buffer, first_newline + 1, strlen(first_newline + 1) + 1);
                log_line_count--;
            } else {
                log_buffer[0] = '\0';
                log_line_count = 0;
                break;
            }
        }

        // Now append the new line (should always fit now)
        current_len = strlen(log_buffer);
        if (current_len + line_len <= max_len) {
            if (current_len == 0) {
                strncpy(log_buffer, log_line, max_len);
            } else {
            strncat(log_buffer, log_line, max_len - current_len);
            }
            log_buffer[max_len] = '\0';
            log_line_count++;
        } else {
            // Still doesn't fit (line too long) - truncate and add
            ESP_LOGW(TAG, "Log line truncated (too long)");
            strncpy(log_buffer, log_line, max_len);
            log_buffer[max_len] = '\0';
            log_line_count = 1;
        }
    } // logMutex unlocks here

    // Lock LVGL operations
    // Update textarea
    lv_lock();

    lv_textarea_set_text(log_textarea, log_buffer);
    
    // Auto-scroll to bottom
    lv_obj_scroll_to_y(log_textarea, LV_COORD_MAX, LV_ANIM_ON);
    lv_unlock();
}

// Clear all logs
void clearLog(void) {
    if (!log_textarea) return;
    
    {
        std::lock_guard<std::mutex> lock(logMutex);
        log_buffer[0] = '\0';
        log_line_count = 0;
    }
    lv_lock();
    lv_textarea_set_text(log_textarea, "");
    lv_unlock();
}

// Clear button event handler
static void clear_button_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        clearLog();
        addLog("#00ff00 [OK] Log cleared#");
    }
}

// Server toggle switch event handler
static void server_switch_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
        bool is_checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
        
        // Ignore if operation in progress
        if (ftp_operation_in_progress.load()) {
            ESP_LOGW(TAG, "FTP operation in progress, ignoring toggle");
            // Revert switch
            if (is_checked) {
                lv_obj_clear_state(sw, LV_STATE_CHECKED);
            } else {
                lv_obj_add_state(sw, LV_STATE_CHECKED);
            }
            return;
        }

        ftp_operation_in_progress.store(true);

        if (is_checked) {
            addLog("Starting FTP server...");
            update_status("Starting...");
        } else {
            addLog("Stopping FTP server...");
            update_status("Stopping...");
        }
        
        // Call user callback to actually start/stop server
        if (ftp_control_callback) {
            ftp_control_callback(is_checked);
        }
    }
}

static void spinner_constructor(const lv_obj_class_t* object_class, lv_obj_t* object);

const lv_obj_class_t tt_spinner_class = {
    .base_class = &lv_image_class,
    .constructor_cb = spinner_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "tt_spinner",
    .width_def = 0,
    .height_def = 0,
    .editable = 0,
    .group_def = 0,
    .instance_size = 0,
    .theme_inheritable = 0
};

lv_obj_t* spinner_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&tt_spinner_class, parent);
    lv_obj_class_init_obj(obj);

    lv_image_set_src(obj, &spinner_img);

    return obj;
}

static void anim_rotation_callback(void* var, int32_t v) {
    auto* object = (lv_obj_t*) var;
    auto width = lv_obj_get_width(object);
    auto height = lv_obj_get_height(object);
    lv_obj_set_style_transform_pivot_x(object, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(object, height / 2, 0);
    lv_obj_set_style_transform_rotation(object, v, 0);
}

static void spinner_constructor(__attribute__((unused)) const lv_obj_class_t* object_class, lv_obj_t* object) {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, object);
    lv_anim_set_values(&a, 0, SPINNER_ROTATION_DEGREES);
    lv_anim_set_duration(&a, SPINNER_ANIMATION_DURATION_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, anim_rotation_callback);
    lv_anim_start(&a);
}

// Create the FTP screen (800x480 optimized, LVGL 9.4)
void create_screen_ftp(void) {
    ESP_LOGI(TAG, "Creating FTP screen");
    
    // Create main screen
    screen_ftp = lv_obj_create(nullptr);
    lv_obj_set_size(screen_ftp, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_ftp, lv_color_hex(0x1a1a1a), 0);
    lv_obj_remove_flag(screen_ftp, LV_OBJ_FLAG_SCROLLABLE);
    
    // ========================================
    // Header Panel (Responsive height)
    // ========================================
    lv_obj_t* header_panel = lv_obj_create(screen_ftp);
    lv_obj_set_size(header_panel, SCREEN_WIDTH, HEADER_HEIGHT_PX);
    lv_obj_align(header_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header_panel, lv_color_hex(UI_COLOR_HEADER), 0);
    lv_obj_set_style_border_width(header_panel, 0, 0);
    lv_obj_set_style_radius(header_panel, 0, 0);
    lv_obj_set_style_pad_all(header_panel, LOG_PANEL_PADDING, 0);
    
    // Title Label
    lv_obj_t* title_label = lv_label_create(header_panel);
#if SCREEN_WIDTH >= 800
    lv_label_set_text(title_label, "FTP Server");
#else
    lv_label_set_text(title_label, "FTP");
#endif
    lv_obj_set_style_text_font(title_label, TITLE_FONT, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(UI_COLOR_TITLE_TEXT), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 10, 0);

    // Port Label (right side of title)
    port_label = lv_label_create(header_panel);
    lv_label_set_text(port_label, "Port: 21");
    lv_obj_set_style_text_font(port_label, PORT_FONT, 0);
    lv_obj_set_style_text_color(port_label, lv_color_hex(0x888888), 0);
    lv_obj_align(port_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // Status Label (to the left of port label)
    status_label = lv_label_create(header_panel);
    lv_label_set_text(status_label, "Disabled");
    lv_obj_set_style_text_font(status_label, STATUS_FONT, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(UI_COLOR_STATUS_TEXT), 0);
    lv_obj_align_to(status_label, port_label, LV_ALIGN_OUT_LEFT_MID, -20, 0);

    // Spinner (positioned to the left of status label)
    spinner = spinner_create(header_panel);
    lv_obj_align_to(spinner, status_label, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    
    // ========================================
    // Info Bar (Below header - responsive height)
    // ========================================
    lv_obj_t* info_bar = lv_obj_create(screen_ftp);
    lv_obj_set_size(info_bar, SCREEN_WIDTH, INFO_BAR_HEIGHT_PX);
    lv_obj_align(info_bar, LV_ALIGN_TOP_MID, 0, HEADER_HEIGHT_PX);
    lv_obj_set_style_bg_color(info_bar, lv_color_hex(0x242424), 0);
    lv_obj_set_style_border_width(info_bar, 0, 0);
    lv_obj_set_style_radius(info_bar, 0, 0);
    lv_obj_set_style_pad_all(info_bar, LOG_PANEL_PADDING, 0);
    lv_obj_remove_flag(info_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // IP Address Label (left side)
    ip_label = lv_label_create(info_bar);
    lv_label_set_text(ip_label, "IP: Connecting...");
    lv_obj_set_style_text_font(ip_label, INFO_FONT, 0);
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(ip_label, LV_ALIGN_LEFT_MID, 10, 0);

    // Time Label (middle-right area)
    time_label = lv_label_create(info_bar);
    lv_label_set_text(time_label, "--:--:--");
    lv_obj_set_style_text_font(time_label, INFO_FONT, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 20, 0);

    // Clear Log Button (small button, right of time)
    clear_log_btn = lv_button_create(info_bar);
    lv_obj_set_size(clear_log_btn, 50, 30);
    lv_obj_align_to(clear_log_btn, time_label, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
    lv_obj_set_style_bg_color(clear_log_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(clear_log_btn, clear_button_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* clear_icon = lv_label_create(clear_log_btn);
    lv_label_set_text(clear_icon, LV_SYMBOL_TRASH);
    lv_obj_center(clear_icon);

    // Server ON/OFF Switch (right side)
    server_switch = lv_switch_create(info_bar);
    lv_obj_set_size(server_switch, SWITCH_WIDTH, SWITCH_HEIGHT);
    lv_obj_align(server_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(server_switch, server_switch_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    // Start in OFF state
    lv_obj_clear_state(server_switch, LV_STATE_CHECKED);

    // ========================================
    // Log Panel (Main area)
    // ========================================
    lv_obj_t* log_panel = lv_obj_create(screen_ftp);
    lv_obj_set_size(log_panel, SCREEN_WIDTH - 20, LOG_PANEL_HEIGHT);
    lv_obj_align(log_panel, LV_ALIGN_TOP_MID, 0, HEADER_HEIGHT_PX + INFO_BAR_HEIGHT_PX + LOG_PANEL_SPACING);
    lv_obj_set_style_bg_color(log_panel, lv_color_hex(UI_COLOR_BG_DARK), 0);
    lv_obj_set_style_border_color(log_panel, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_width(log_panel, 2, 0);
    lv_obj_set_style_radius(log_panel, 5, 0);
    lv_obj_set_style_pad_all(log_panel, LOG_PANEL_PADDING, 0);
    lv_obj_remove_flag(log_panel, LV_OBJ_FLAG_SCROLLABLE);
    
    // Log Label (title)
    lv_obj_t* log_title = lv_label_create(log_panel);
    lv_label_set_text(log_title, "Activity Log");
    lv_obj_set_style_text_font(log_title, LOG_FONT, 0);
    lv_obj_set_style_text_color(log_title, lv_color_hex(0x888888), 0);
    lv_obj_align(log_title, LV_ALIGN_TOP_LEFT, 5, 5);
    
    // Log Textarea (scrollable - responsive)
    log_textarea = lv_textarea_create(log_panel);
    lv_obj_set_size(log_textarea, SCREEN_WIDTH - 50, LOG_TEXTAREA_HEIGHT_PX);
    lv_obj_align(log_textarea, LV_ALIGN_TOP_MID, 0, LOG_PANEL_TITLE_HEIGHT);
    lv_textarea_set_text(log_textarea, "");
    lv_obj_set_style_bg_color(log_textarea, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(log_textarea, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(log_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(log_textarea, LOG_FONT, 0);
    lv_obj_set_style_border_color(log_textarea, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(log_textarea, 1, 0);
    lv_textarea_set_one_line(log_textarea, false);
    lv_obj_set_scrollbar_mode(log_textarea, LV_SCROLLBAR_MODE_AUTO);
    lv_label_set_recolor(lv_textarea_get_label(log_textarea), true);
    
    lv_obj_add_flag(log_textarea, LV_OBJ_FLAG_SCROLLABLE);      // Enable touch scrolling
    lv_obj_clear_flag(log_textarea, LV_OBJ_FLAG_CLICK_FOCUSABLE); // Don't focus on click
    lv_textarea_set_cursor_click_pos(log_textarea, false);      // No cursor positioning

    // Initialize log buffer
    memset(log_buffer, 0, sizeof(log_buffer));
    log_line_count = 0;

    lv_screen_load(screen_ftp);

    ESP_LOGI(TAG, "FTP screen created successfully");
}

// Create a timer to update time every second
void start_time_update_timer(void) {
    if (time_update_timer) {
        return;  // Timer already running
    }
    time_update_timer = lv_timer_create([](lv_timer_t* timer) {
        update_time_label();
    }, 1000, nullptr);
}
