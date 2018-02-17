/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>
   Final Cartridge III, DreamLoad, ELoad fastloader support:
   Copyright (C) 2008-2011  Thomas Giesel <skoe@directbox.com>
   Nippon Loader support:
   Copyright (C) 2010  Joerg Jungermann <abra@borkum.net>

   Inspired by MMC2IEC by Lars Pontoppidan et al.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   fl-wingsoffury.c: High level handling of Wind of Fury's (C64) loader

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "diskchange.h"
#include "doscmd.h"
#include "errormsg.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "wrapops.h"
#include "fastloader.h"


#define WHILE_DATA_ACTIVE while (!(iec_bus_read() & IEC_BIT_DATA))
#define WHILE_DATA_INACTIVE while (iec_bus_read() & IEC_BIT_DATA)
#define WHILE_CLOCK_ACTIVE while (!(iec_bus_read() & IEC_BIT_CLOCK))
#define WHILE_CLOCK_INACTIVE while (iec_bus_read() & IEC_BIT_CLOCK)


/** Display error using the busy/dirty leds.
  * 8 blinks = 8 bits MSb first, green = 1, red = 0.
  * Really crappy debugging aid for sd2iecs that don't have their
  * UART pins accessible because they are glued shut like mine.
  *
  * @buf: array of page buffers (mapping $0400.. $07ff)
  * @a  : lo byte of page addr
  * @b  : hi byte of page addr
  */
static void aw_debug(uint8_t c) {
  set_busy_led(1);
  set_dirty_led(1);
  delay_ms(500);
  for (uint8_t m = 0x80; m; m >>= 1) {
    if (c & m) {
      set_busy_led(1); set_dirty_led(0);
    } else {
      set_busy_led(0); set_dirty_led(1);
    }
    delay_ms(750);
    set_busy_led(0);
    set_dirty_led(0);
    delay_ms(250);
  }
}

/** Sync with the C64 ($44c)
  * TODO: check ATN/keys and get out of the loader if so
  */
static void sync(void) {
  set_clock(1); // release CLOCK
  set_data(1); // release DATA
  delay_us(10);
  WHILE_DATA_ACTIVE;
  set_clock(0); // CLOCK ACTIVE
  WHILE_DATA_INACTIVE;
}


/** Get byte from host ($472)
  * @return the byte
  */
static uint8_t get_byte(void) {
  uint8_t b = 0;
  sync();
  WHILE_DATA_ACTIVE;
  set_data(1); // release DATA
  set_clock(1); // release CLOCK
  delay_us(18); // 18 cycles
  b |= (iec_bus_read() & IEC_BIT_CLOCK) ? 0 : 1;
  b |= (iec_bus_read() & IEC_BIT_DATA) ? 0 : 2;
  delay_us(11);
  b |= (iec_bus_read() & IEC_BIT_CLOCK) ? 0 : 4;
  b |= (iec_bus_read() & IEC_BIT_DATA) ? 0 : 8;
  delay_us(11);
  b |= (iec_bus_read() & IEC_BIT_CLOCK) ? 0 : 16;
  b |= (iec_bus_read() & IEC_BIT_DATA) ? 0 : 32;
  delay_us(11);
  b |= (iec_bus_read() & IEC_BIT_CLOCK) ? 0 : 64;
  b |= (iec_bus_read() & IEC_BIT_DATA) ? 0 : 128;
  set_clock(0); // keep CLOCK active (= busy)
  return b;
}


static void put_single_byte(uint8_t b) {
  set_data(1);
  set_clock(1); // release both
  WHILE_DATA_ACTIVE;

  set_clock(!(b & 1));
  set_data( !(b & 2));
  delay_us(19); // 19
  set_clock(!(b & 4));
  set_data( !(b & 8));
  delay_us(10); // 12
  set_clock(!(b & 16));
  set_data( !(b & 32));
  delay_us(11); // 13
  set_clock(!(b & 64));
  set_data( !(b & 128));

  delay_us(10); // make sure the last two bits are latched
}


/** Send byte to host ($4c8)
  * @b: the byte
  */
static void sync_and_put_byte(uint8_t b) {
  sync();
  put_single_byte(b);
}


/** Read sector and send to host
  * @buf: page buffer
  * @a  : track
  * @b  : sector
  */
static void read_and_tx_sector(buffer_t *buf, uint8_t a, uint8_t b) {
  uint8_t i = 0;
  read_sector(buf, current_part, a, b);
  sync_and_put_byte(0x01); // read status 1=ok, 4/5=err
  sync();
  do {
    put_single_byte(buf->data[i]);
    i++;
  } while (i);
}


/** Read sector chain and send to host
  * @buf: page buffer
  * @a  : track
  * @b  : sector
  */
static void read_and_tx_sector_chain(buffer_t *buf, uint8_t a, uint8_t b) {
  uint8_t i = 0;
  while (a) {
    read_sector(buf, current_part, a, b);
    sync();
    do {
      put_single_byte(buf->data[i]);
      i++;
    } while (i);
    a = buf->data[0];
    b = buf->data[1];
  } 
}


/** Receive and write sector
  * @buf: page buffers
  * @a  : track
  * @b  : sector
  */
static void rx_and_write_sector(buffer_t *buf, uint8_t a, uint8_t b) {
  uint8_t i = 0;
  do {
    buf->data[i] = get_byte();
    i++;
  } while (i);
  write_sector(buf, current_part, a, b);
}


void load_wingsoffury(UNUSED_PARAMETER) {
  buffer_t *buf;
  uint8_t a, b, c;  // C = command, A/B = parameters (track/sector or hi/lo pointer)
  uint8_t x; // A^B^C checksum

  /* WoF uses only one page */
  buf = alloc_system_buffer();

  set_data(1);
  set_clock(1);

  // VISUAL SIGNAL TO START SAMPLING DATA ON THE SCOPE
  //set_dirty_led(1); set_busy_led(1);
  //delay_ms(1000);
  //set_dirty_led(0); set_busy_led(0);

  //set_dirty_led(1);

  /* Main loop ($300) */
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    for (;;) {

      set_busy_led(0);

      /* Read incoming command */
      c = get_byte();

      //aw_debug(a);

      a = get_byte();

      //aw_debug(b);

      b = get_byte();

      //aw_debug(c);

      x = get_byte();

      //aw_debug(x);

      set_busy_led(1);

      delay_ms(1);

      if (x == (a ^ b ^ c)) {
        sync_and_put_byte(0x89);
        //aw_debug(0x80);
      } else {
        sync_and_put_byte(0xa1);
        //aw_debug(0x40);
        continue;
      }

      switch (c) {
        case 0:
          //aw_debug(0x20);
          read_and_tx_sector(buf, a, b);
          break;
        case 1:
          //aw_debug(0x10);
          set_dirty_led(1);
          rx_and_write_sector(buf, a, b);
          set_dirty_led(0);
          break;
        case 2:
          //aw_debug(0x08);
          read_and_tx_sector_chain(buf, a, b);
          break;
        default:
          //aw_debug(0x04);
          if (c & 0x80) {
            //aw_debug(0x02);
            goto done;
          }
          goto error;
      }
    }
  }

error:
  aw_debug(c);
  set_error(ERROR_UNKNOWN_DRIVECODE);
done:
  set_data(1);
  set_clock(1);
  free_buffer(buf);
}
