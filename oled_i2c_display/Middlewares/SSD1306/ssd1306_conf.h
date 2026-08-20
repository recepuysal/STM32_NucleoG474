/**
 * Project-specific configuration for the vendored SSD1306 library
 * (see LICENSE-ssd1306.txt). Based on ssd1306_conf_template.h from
 * the upstream repository, adapted for NUCLEO-G474RE + I2C1.
 */

#ifndef __SSD1306_CONF_H__
#define __SSD1306_CONF_H__

// Choose a microcontroller family
#define STM32G4

// Choose a bus
#define SSD1306_USE_I2C

// I2C Configuration
#define SSD1306_I2C_PORT        hi2c1
#define SSD1306_I2C_ADDR        (0x3C << 1)

// Mirror the screen if needed
// #define SSD1306_MIRROR_VERT
// #define SSD1306_MIRROR_HORIZ

// Set inverse color if needed
// #define SSD1306_INVERSE_COLOR

// Include only needed fonts
#define SSD1306_INCLUDE_FONT_6x8

// 128x64 is the common size for these modules (default values, kept
// explicit here for clarity).
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64

#endif /* __SSD1306_CONF_H__ */
