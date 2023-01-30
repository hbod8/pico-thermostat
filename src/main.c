/**
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "raspberry26x32.h"
#include "fonts/A.xbm"
#include "display.c"
#include "rect.h"

int main()
{
  stdio_init_all();

  // Waits for serial connction to run code
  while (!stdio_usb_connected())
  {
    printf("waiting for serial connection");
    sleep_ms(1000);
  }

  printf("got serial connection\n");

  // useful information for picotool
  bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));
  bi_decl(bi_program_description("OLED I2C example for the Raspberry Pi Pico"));

  // I2C is "open drain", pull ups to keep signal high when no data is being
  // sent
  i2c_init(i2c_default, 400 * 1000);
  gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
  gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

  // run through the complete initialization process
  printf("initializing sceen...");
  init();
  printf("done\n");

  // zero the gddram
  printf("claring the gddram...");
  clear_gddram();
  printf("done\n");


  // intro sequence: flash the screen 3 times
  printf("intro sequence...");
  for (int i = 0; i < 3; i++)
  {
    printf("%d ", i + 1);
    send_cmd(SET_ENTIRE_ON); // ignore RAM, all pixels on
    sleep_ms(300);
    send_cmd(SET_ENTIRE_OFF); // go back to following RAM
    sleep_ms(300);
  }
  printf("done\n");

  // glyph rect = {
  //   height : RECT_height,
  //   width : RECT_width,
  //   buf : RECT_bits
  // };

  // clear_gddram();

  // write_glyph(&rect, 0, 0);

  glyph a = {
    height : A_height,
    width : A_width,
    buf : A_bits
  };

  dump_glyph(&a);

  write_glyph(&a, 0, 0);

  return 0;
}
