#pragma once

#include "driver/spi_master.h"

// ============================================================
// HisuOS - Zhengchen 1.54" Board Configuration
// ============================================================

// ------------------------------------------------------------
// LCD - VERIFIED
// ST7789 / 240x240 / SPI
// ------------------------------------------------------------

#define LCD_HOST       SPI2_HOST

#define LCD_MOSI       41
#define LCD_SCLK       42
#define LCD_CS         21
#define LCD_DC         40
#define LCD_RST        45
#define LCD_BL         20

#define LCD_WIDTH      240
#define LCD_HEIGHT     240


// ------------------------------------------------------------
// Microphone - VERIFIED
// ------------------------------------------------------------

#define MIC_WS         4
#define MIC_SCK        5
#define MIC_SD         6


// ------------------------------------------------------------
// Speaker - NOT YET VERIFIED
// ------------------------------------------------------------

#define SPK_DOUT       7
#define SPK_BCLK       15
#define SPK_LRCK       16


// ------------------------------------------------------------
// Buttons - VERIFIED
// ------------------------------------------------------------

#define BTN_BOOT       0
#define BTN_VOL_UP     10
#define BTN_VOL_DOWN   39


// ------------------------------------------------------------
// Battery ADC - NOT YET VERIFIED
// ------------------------------------------------------------

#define BAT_ADC        8
#define BAT_ADC_CHANNEL ADC_CHANNEL_7
#define CHARGE_GPIO    9
