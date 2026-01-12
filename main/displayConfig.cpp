#include "displayConfig.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "[DISPLAY]";

// Static handles for cleanup
static esp_lcd_panel_handle_t panel_handle = nullptr;
static esp_lcd_touch_handle_t touch_handle = nullptr;
static lv_display_t *lvgl_disp = nullptr;
static bool backlight_initialized = false;
static bool i2c_initialized = false;

/**
 * @brief RGB LCD panel configuration
 *
 * Optimized timings for SUNTON ESP32-S3 8048S043 board:
 * - 16 MHz pixel clock
 * - Double buffering in PSRAM
 * - Bounce buffer for smooth rendering
 */
static const esp_lcd_rgb_panel_config_t panel_config = {
    .clk_src = LCD_CLK_SRC_PLL160M,
    .timings = {
        .pclk_hz = SUNTON_ESP32_LCD_PIXEL_CLOCK_HZ,
        .h_res = SUNTON_ESP32_LCD_WIDTH,
        .v_res = SUNTON_ESP32_LCD_HEIGHT,
        .hsync_pulse_width = 4,
        .hsync_back_porch = 8,
        .hsync_front_porch = 8,
        .vsync_pulse_width = 4,
        .vsync_back_porch = 8,
        .vsync_front_porch = 8,
        .flags = {
            .hsync_idle_low = false,
            .vsync_idle_low = false,
            .de_idle_high = false,
            .pclk_active_neg = true,
            .pclk_idle_high = false,
        },
    },
    .data_width = 16,  // RGB565
    .bits_per_pixel = 0,
    .num_fbs = 2,  // Double buffering
    .bounce_buffer_size_px = SUNTON_ESP32_LCD_WIDTH * 10,  // Small bounce buffer for efficiency
    .sram_trans_align = 8,
    .psram_trans_align = 64,
    .hsync_gpio_num = SUNTON_ESP32_LCD_PIN_HSYNC,
    .vsync_gpio_num = SUNTON_ESP32_LCD_PIN_VSYNC,
    .de_gpio_num = SUNTON_ESP32_LCD_PIN_DE,
    .pclk_gpio_num = SUNTON_ESP32_LCD_PIN_PCLK,
    .disp_gpio_num = SUNTON_ESP32_LCD_PIN_DISP_EN,
    .data_gpio_nums = {
        // Blue (5 bits)
        SUNTON_ESP32_LCD_PIN_DATA0,
        SUNTON_ESP32_LCD_PIN_DATA1,
        SUNTON_ESP32_LCD_PIN_DATA2,
        SUNTON_ESP32_LCD_PIN_DATA3,
        SUNTON_ESP32_LCD_PIN_DATA4,
        // Green (6 bits)
        SUNTON_ESP32_LCD_PIN_DATA5,
        SUNTON_ESP32_LCD_PIN_DATA6,
        SUNTON_ESP32_LCD_PIN_DATA7,
        SUNTON_ESP32_LCD_PIN_DATA8,
        SUNTON_ESP32_LCD_PIN_DATA9,
        SUNTON_ESP32_LCD_PIN_DATA10,
        // Red (5 bits)
        SUNTON_ESP32_LCD_PIN_DATA11,
        SUNTON_ESP32_LCD_PIN_DATA12,
        SUNTON_ESP32_LCD_PIN_DATA13,
        SUNTON_ESP32_LCD_PIN_DATA14,
        SUNTON_ESP32_LCD_PIN_DATA15,
    },
    .flags = {
        .disp_active_low = false,
        .refresh_on_demand = false,
        .fb_in_psram = true,      // Frame buffers in PSRAM
        .double_fb = true,
        .no_fb = false,
        .bb_invalidate_cache = false,
    },
};

/**
 * @brief Initialize backlight PWM controller
 *
 * Uses LEDC peripheral to control backlight brightness.
 * Starts with full brightness (255).
 */
static void backlight_init(void)
{
    ESP_LOGI(TAG, "Initializing backlight PWM");

    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = BACKLIGHT_LEDC_TIMER,
        .freq_hz         = 200,  // 200 Hz PWM
        .clk_cfg         = LEDC_USE_RC_FAST_CLK,
        .deconfigure     = false
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num   = SUNTON_ESP32_PIN_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BACKLIGHT_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BACKLIGHT_LEDC_TIMER,
        .duty       = 0,  // Start at 0% (will be set to 100% below)
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags      = {
            .output_invert = 0,
        }
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Set to full brightness
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, 255));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL));

    backlight_initialized = true;
}

void suntonEsp32s3SetBrightness(uint8_t brightness)
{
    if (!backlight_initialized) {
        ESP_LOGW(TAG, "Backlight not initialized, ignoring brightness change");
        return;
    }
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, brightness));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL));
}

static void cleanup_resources(void)
{
    if (touch_handle) {
        esp_lcd_touch_del(touch_handle);
        touch_handle = nullptr;
    }
    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
        panel_handle = nullptr;
    }
    if (i2c_initialized) {
        i2c_driver_delete(I2C_NUM_0);
        i2c_initialized = false;
    }
}

/**
 * @brief Initialize I2C bus for GT911 touch controller
 */
static esp_err_t i2c_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus");

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SUNTON_ESP32_TOUCH_PIN_SDA,
        .scl_io_num = SUNTON_ESP32_TOUCH_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = SUNTON_ESP32_TOUCH_FREQ_HZ,
        },
        .clk_flags = 0
    };

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_initialized = true;
    return ESP_OK;
}

/**
 * @brief Initialize RGB LCD panel
 */
static esp_err_t lcd_panel_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LCD panel");

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_reset(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_init(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RGB LCD panel initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize GT911 capacitive touch controller
 */
static esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    // Create I2C panel IO for GT911
    esp_lcd_panel_io_handle_t touch_io_handle = nullptr;
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = SUNTON_ESP32_TOUCH_ADDRESS,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 0,
        .flags = {
            .dc_low_on_data = 0,
            .disable_control_phase = 1,
        },
        .scl_speed_hz = 0,  // Use default I2C speed
    };

    esp_err_t ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_config, &touch_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch I2C panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_touch_io_gt911_config_t gt911_config = {
        .dev_addr = static_cast<uint8_t>(SUNTON_ESP32_TOUCH_ADDRESS),
    };

    // Create GT911 touch driver
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = SUNTON_ESP32_LCD_WIDTH,
        .y_max = SUNTON_ESP32_LCD_HEIGHT,
        .rst_gpio_num = SUNTON_ESP32_TOUCH_PIN_RST,
        .int_gpio_num = SUNTON_ESP32_TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = &gt911_config
    };

    ret = esp_lcd_touch_new_i2c_gt911(touch_io_handle, &touch_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 touch: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GT911 touch controller initialized successfully");
    return ESP_OK;
}

lv_display_t *suntonEsp32s3LcdInit(void)
{
    ESP_LOGI(TAG, "=== Starting Display Initialization ===");

    // 1. Initialize I2C for touch controller
    if (i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        cleanup_resources();
        return nullptr;
    }

    // 2. Initialize RGB LCD panel
    if (lcd_panel_init() != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel initialization failed");
        cleanup_resources();
        return nullptr;
    }

    // 3. Initialize backlight
    backlight_init();

    // 4. Initialize touch controller
    if (touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed");
        cleanup_resources();
        return nullptr;
    }

    // 5. Initialize esp_lvgl_port
    ESP_LOGI(TAG, "Initializing LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(ret));
        cleanup_resources();
        return nullptr;
    }

    // 6. Add RGB display to LVGL port
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = nullptr,
        .panel_handle = panel_handle,
        .control_handle = nullptr,
        .buffer_size = SUNTON_ESP32_LCD_WIDTH * SUNTON_ESP32_LCD_HEIGHT,
        .double_buffer = true,
        .trans_size = 0,
        .hres = SUNTON_ESP32_LCD_WIDTH,
        .vres = SUNTON_ESP32_LCD_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false
        }

    };

    // 7. Add RGB display to LVGL port
    ESP_LOGI(TAG, "Adding RGB display to LVGL");
    const lvgl_port_display_rgb_cfg_t rgb_disp_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = false
        }
    };

    lvgl_disp = lvgl_port_add_disp_rgb(&display_config, &rgb_disp_cfg);
    if (!lvgl_disp) {
        ESP_LOGE(TAG, "Failed to add RGB display to LVGL port");
        lvgl_port_deinit();
        cleanup_resources();
        return nullptr;
    }

    // 8. Add touch input to LVGL port
    ESP_LOGI(TAG, "Adding touch to LVGL");
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
        .scale = {
            .x = 0,
            .y = 0
        }
    };

    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!touch_indev) {
        ESP_LOGE(TAG, "Failed to add touch to LVGL port");
        lvgl_port_deinit();
        cleanup_resources();
        return nullptr;
    }

    ESP_LOGI(TAG, "=== Display Initialization Complete ===");
    ESP_LOGI(TAG, "Display: %dx%d RGB565", SUNTON_ESP32_LCD_WIDTH, SUNTON_ESP32_LCD_HEIGHT);
    ESP_LOGI(TAG, "Touch: GT911 capacitive");
    ESP_LOGI(TAG, "LVGL: v%d.%d.%d", lv_version_major(), lv_version_minor(), lv_version_patch());

    return lvgl_disp;
}
