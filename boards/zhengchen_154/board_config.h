#pragma once

#include "driver/spi_master.h"

// HisuOS - Zhengchen 1.54 TFT WiFi (ESP32-S3 N16R8).
// Hardware constants only; resource initialization belongs to board drivers.

#define BOARD_NAME             "Zhengchen 1.54 TFT WiFi"
#define BOARD_FLASH_SIZE_MB    16
#define BOARD_PSRAM_SIZE_MB    8
#define BOARD_HAS_TOUCH        0

// ST7789, 240x240, SPI.
#define LCD_HOST               SPI2_HOST
#define LCD_MOSI               41
#define LCD_SCLK               42
#define LCD_CS                 21
#define LCD_DC                 40
#define LCD_RST                45
#define LCD_BL                 20
#define LCD_WIDTH              240
#define LCD_HEIGHT             240

// Separate I2S microphone and speaker pins.
#define MIC_WS                 4
#define MIC_SCK                5
#define MIC_SD                 6
#define SPK_DOUT               7
#define SPK_BCLK               15
#define SPK_LRCK               16

// Active-low buttons; debounce and gestures are not board configuration.
#define BTN_BOOT               0
#define BTN_VOL_UP             10
#define BTN_VOL_DOWN           39
#define BTN_ACTIVE_LEVEL       0

// GPIO8 maps to ADC1 channel 7; battery calibration is not defined here.
#define BAT_ADC                8

// Muse reference only: verify GPIO9 wiring and polarity before using it.
#define CHARGE_STATUS_GPIO     9
