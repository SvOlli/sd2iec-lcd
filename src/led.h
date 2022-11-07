/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2022  Ingo Korb <ingo@akana.de>

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


   led.h: Definitions for the LEDs

*/

#ifndef LED_H
#define LED_H

#include "config.h"
#include "uart.h"

/* LED-to-bit mapping - BUSY/DIRTY are only used for SINGLE_LED */
#define LED_ERROR      1
#define LED_BUSY       2
#define LED_DIRTY      4

extern volatile uint8_t led_state;

/* Update the LEDs to match the buffer state */
void update_leds(void);

/* Wrapped in do..while to avoid "ambigious else" warnings */
#ifdef SINGLE_LED
#  define set_dirty_led(x) do{if (x) { led_state |= LED_DIRTY; } else { led_state &= (uint8_t)~LED_DIRTY; }}while(0)
#  define set_busy_led(x)  do{if (x) { led_state |= LED_BUSY ; } else { led_state &= (uint8_t)~LED_BUSY ; }}while(0)
#  define set_error_led(x) do{if (x) { led_state |= LED_ERROR; } else { led_state &= (uint8_t)~LED_ERROR; }}while(0)
#else
static inline __attribute__((always_inline)) void set_error_led(uint8_t state) {
  if (state) {
    led_state |= LED_ERROR;
  } else {
    led_state &= ~LED_ERROR;
    update_leds();
  }
}
#endif

#endif
