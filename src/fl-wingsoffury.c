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

#ifdef __AVR__
# include <avr/boot.h>
#endif
#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "atomic.h"
#include "buffers.h"
#include "diskchange.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fastloader-ll.h"
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


/** Sync, then get one byte from host ($472)
  * @return: the byte
  */
static uint8_t sync_and_get_byte(void) {
  wof_sync();
  return wof_get_byte();
}


/** Sync, then get one byte to host ($4c8)
  * @b: the byte
  */
static void sync_and_put_byte(uint8_t b) {
  wof_sync();
  wof_put_byte(b);
}


/** Read sector and send to host
  * @buf: page buffer
  * @t  : track
  * @s  : sector
  */
static void read_and_tx_sector(buffer_t *buf, uint8_t t, uint8_t s) {
  uint8_t i = 0;
  read_sector(buf, current_part, t, s);
  sync_and_put_byte(0x01); // read status 1=ok, 4/5=err
  wof_sync();
  do {
    wof_put_byte(buf->data[i]);
    i++;
  } while (i);
}


/** Read sector chain and send to host
  * @buf: page buffer
  * @a  : track
  * @b  : sector
  */
static void read_and_tx_sector_chain(buffer_t *buf, uint8_t t, uint8_t s) {
  uint8_t i = 0;
  while (t) {
    read_sector(buf, current_part, t, s);
    wof_sync();
    do {
      wof_put_byte(buf->data[i]);
      i++;
    } while (i);
    t = buf->data[0];
    s = buf->data[1];
  } 
}


/** Receive and write sector
  * @buf: page buffers
  * @t  : track
  * @s  : sector
  */
static void rx_and_write_sector(buffer_t *buf, uint8_t t, uint8_t s) {
  uint8_t i = 0, chk, b;
  uint8_t *ptr;
  do {
    chk = 0;
    ptr = buf->data;
    wof_sync();
    do {
      b = wof_get_byte();
      chk ^= b;
      *ptr = b;
      ptr++;
      i++;
    } while (i);
    sync_and_put_byte(chk);
    b = sync_and_get_byte(); // get ack from c64
  } while (b != 0x89);

  write_sector(buf, current_part, t, s);
  sync_and_put_byte(1); // write okay
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


/**
  * Wings of Fury loader
  * This is for the PAL import version. The loader is credited to
  * "Andrew Software".
  */
void load_wingsoffury(UNUSED_PARAMETER) {
  buffer_t *buf;
  uint8_t t, s, c;  // c = command, t/s = track/sector
  uint8_t chk; // A^B^C checksum

#if defined __AVR_ATmega644__   || \
    defined __AVR_ATmega644P__  || \
    defined __AVR_ATmega1284P__ || \
    defined __AVR_ATmega1281__
  /* Lock out clock sources that aren't stable enough for this protocol */
  uint8_t tmp = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS) & 0x0f;
  if (tmp == 2) {
    set_error(ERROR_CLOCK_UNSTABLE);
    return;
  }
#endif

  /* WoF uses only one buffer on the 1541 side */
  buf = alloc_system_buffer();

  set_data(1);
  set_clock(1);

#if 0
  // Visual cue to start scope/logic analyzer capture
  set_dirty_led(1); set_busy_led(1);
  delay_ms(1000);
  set_dirty_led(0); set_busy_led(0);
#endif

  /* Main loop ($300) */
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    for (;;) {

      set_busy_led(0);

      /* Read incoming command or exit loader if C64 reset (ATN ACTIVE) */
      wof_sync();
      if (!(iec_bus_read() & IEC_BIT_ATN)) {
        goto done;
      }
      c = wof_get_byte();

      /* Read the remaining command bytes t, s and checksum */
      t = sync_and_get_byte();
      s = sync_and_get_byte();
      chk = sync_and_get_byte();

      set_busy_led(1);

      if (chk == (t ^ s ^ c)) {
        sync_and_put_byte(0x89); // ACK
      } else {
        sync_and_put_byte(0xa1); // NAK, expect retry
        continue;
      }

      switch (c) {
        case 0:
          read_and_tx_sector(buf, t, s);
          break;
        case 1:
          set_dirty_led(1);
          rx_and_write_sector(buf, t, s);
          set_dirty_led(0);
          break;
        case 2:
          read_and_tx_sector_chain(buf, t, s);
          break;
        default:
          if (c & 0x80) {
            goto done;
          }
          goto error;
      }
    }
  }

error:
  set_error(ERROR_UNKNOWN_DRIVECODE);
done:
  set_data(1);
  set_clock(1);
  free_buffer(buf);

  /* Visual cue that the loader is gone */
  set_dirty_led(1);
  set_busy_led(1);
  delay_ms(200);
  set_dirty_led(0);
  set_busy_led(0);
}
