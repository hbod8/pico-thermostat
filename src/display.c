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
#define SET_CONTRAST _u(0x81)
#define SET_ENTIRE_OFF _u(0xA4)
#define SET_ENTIRE_ON _u(0xA5)
#define SET_NORM_INV _u(0xA6)
#define SET_DISP _u(0xAE)
#define SET_MEM_ADDR _u(0x20)
#define SET_COL_ADDR _u(0x21)
#define SET_PAGE_ADDR _u(0x22)
#define SET_DISP_START_LINE _u(0x40)
#define SET_SEG_REMAP _u(0xA0)
#define SET_MUX_RATIO _u(0x3F)
#define SET_COM_OUT_DIR _u(0xC0)
#define SET_DISP_OFFSET _u(0xD3)
#define SET_COM_PIN_CFG _u(0x12)
#define SET_DISP_CLK_DIV _u(0xD5)
#define SET_PRECHARGE _u(0xD9)
#define SET_VCOM_DESEL _u(0xDB)
#define SET_CHARGE_PUMP _u(0x8D)
#define SET_HORIZ_SCROLL _u(0x26)
#define SET_SCROLL _u(0x2E)

#define ADDR _u(0x3C)
#define HEIGHT _u(64)
#define WIDTH _u(128)
#define PAGE_HEIGHT _u(8)
#define NUM_PAGES HEIGHT / PAGE_HEIGHT
#define BUF_LEN (NUM_PAGES * WIDTH)

#define WRITE_MODE _u(0xFE)
#define READ_MODE _u(0xFF)

#define SET_BIT(x, pos, in) (x |= ((unsigned)in << pos))
#define GET_BIT(x, pos) (x & (1UL << pos) )

/**
 * Glyph object.  Note: this will be displayed with bits written R -> L.
*/
typedef struct glyph
{
  int width;
  int height;
  uint8_t *buf;
} glyph;

void dump_buffer(uint8_t *buf, int pages, int segments)
{
  printf("dumping buffer: %d pages x %d segments\n", pages, segments);
  for (int i = 0; i < pages; i++)
  {
    for (int j = 0; j < 8; j++)
    {
      for (int k = 0; k < segments; k++)
      {
        printf("%s", buf[i * segments + k] & 0x01 << j ? "X" : " ");
      }
      printf("\n");
    }
  }
}

void dump_glyph(glyph *gly)
{
  printf("dumping glyph: %d x %d\n", gly->width, gly->height);
  for (int i = 0; i < gly->height; i++)
  {
    for (int j = 0; j < (gly->width / 8); j++)
    {
      for (int k = 0; k < 8; k++)
      {
        printf("%s", (gly->buf[i * (gly->width / 8) + j] >> k) & 0x01 ? "X" : " ");
      }
    }
    printf("\n");
  }
}

/*
 a note about I2C data transmission:
 every transmission to the receiver begins with a control after the recipient address:
 (C0) (D/C#) 0 0 0 0 0 0
 C0 = continuation byte, if 0 then the rest of the transmission is only data bytes
 D/C# = data or command byte, if 0 then the following data bytes should be interpreted as commands
        if 1 then the following data should be written to GDDRAM and displayed on the screen
 */

/**
 * writes given buffer to the device
 * 
 * @param buffer byte buffer to be written to device
 * @param buffer_len length of buffer
 * @param command if this is a command or data to be written GDDRAM
*/
void write_buffer(uint8_t *buffer, int buffer_len, bool command)
{
  uint8_t *buffer_with_control_byte = malloc(buffer_len + 1); // allocate a temporary buffer to add control byte at start
  buffer_with_control_byte[0] = command ? 0x00 : 0x40; // set command bit with continuation bit off for buffer
  memcpy(&buffer_with_control_byte[1], buffer, buffer_len); // copy in the old buffer
  i2c_write_blocking(i2c_default, (ADDR | 0x00), buffer_with_control_byte, buffer_len, false); // write to device
  free(buffer_with_control_byte); // free memory
}

/**
 * writes given byte to the device
 * 
 * @param buffer byte buffer to be written to device
 * @param buffer_len length of buffer
 * @param command if this is a command or data to be written GDDRAM
*/
void write_byte(uint8_t byte, bool command)
{
  uint8_t message[2]; // create a array to add control byte
  message[0] = command ? 0x80 : 0xC0; // set command bit with continuation bit on for single byte
  message[1] = byte; // set byte
  i2c_write_blocking(i2c_default, (ADDR | 0x00), message, 2, false); // write to device
}

/**
 * send a single command byte
 * 
 * @param cmd the command byte to be sent
*/
void send_cmd(uint8_t cmd)
{
  write_byte(cmd, true);
}

/**
 * send a data buffer
 * 
 * @param buffer buffer to be written GDDRAM
 * @param buffer_len buffer length
*/
void write_data_buffer(uint8_t *buffer, int buffer_len)
{
  write_buffer(buffer, buffer_len, false);
}

/**
 * Write a buffer to a specific location on the screen.  Pages are groups of rows 8 pixels high.
 * The least significant bit gets written to the highest pixel.  Segments are the individual groups
 * of 8 pixels.
 * 
 * @param buffer the buffer to be written
 * @param start_page the starting page
 * @param end_page the ending page
 * @param start_segment the starting segment
 * @param end_segment the ending segment
*/
void write_buffer_to(uint8_t *buffer, uint8_t start_page, uint8_t end_page, uint8_t start_segment, uint8_t end_segment)
{
  send_cmd(SET_PAGE_ADDR);
  send_cmd(start_page);
  send_cmd(end_page);

  send_cmd(SET_COL_ADDR);
  send_cmd(start_segment);
  send_cmd(end_segment);

  write_data_buffer(buffer, (end_page - start_page) * (end_segment - start_segment));
}

/**
 * Write a glyph at a location (top left is 0,0, increasing to the left and down).
 * Glyphs should be stored as right -> left bits.
 * 
 * @param gly the glyph to be displayed
 * @param x the x position to render the glyph (i.e. column starting at 0 at the right and increasing)
 * @param y the y position to render the glyph (i.e. row starting at 0 at the top and increasing)
*/
void write_glyph(glyph *gly, uint16_t x, uint16_t y)
{
  uint8_t *buffer = malloc((gly->height / PAGE_HEIGHT) * gly->width); // allocate buffer to be displayed
  // crazy stuff
  for (int i = 0; i < (gly->height / PAGE_HEIGHT); i++) // buffer page
  {
    for (int j = 0; j < PAGE_HEIGHT; j++) // glyph every 8 row, buffer shift
    {
      for (int k = 0; k < (gly->width / 8); k++) // glyph byte, buffer every 8 segments
      {
        for (int m = 0; m < 8; m++) // glyph shift, every buffer segment
        {
          // gly->buf[] >> 7 - m // get glyph MSB 0x01
          // buffer[] |= (gly MSB 0x01) << m // set buffer LSB
          // buffer[(i * PAGE_HEIGHT) + (k * 8) + m] |= (gly->buf[(j * 8) + (i * PAGE_HEIGHT) + k] >> 7u - m) << j; // combine into byte reversal
          SET_BIT(buffer[(i * PAGE_HEIGHT) + (k * 8) + m], 7u - m, GET_BIT(gly->buf[(j * 8) + (i * PAGE_HEIGHT) + k], j));
        }
      }
    }
  }
  dump_buffer(buffer, (gly->height / PAGE_HEIGHT), gly->width);
  // phew, that was alot, now lets write it!
  write_buffer_to(buffer, y / PAGE_HEIGHT, (gly->height + y) / PAGE_HEIGHT, x, x + gly->width);
}

/**
 * Clears all memory in the GDDRAM.
*/
void clear_gddram()
{
  uint8_t *buf = calloc(WIDTH * PAGE_HEIGHT, sizeof(uint8_t)); // create zeroed buffer
  write_data_buffer(buf, WIDTH * PAGE_HEIGHT); // write buffer
  free(buf); // clean up
}

/**
 * Initalizes screen with manufacturer defaults.
*/
void init()
{
  send_cmd(SET_DISP | 0x00); // set display off
  /* memory mapping */
  send_cmd(SET_MEM_ADDR | 0x00); // set memory address mode to horizontal addressing mode
  /* resolution and layout */
  send_cmd(SET_DISP_START_LINE | 0x00); // set display start line to 0
  send_cmd(SET_SEG_REMAP | 0x01); // set segment re-map
  // column address 127 is mapped to SEG0
  send_cmd(SET_MUX_RATIO); // set multiplex ratio
  send_cmd(HEIGHT - 1);    // our display is 64 pixels high
  send_cmd(SET_COM_OUT_DIR | 0x08); // set COM (common) output scan direction
  // scan from bottom up, COM[N-1] to COM0
  send_cmd(SET_DISP_OFFSET); // set display offset
  send_cmd(0x00);                 // no offset
  send_cmd(SET_COM_PIN_CFG); // set COM (common) pins hardware configuration
  send_cmd(0x12);                 // manufacturer magic number
  /* timing and driving scheme */
  send_cmd(SET_DISP_CLK_DIV); // set display clock divide ratio
  send_cmd(0x80);                  // div ratio of 1, standard freq
  send_cmd(SET_PRECHARGE); // set pre-charge period
  send_cmd(0xF1);               // Vcc internally generated on our board
  send_cmd(SET_VCOM_DESEL); // set VCOMH deselect level
  send_cmd(0x30);                // 0.83xVcc
  /* display */
  send_cmd(SET_CONTRAST); // set contrast control
  send_cmd(0xFF);
  send_cmd(SET_ENTIRE_ON); // set entire display on to follow RAM content
  send_cmd(SET_NORM_INV); // set normal (not inverted) display
  send_cmd(SET_CHARGE_PUMP); // set charge pump
  send_cmd(0x14);                 // Vcc internally generated on our board
  send_cmd(SET_SCROLL | 0x00); // deactivate horizontal scrolling if set
  send_cmd(SET_DISP | 0x01); // turn display on
}

