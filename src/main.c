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

/* Example code to talk to an SSD1306-based OLED display

   NOTE: Ensure the device is capable of being driven at 3.3v NOT 5v. The Pico
   GPIO (and therefore I2C) cannot be used at 5v.

   You will need to use a level shifter on the I2C lines if you want to run the
   board at 5v.

   Connections on Raspberry Pi Pico board, other boards may vary.

   GPIO PICO_DEFAULT_I2C_SDA_PIN (on Pico this is GP4 (pin 6)) -> SDA on display
   board
   GPIO PICO_DEFAULT_I2C_SCK_PIN (on Pico this is GP5 (pin 7)) -> SCL on
   display board
   3.3v (pin 36) -> VCC on display board
   GND (pin 38)  -> GND on display board
*/

// commands (see datasheet)
#define OLED_SET_CONTRAST _u(0x81)
#define OLED_SET_ENTIRE_OFF _u(0xA4)
#define OLED_SET_ENTIRE_ON _u(0xA5)
#define OLED_SET_NORM_INV _u(0xA6)
#define OLED_SET_DISP _u(0xAE)
#define OLED_SET_MEM_ADDR _u(0x20)
#define OLED_SET_COL_ADDR _u(0x21)
#define OLED_SET_PAGE_ADDR _u(0x22)
#define OLED_SET_DISP_START_LINE _u(0x40)
#define OLED_SET_SEG_REMAP _u(0xA0)
#define OLED_SET_MUX_RATIO _u(0x3F)
#define OLED_SET_COM_OUT_DIR _u(0xC0)
#define OLED_SET_DISP_OFFSET _u(0xD3)
#define OLED_SET_COM_PIN_CFG _u(0x12)
#define OLED_SET_DISP_CLK_DIV _u(0xD5)
#define OLED_SET_PRECHARGE _u(0xD9)
#define OLED_SET_VCOM_DESEL _u(0xDB)
#define OLED_SET_CHARGE_PUMP _u(0x8D)
#define OLED_SET_HORIZ_SCROLL _u(0x26)
#define OLED_SET_SCROLL _u(0x2E)

#define OLED_ADDR _u(0x3C)
#define OLED_HEIGHT _u(64)
#define OLED_WIDTH _u(128)
#define OLED_PAGE_HEIGHT _u(8)
#define OLED_NUM_PAGES OLED_HEIGHT / OLED_PAGE_HEIGHT
#define OLED_BUF_LEN (OLED_NUM_PAGES * OLED_WIDTH)

#define OLED_WRITE_MODE _u(0xFE)
#define OLED_READ_MODE _u(0xFF)

typedef struct render_area
{
  uint8_t start_col;
  uint8_t end_col;
  uint8_t start_page;
  uint8_t end_page;
} render_area;

typedef struct glyph
{
  int len;
  uint8_t *buf;
} glyph;

void fill(uint8_t buf[], uint8_t fill)
{
  // fill entire buffer with the same byte
  for (int i = 0; i < OLED_BUF_LEN; i++)
  {
    buf[i] = fill;
  }
};

void fill_page(uint8_t *buf, uint8_t fill, uint8_t page)
{
  // fill entire page with the same byte
  memset(buf + (page * OLED_WIDTH), fill, OLED_WIDTH);
};

// convenience methods for printing out a buffer to be rendered
// mostly useful for debugging images, patterns, etc

void print_buf_page(uint8_t buf[], uint8_t page)
{
  // prints one page of a full length (128x4) buffer
  for (int j = 0; j < OLED_PAGE_HEIGHT; j++)
  {
    for (int k = 0; k < OLED_WIDTH; k++)
    {
      printf("%u", (buf[page * OLED_WIDTH + k] >> j) & 0x01);
    }
    printf("\n");
  }
}

void print_buf_pages(uint8_t buf[])
{
  // prints all pages of a full length buffer
  for (int i = 0; i < OLED_NUM_PAGES; i++)
  {
    printf("--page %d--\n", i);
    print_buf_page(buf, i);
  }
}

void print_buf_area(uint8_t *buf, struct render_area *area)
{
  // print a render area of generic size
  int area_width = area->end_col - area->start_col + 1;
  int area_height = area->end_page - area->start_page + 1; // in pages, not pixels
  for (int i = 0; i < area_height; i++)
  {
    for (int j = 0; j < OLED_PAGE_HEIGHT; j++)
    {
      for (int k = 0; k < area_width; k++)
      {
        printf("%u", (buf[i * area_width + k] >> j) & 0x01);
      }
      printf("\n");
    }
  }
}

int calc_render_area(struct render_area *area)
{
  // calculate how long the flattened buffer will be for a render area
  return (area->end_col - area->start_col + 1) * (area->end_page - area->start_page + 1);
}

#ifdef i2c_default

void oled_send_cmd(uint8_t cmd)
{
  // I2C write process expects a control byte followed by data
  // this "data" can be a command or data to follow up a command

  // Co = 1, D/C = 0 => the driver expects a command
  uint8_t buf[2] = {0x80, cmd};
  i2c_write_blocking(i2c_default, (OLED_ADDR & OLED_WRITE_MODE), buf, 2, false);
}

void oled_send_buf(uint8_t buf[], int buflen)
{
  // in horizontal addressing mode, the column address pointer auto-increments
  // and then wraps around to the next page, so we can send the entire frame
  // buffer in one gooooooo!

  // copy our frame buffer into a new buffer because we need to add the control byte
  // to the beginning

  // TODO find a more memory-efficient way to do this..
  // maybe break the data transfer into pages?
  uint8_t *temp_buf = malloc(buflen + 1);

  for (int i = 1; i < buflen + 1; i++)
  {
    temp_buf[i] = buf[i - 1];
  }
  // Co = 0, D/C = 1 => the driver expects data to be written to RAM
  temp_buf[0] = 0x40;
  i2c_write_blocking(i2c_default, (OLED_ADDR & OLED_WRITE_MODE), temp_buf, buflen + 1, false);

  free(temp_buf);
}

void oled_init()
{
  // some of these commands are not strictly necessary as the reset
  // process defaults to some of these but they are shown here
  // to demonstrate what the initialization sequence looks like

  // some configuration values are recommended by the board manufacturer

  oled_send_cmd(OLED_SET_DISP | 0x00); // set display off

  /* memory mapping */
  oled_send_cmd(OLED_SET_MEM_ADDR); // set memory address mode
  oled_send_cmd(0x00);              // horizontal addressing mode

  /* resolution and layout */
  oled_send_cmd(OLED_SET_DISP_START_LINE); // set display start line to 0

  oled_send_cmd(OLED_SET_SEG_REMAP | 0x01); // set segment re-map
  // column address 127 is mapped to SEG0

  oled_send_cmd(OLED_SET_MUX_RATIO); // set multiplex ratio
  oled_send_cmd(OLED_HEIGHT - 1);    // our display is only 32 pixels high

  oled_send_cmd(OLED_SET_COM_OUT_DIR | 0x08); // set COM (common) output scan direction
  // scan from bottom up, COM[N-1] to COM0

  oled_send_cmd(OLED_SET_DISP_OFFSET); // set display offset
  oled_send_cmd(0x00);                 // no offset

  oled_send_cmd(OLED_SET_COM_PIN_CFG); // set COM (common) pins hardware configuration
  oled_send_cmd(0x12);                 // manufacturer magic number

  /* timing and driving scheme */
  oled_send_cmd(OLED_SET_DISP_CLK_DIV); // set display clock divide ratio
  oled_send_cmd(0x80);                  // div ratio of 1, standard freq

  oled_send_cmd(OLED_SET_PRECHARGE); // set pre-charge period
  oled_send_cmd(0xF1);               // Vcc internally generated on our board

  oled_send_cmd(OLED_SET_VCOM_DESEL); // set VCOMH deselect level
  oled_send_cmd(0x30);                // 0.83xVcc

  /* display */
  oled_send_cmd(OLED_SET_CONTRAST); // set contrast control
  oled_send_cmd(0xFF);

  oled_send_cmd(OLED_SET_ENTIRE_ON); // set entire display on to follow RAM content

  oled_send_cmd(OLED_SET_NORM_INV); // set normal (not inverted) display

  oled_send_cmd(OLED_SET_CHARGE_PUMP); // set charge pump
  oled_send_cmd(0x14);                 // Vcc internally generated on our board

  oled_send_cmd(OLED_SET_SCROLL | 0x00); // deactivate horizontal scrolling if set
  // this is necessary as memory writes will corrupt if scrolling was enabled

  oled_send_cmd(OLED_SET_DISP | 0x01); // turn display on
}

void render(glyph *gly, render_area *ra)
{
  // update a portion of the display with a render area
  oled_send_cmd(OLED_SET_COL_ADDR);
  oled_send_cmd(ra->start_col);
  oled_send_cmd(ra->end_col);

  oled_send_cmd(OLED_SET_PAGE_ADDR);
  oled_send_cmd(ra->start_page);
  oled_send_cmd(ra->end_page);

  oled_send_buf(gly->buf, gly->len);
}

#endif

int main()
{
  stdio_init_all();

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
  oled_init();

  // initialize render area for entire frame (128 pixels by 4 pages) and zero the entire display
  // allocate frame
  render_area *frame_area = malloc(sizeof(render_area));
  frame_area->start_col = 0;
  frame_area->end_col = OLED_WIDTH - 1;
  frame_area->start_page = 0;
  frame_area->end_page = OLED_NUM_PAGES - 1;
  // allocate glyph (whole screen in this case)
  uint8_t *buf = calloc(calc_render_area(frame_area), sizeof(uint8_t));
  glyph *blank_screen = malloc(sizeof(glyph));
  blank_screen->buf = buf;
  blank_screen->len = calc_render_area(frame_area);
  // render it
  render(blank_screen, frame_area);
  // clean up
  free(buf);
  free(blank_screen);

  // intro sequence: flash the screen 3 times
  for (int i = 0; i < 3; i++)
  {
    oled_send_cmd(OLED_SET_ENTIRE_ON); // ignore RAM, all pixels on
    sleep_ms(500);
    oled_send_cmd(OLED_SET_ENTIRE_OFF); // go back to following RAM
    sleep_ms(500);
  }

  // render a cute little raspberry
  struct render_area raspberry_area = {
    start_col : 0,
    end_col : IMG_WIDTH - 1,
    start_page : 0,
    end_page : (IMG_HEIGHT - 1) / OLED_PAGE_HEIGHT
  };
  glyph raspberry = {
    len : calc_render_area(&raspberry_area),
    buf : raspberry26x32
  };
  render(&raspberry, &raspberry_area);

  return 0;
}
