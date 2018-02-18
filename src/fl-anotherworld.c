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


   fl-anotherworld.c: High level handling of Another World's (C64) loader

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


/** Get byte from host.
  * Bytes are received over DATA one bit at a time on every CLK edge, MSb first.
  * @return the byte
  */
static uint8_t get_byte(void) { // $3de
  uint8_t b = 0, i;
  for (i = 0; i < 4; i++) {
    while ((iec_bus_read() & IEC_BIT_CLOCK)) ;
    b = (b << 1) | ((iec_bus_read() & IEC_BIT_DATA) ? 0 : 1);
    while (!(iec_bus_read() & IEC_BIT_CLOCK)) ;
    b = (b << 1) | ((iec_bus_read() & IEC_BIT_DATA) ? 0 : 1);
  }
  return b;
}


/** Send byte to host.
  * Bytes are sent over DATA one bit at a time on every CLK edge MSb first.
  */
static void put_byte(uint8_t b) { // $12e
  uint8_t i;
  for (i = 0; i < 4; i++) {
    set_data(!(b & 0x80));
    b <<= 1;
    while ((iec_bus_read() & IEC_BIT_CLOCK)) ;
    set_data(!(b & 0x80));
    b <<= 1;
    while (!(iec_bus_read() & IEC_BIT_CLOCK)) ;
  }
}


/** Read BAM (= JSR $D042, do nothing for now).
  *
  * Not sure this is actually needed. It's called during the $366
  * (sector chain read) routine after all sectors are read and uploaded
  * to the C64.
  */
static void read_bam(void) {
  // nop
}


/** Copy page at $0700 to $bbaa.
  * @buf: array of page buffers (mapping $0400.. $07ff)
  * @a  : lo byte of destination address
  * @b  : hi byte of destination address
  */
static void copy_page(buffer_t *buf[], uint8_t a, uint8_t b) {
  // We should only expect b between 4 and 7, bail out otherwise.
  // Also, 7 is pointless (copy to itself)
  if (b < 4 || b > 6) {
    return;
  }
  memcpy(buf[b-4]->data, buf[3]->data, 256);
}


/** Read sector chain and send to host
  * @buf: array of page buffers (mapping $0400.. $07ff)
  * @a  : track
  * @b  : sector
  */
static void read_sector_chain(buffer_t *buf[], uint8_t a, uint8_t b) {
  uint8_t i;
  while (a) {
    set_clock(0); // signal busy reading sector
    read_sector(buf[3], current_part, a, b);
    set_clock(1); // release CLK
    i = 2;
    do {
      put_byte(buf[3]->data[i]);
      i++;
    } while (i);
    a = buf[3]->data[0];
    b = buf[3]->data[1];
    put_byte(a);
  } 
  read_bam();
}


/** Upload page
  * @buf: array of page buffers (mapping $0400.. $07ff)
  * @a  : lo byte of page addr
  * @b  : hi byte of page addr
  */
static void upload_page(buffer_t *buf[], uint8_t a, uint8_t b) {
  uint8_t i = 0;
  // We should only expect b between 4 and 7, bail out otherwise
  if (b < 4 || b > 7) {
    return;
  }

  do {
    put_byte(buf[b-4]->data[i]);
    i++;
  } while (i);
}


/** Download page from host into $700
  * @buf: array of page buffers (mapping $0400.. $07ff)
  * @a  : lo byte of page addr
  * @b  : hi byte of page addr
  */
static void download_page(buffer_t *buf[], uint8_t a, uint8_t b) {
  uint8_t i = 0;
  do {
    buf[3]->data[i] = get_byte();
    i++;
  } while (i);
}


/** Upload drive control register ($1c00).
  * Used by Another World to check for disk write protection.
  * Currently hardcoded to $10 = disk is write unprotected.
  */
static void send_drive_control_reg(void) {
  put_byte(0x10);
}


#if 0
/** Display error by blinking the busy/dirty leds 8 times
  * (green = 1, red = 0), MSb first.
  *
  * Really crappy debugging aid for sd2iecs that don't have their
  * UART pins accessible because they are glued shut.
  *
  * @c: whatever value you want to display
  */
static void debug_blink(uint8_t c) {
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
#endif


void load_anotherworld(UNUSED_PARAMETER) {
  buffer_t *buf[4];
  uint8_t i;
  uint8_t a, b, c;  // C = command, A/B = parameters (track/sector or hi/lo pointer)

  delay_ms(500);  // time for the 1541 to read 2 sectors

  /* AW loader uses pages $04 .. $07 to buffer disk data (eg. sprites), then
   * transfers them all at once so let's allocate one buffer per page */
  for (i = 0; i < 4; i++) {
    buf[i] = alloc_system_buffer();
  }

  /* Main loop ($300) */
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    for (;;) {

      set_busy_led(0);

      /* $309 .. $313: Wiggle DATA, this tells the C64 we're ready.
       * Stop when CLOCK becomes active. This tells us the C64 is ready */
      do {
        delay_us(10); // 14 cycles, but shorter (10 + loop overhead) is okay
        set_data(! (iec_bus_read() & IEC_BIT_DATA));
      } while ((iec_bus_read() & IEC_BIT_CLOCK));

      set_data(1);

      /* Wait for incoming command, ie. CLOCK goes inactive ($31d .. $322) */
      while (!(iec_bus_read() & IEC_BIT_CLOCK)) {
        /* ATN pulled down = reset, exit the loader */
        if (!(iec_bus_read() & IEC_BIT_ATN)) {
          goto done;
        }
      }

      /* Read incoming command */
      set_busy_led(1);

      a = get_byte();
      b = get_byte();
      c = get_byte();

  #if 0 
      /* Output command for debugging purposes */
      uart_puthex(a);
      uart_putc(' ');
      uart_puthex(b);
      uart_putc(' ');
      uart_puthex(c);
      uart_putcrlf();
  #endif

      switch(c / 2) {
        case 0: // $366 read sector chain (~ file) starting at track/sector A/B
          read_sector_chain(buf, a, b);
          break;

        case 1: // $38f read track/sector A/B into $700
          read_sector(buf[3], current_part, a, b);
          break;

        case 2: // $3a6 copy $700 page to $BBAA
          copy_page(buf, a, b);
          break;

        case 3: // $3bb write track/sector A/B from $700
          set_dirty_led(1);
          write_sector(buf[3], current_part, a, b);
          set_dirty_led(0);
          break;

        case 4: // $3b1 bump head
          // yeah ok done
          break;

        case 5: // $38b read BAM
          read_bam();
          break;

        case 6: // $3d2 download page from host into $700
          download_page(buf, a, b);
          break;

        case 7: // $11c upload page at $BBAA to host
          upload_page(buf, a, b);
          break;

        case 8: // $128 read $1c00 (VIA 2 drive control) and send to host
          send_drive_control_reg();
          break;

        case 9: // $119 reset 1541, unload code
          goto done;

        default:
          goto error;
      }
    }
  }

error:
  set_error(ERROR_UNKNOWN_DRIVECODE);
done:
  set_data(1);
  set_clock(1);
  for (i = 0; i < 4; i++) {
    free_buffer(buf[i]);
  }

  /* Visual cue that the loader is gone */
  set_dirty_led(1);
  set_busy_led(1);
  delay_ms(200);
  set_dirty_led(0);
  set_busy_led(0);
}
