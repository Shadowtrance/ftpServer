#pragma once

#include <stdint.h>

// Screen dimensions
#define SUNTON_ESP32_LCD_WIDTH          800
#define SUNTON_ESP32_LCD_HEIGHT         480
#define SUNTON_ESP32_LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000)

// Backlight control
#define SUNTON_ESP32_BCKL_ON_LEVEL  1
#define SUNTON_ESP32_BCKL_OFF_LEVEL 0
#define SUNTON_ESP32_PIN_BCKL        GPIO_NUM_2

// LCD RGB interface pins
#define SUNTON_ESP32_LCD_PIN_HSYNC   GPIO_NUM_39
#define SUNTON_ESP32_LCD_PIN_VSYNC   GPIO_NUM_41
#define SUNTON_ESP32_LCD_PIN_DE      GPIO_NUM_40
#define SUNTON_ESP32_LCD_PIN_PCLK    GPIO_NUM_42

// LCD data pins (RGB565)
#define SUNTON_ESP32_LCD_PIN_DATA0   GPIO_NUM_8  // B3
#define SUNTON_ESP32_LCD_PIN_DATA1   GPIO_NUM_3  // B4
#define SUNTON_ESP32_LCD_PIN_DATA2   GPIO_NUM_46 // B5
#define SUNTON_ESP32_LCD_PIN_DATA3   GPIO_NUM_9  // B6
#define SUNTON_ESP32_LCD_PIN_DATA4   GPIO_NUM_1  // B7

#define SUNTON_ESP32_LCD_PIN_DATA5   GPIO_NUM_5  // G2
#define SUNTON_ESP32_LCD_PIN_DATA6   GPIO_NUM_6  // G3
#define SUNTON_ESP32_LCD_PIN_DATA7   GPIO_NUM_7  // G4
#define SUNTON_ESP32_LCD_PIN_DATA8   GPIO_NUM_15 // G5
#define SUNTON_ESP32_LCD_PIN_DATA9   GPIO_NUM_16 // G6
#define SUNTON_ESP32_LCD_PIN_DATA10  GPIO_NUM_4  // G7

#define SUNTON_ESP32_LCD_PIN_DATA11  GPIO_NUM_45 // R3
#define SUNTON_ESP32_LCD_PIN_DATA12  GPIO_NUM_48 // R4
#define SUNTON_ESP32_LCD_PIN_DATA13  GPIO_NUM_47 // R5
#define SUNTON_ESP32_LCD_PIN_DATA14  GPIO_NUM_21 // R6
#define SUNTON_ESP32_LCD_PIN_DATA15  GPIO_NUM_14 // R7

#define SUNTON_ESP32_LCD_PIN_DISP_EN GPIO_NUM_NC // not connected

// Touch controller pins
#define SUNTON_ESP32_TOUCH_PIN_RST   GPIO_NUM_38
#define SUNTON_ESP32_TOUCH_PIN_SCL   GPIO_NUM_20
#define SUNTON_ESP32_TOUCH_PIN_SDA   GPIO_NUM_19
#define SUNTON_ESP32_TOUCH_PIN_INT   GPIO_NUM_NC
#define SUNTON_ESP32_TOUCH_FREQ_HZ   (400000)
#define SUNTON_ESP32_TOUCH_ADDRESS   ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS

// SD card pins (for reference - not used in display init)
#define SUNTON_ESP32_SDCARD_PIN_CS       GPIO_NUM_10
#define SUNTON_ESP32_SDCARD_PIN_MOSI     GPIO_NUM_11
#define SUNTON_ESP32_SDCARD_PIN_CLK      GPIO_NUM_12
#define SUNTON_ESP32_SDCARD_PIN_MISO     GPIO_NUM_13

// Backlight PWM settings
#define BACKLIGHT_LEDC_TIMER             LEDC_TIMER_0
#define BACKLIGHT_CHANNEL                LEDC_CHANNEL_0

// Forward declaration for LVGL display type
typedef struct _lv_display_t lv_display_t;

/**
 * @brief Set LCD backlight brightness
 *
 * @param brightness Brightness level (0-255, 0=off, 255=full brightness)
 */
void suntonEsp32s3SetBrightness(uint8_t brightness);

/**
 * @brief Initialize the SUNTON ESP32-S3 LCD and touch controller
 *
 * Initializes:
 * - I2C bus for touch controller
 * - RGB LCD panel with optimized timings
 * - GT911 capacitive touch controller
 * - LVGL library using esp_lvgl_port
 * - Backlight PWM controller
 *
 * @return Pointer to LVGL display object, or nullptr on failure
 */
lv_display_t *suntonEsp32s3LcdInit(void);
