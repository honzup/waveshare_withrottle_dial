#include <stdio.h>
#include "lcd_touch_bsp.h"
#include "i2c_bsp.h"

void lcd_touch_init(void)
{
  uint8_t data = 0x00;
  ESP_ERROR_CHECK(i2c_write_buff(disp_touch_dev_handle,0x00,&data,1)); //切换正常模式
}

uint8_t tpGetCoordinates(uint16_t *x,uint16_t *y)
{
  if (x == NULL || y == NULL) {
    return 0;
  }
  uint8_t GetNum = 0;
  uint8_t data[7] = {0};
  
  // Attempt I2C read with error handling
  esp_err_t err = i2c_read_buff(disp_touch_dev_handle,0x00,data,7);
  if (err != ESP_OK) {
    // I2C read failed - return no touch detected instead of hanging
    static uint32_t error_count = 0;
    error_count++;
    return 0;
  }
  
  GetNum = data[2];
  if(GetNum)
  {
    *x = ((uint16_t)(data[3] & 0x0f)<<8) + (uint16_t)data[4];
    *y = ((uint16_t)(data[5] & 0x0f)<<8) + (uint16_t)data[6];
    return 1;
  }
  return 0;
}
