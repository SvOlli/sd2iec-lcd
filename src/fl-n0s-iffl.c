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


   fl-n0sdos.c: High level handling of N0stalgia fastloaders

*/

#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "d64ops.h"
#include "diskchange.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fastloader-ll.h"
#include "fileops.h"
#include "iec-bus.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "fastloader.h"
#include "wrapops.h"


#define ATN_DATA_CLOCK_INACTIVE ((iec_bus_read() & (IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_ATN)) == (IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_ATN))
#define DATA_ACTIVE (!(iec_bus_read() & IEC_BIT_DATA))
#define CLOCK_ACTIVE (!(iec_bus_read() & IEC_BIT_CLOCK))
#define ATN_ACTIVE (!(iec_bus_read() & IEC_BIT_ATN))
#define SET_CLOCK_ACTIVE set_clock(0);
#define SET_DATA_ACTIVE set_data(0);
#define RELEASE_CLOCK_AND_DATA set_clock(1); set_data(1);

// -------------------------------------------------------------
// Scanner routine
// -------------------------------------------------------------

void scan_n0s_iffl(UNUSED_PARAMETER) {
  // Only d64 images supported, exit otherwise
  if (partition[current_part].fop != &d64ops) {
    set_error(ERROR_IMAGE_INVALID);
    return;
  }

  // Allocate 1 buffer for single sector reads
  buffer_t *buf = alloc_system_buffer();

  // Vfile LBA tables LO/HI ($590/$660)
  // N0SDOS uploads a "vfile LBA" table with the starting sectors for each vfile
  // laid out as two actual uint8_t[208] tables. The scanner's job is to translate
  // this LBA table into actual track/sector by following the container's sector
  // chain.
  uint8_t * const vfile_lba_lo = find_buffer(BUFFER_SYS_CAPTURE1)->data;
  uint8_t * const vfile_lba_hi = find_buffer(BUFFER_SYS_CAPTURE2)->data;

  // Sector LO/HI double as vfile start sector/track, use aliases for clarity
  uint8_t * const vfile_start_sector = vfile_lba_lo;
  uint8_t * const vfile_start_track  = vfile_lba_hi; 

  // C64 opens IFFL container and reads the first byte so that the scanner
  // can locate its first track/sector by looking at $18/$19
  uint8_t t = d64_lastread.track;
  uint8_t s = d64_lastread.sector;

  uint8_t vfile_index = 0;
  uint8_t sector_count = 0;

  // Follow the chain and translate "vfile LBA" into physical track/sector
  while (t) {
    read_sector(buf, current_part, t, s);
    if (sector_count == ((vfile_lba_hi[vfile_index] << 8) | vfile_lba_lo[vfile_index])) {
      vfile_start_track[vfile_index] = t;
      vfile_start_sector[vfile_index] = s;
      vfile_index++;
    }
    t = buf->data[0];
    s = buf->data[1];
    sector_count++;
  }
  free_buffer(buf);
}


// -------------------------------------------------------------
// Loader routine + helper functions
// -------------------------------------------------------------

/** Get byte from host.
  * Bytes are received over DATA one bit at a time on every CLK edge, MSb first.
  * @return the byte
  */
static uint8_t get_byte(void) {
  uint8_t b = 0, i;

  RELEASE_CLOCK_AND_DATA;

  for (i = 0; i < 8; i++) {
    while (ATN_DATA_CLOCK_INACTIVE) ;
    if (ATN_ACTIVE) return 0;
    b <<= 1;
    if (CLOCK_ACTIVE) {
      b |= 1;
      SET_DATA_ACTIVE;
      while (CLOCK_ACTIVE) ;
    } else {
      SET_CLOCK_ACTIVE;
      while (DATA_ACTIVE) ;
    }
    RELEASE_CLOCK_AND_DATA;
  }
  return b;
}



static void vfile_read_sector(buffer_t *buf, uint8_t t, uint8_t s, uint8_t o) {
  read_sector(buf, current_part, t, s);
  buf->pvt.d64.track  = t;      // remember t/s in case we have to write it back
  buf->pvt.d64.sector = s;
  buf->position       = o + 2;  // offset within sector (skips t/s header)
}


static void vfile_open(buffer_t *buf, uint8_t t, uint8_t s, uint8_t o) {
  vfile_read_sector(buf, t, s, o);
}


static uint8_t vfile_read_byte(buffer_t *buf) {
  if (buf->position == 0) // wrapped around? get next sector
    vfile_read_sector(buf, buf->data[0], buf->data[1], 0);
  return buf->data[buf->position++];
}


static void vfile_write_byte(buffer_t *buf, uint8_t b) {
  if (buf->position == 0) { // wrapped around? write sector and get the next in chain
    write_sector(buf, current_part, buf->pvt.d64.track, buf->pvt.d64.sector);
    vfile_read_sector(buf, buf->data[0], buf->data[1], 0);
  }
  buf->data[buf->position++] = b;
}


void load_n0s_iffl(UNUSED_PARAMETER) {
  // Release both clock and data
  set_data(1);
  set_clock(1);

  // Vfile start sector/track
  uint8_t * const vfile_start_sector = find_buffer(BUFFER_SYS_CAPTURE1)->data;
  uint8_t * const vfile_start_track  = find_buffer(BUFFER_SYS_CAPTURE2)->data;
  uint8_t * const vfile_offset       = find_buffer(BUFFER_SYS_CAPTURE3)->data;

  // Allocate 1 buffer for single sector reads/writes
  buffer_t *buf = alloc_system_buffer();

  for (;;) {
    // Get command
    uint8_t c = get_byte();
    if (ATN_ACTIVE)
      goto end;
  
    uint8_t vi = c; // vfile index
    if (vi >= 0xe0) {
      vi &= 0x1f;  // write vfile
    }

    // Open vfile
    vfile_open(buf, vfile_start_track[vi], vfile_start_sector[vi], vfile_offset[vi]);
    uint16_t size = vfile_read_byte(buf) << 8 | vfile_read_byte(buf);

    // C64 expects the buffer's position after reading the vfile size
    n0s_iffl_put_byte(buf->position);

    for ( ; size; size++) { // the size is actually complemented so let's ++ not --
      if (c < 0xe0) {       // read
        n0s_iffl_put_byte(vfile_read_byte(buf));
      } else {              // write
        vfile_write_byte(buf, get_byte());
      }
    }
  }

end:
  free_buffer(buf);
}