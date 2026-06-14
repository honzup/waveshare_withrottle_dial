#ifndef USER_CONFIG_H
#define USER_CONFIG_H

//spi & i2c handle
#define LCD_HOST SPI2_HOST
#define TOUCH_HOST I2C_NUM_0

// I2C
#define ESP32_SCL_NUM (GPIO_NUM_12)
#define ESP32_SDA_NUM (GPIO_NUM_11)

//  DISP
// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 360
#define EXAMPLE_LCD_V_RES 360
// Two LVGL draw buffers live in internal DMA RAM (360 * BUF_HEIGHT * 2 B each).
// At V_RES/10 (36 lines) that was ~52 KB of internal RAM — enough to starve
// ui_update_task's 8 KB stack at boot. /20 (18 lines) halves it; the only cost
// is more (still cheap) partial flushes per frame.
#define EXAMPLE_LVGL_BUF_HEIGHT (EXAMPLE_LCD_V_RES / 20)

#define EXAMPLE_PIN_NUM_LCD_CS (gpio_num_t) 14
#define EXAMPLE_PIN_NUM_LCD_PCLK (gpio_num_t) 13
#define EXAMPLE_PIN_NUM_LCD_DATA0 (gpio_num_t) 15
#define EXAMPLE_PIN_NUM_LCD_DATA1 (gpio_num_t) 16
#define EXAMPLE_PIN_NUM_LCD_DATA2 (gpio_num_t) 17
#define EXAMPLE_PIN_NUM_LCD_DATA3 (gpio_num_t) 18
#define EXAMPLE_PIN_NUM_LCD_RST (gpio_num_t) 21
#define EXAMPLE_PIN_NUM_BK_LIGHT (gpio_num_t) 47

#define EXAMPLE_TOUCH_ADDR 0x15
#define EXAMPLE_PIN_NUM_TOUCH_RST (gpio_num_t) 10
#define EXAMPLE_PIN_NUM_TOUCH_INT (gpio_num_t) 9

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 5
#define EXAMPLE_LVGL_TASK_STACK_SIZE (8 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY 2

#define EXAMPLE_USE_TOUCH 1 //Without tp ---- Touch off

// #define Backlight_Testing
// #define EXAMPLE_Rotate_90

//encoder
#define EXAMPLE_ENCODER_ECA_PIN 8
#define EXAMPLE_ENCODER_ECB_PIN 7

//bit
#define SET_BIT(reg, bit) (reg |= ((uint32_t) 0x01 << bit))
#define CLEAR_BIT(reg, bit) (reg &= (~((uint32_t) 0x01 << bit)))
#define READ_BIT(reg, bit) (((uint32_t) reg >> bit) & 0x01)
#define BIT_EVEN_ALL (0x00ffffff)

#endif
