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


   spi.c: Low-level SPI routines

*/

#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "config.h"
#include "spi.h"

#define SSP_TFE 0   // Transmit FIFO empty
#define SSP_TNF 1   // Transmit FIFO not full
#define SSP_RNE 2   // Receive FIFO not empty
#define SSP_RFF 3   // Receive FIFO full
#define SSP_BSY 4   // Busy

#if SPI_ON_SSP == 0
#  define SSP_REGS LPC_SSP0
#  define SSP_PCLKREG PCLKSEL1
#  define SSP_PCLKBIT 10
#  define SSP_DMAID_TX 0
#  define SSP_DMAID_RX 1
#elif SPI_ON_SSP == 1
#  define SSP_REGS LPC_SSP1
#  define SSP_PCLKREG PCLKSEL0
#  define SSP_PCLKBIT 20
#  define SSP_DMAID_TX 2
#  define SSP_DMAID_RX 3
#else
#  error "Unknown SSP unit specified!"
#endif

void spi_init(spi_speed_t speed) {
  /* Set clock prescaler to 1:1 */
  BITBAND(LPC_SC->SSP_PCLKREG, SSP_PCLKBIT) = 1;

  /* configure data format - 8 bits, SPI, CPOL=0, CPHA=0, 1 clock per bit */
  SSP_REGS->CR0 = (8-1);

  /* set clock prescaler */
  if (speed == SPI_SPEED_FAST) {
    SSP_REGS->CPSR = SSP_CLK_DIVISOR_FAST;
  } else {
    SSP_REGS->CPSR = SSP_CLK_DIVISOR_SLOW;
  }

  /* Enable SSP */
  SSP_REGS->CR1 = BV(1);

  /* Enable DMA controller, little-endian mode */
  BITBAND(LPC_SC->PCONP, 29) = 1;
  LPC_GPDMA->DMACConfig = 1;
}

void spi_tx_byte(uint8_t data) {
  /* Wait until TX fifo can accept data */
  while (!BITBAND(SSP_REGS->SR, SSP_TNF)) ;

  /* Send byte */
  SSP_REGS->DR = data;
}

uint8_t spi_rx_byte(void) {
  /* Wait until SSP is not busy */
  while (BITBAND(SSP_REGS->SR, SSP_BSY)) ;

  /* Clear RX fifo */
  while (BITBAND(SSP_REGS->SR, SSP_RNE))
    (void) SSP_REGS->DR;

  /* Transmit a single dummy byte */
  SSP_REGS->DR = 0xff;

  /* Wait until answer has been received */
  while (!BITBAND(SSP_REGS->SR, SSP_RNE)) ;

  return SSP_REGS->DR;
}

void spi_tx_block(const void *ptr, unsigned int length) {
  const uint8_t *data = (const uint8_t *)ptr;

  while (length--) {
    /* Wait until TX fifo can accept data */
    while (!BITBAND(SSP_REGS->SR, SSP_TNF)) ;

    SSP_REGS->DR = *data++;
  }
}

void spi_rx_block(void *ptr, unsigned int length) {
  uint8_t *data = (uint8_t *)ptr;
  unsigned int txlen = length;

  /* Wait until SSP is not busy */
  while (BITBAND(SSP_REGS->SR, SSP_BSY)) ;

  /* Clear RX fifo */
  while (BITBAND(SSP_REGS->SR, SSP_RNE))
    (void) SSP_REGS->DR;

  if ((length & 3) != 0 || ((uint32_t)ptr & 3) != 0) {
    /* Odd length or unaligned buffer */
    while (length > 0) {
      /* Wait until TX or RX FIFO are ready */
      while (txlen > 0 && !BITBAND(SSP_REGS->SR, SSP_TNF) &&
             !BITBAND(SSP_REGS->SR, SSP_RNE)) ;

      /* Try to receive data */
      while (length > 0 && BITBAND(SSP_REGS->SR, SSP_RNE)) {
        *data++ = SSP_REGS->DR;
        length--;
      }

      /* Send dummy data until TX full or RX ready */
      while (txlen > 0 && BITBAND(SSP_REGS->SR, SSP_TNF) && !BITBAND(SSP_REGS->SR, SSP_RNE)) {
        txlen--;
        SSP_REGS->DR = 0xff;
      }
    }
  } else {
    /* Clear interrupt flags of DMA channels 0 */
    LPC_GPDMA->DMACIntTCClear = BV(0);
    LPC_GPDMA->DMACIntErrClr  = BV(0);

    /* Set up RX DMA channel */
    LPC_GPDMACH0->DMACCSrcAddr  = (uint32_t)&SSP_REGS->DR;
    LPC_GPDMACH0->DMACCDestAddr = (uint32_t)ptr;
    LPC_GPDMACH0->DMACCLLI      = 0; // no linked list
    LPC_GPDMACH0->DMACCControl  = length
      | (0 << 12) // source burst size 1
      | (0 << 15) // destination burst size 1
      | (0 << 18) // source transfer width 1 byte
      | (2 << 21) // destination transfer width 4 bytes
      | (0 << 26) // source address not incremented
      | (1 << 27) // destination address incremented
      ;
    LPC_GPDMACH0->DMACCConfig = 1 // enable channel
      | (SSP_DMAID_RX << 1) // data source SSP RX
      | (2 << 11) // transfer from peripheral to memory
      ;

    /* Enable RX FIFO DMA */
    SSP_REGS->DMACR = 1;

    /* Write <length> bytes into TX FIFO */
    while (txlen > 0) {
      while (txlen > 0 && BITBAND(SSP_REGS->SR, SSP_TNF)) {
        txlen--;
        SSP_REGS->DR = 0xff;
      }
    }

    /* Wait until DMA channel disables itself */
    while (LPC_GPDMACH0->DMACCConfig & 1) ;

    /* Disable RX FIFO DMA */
    SSP_REGS->DMACR = 0;
  }
}

void spi_set_speed(spi_speed_t speed) {
  /* Wait until TX fifo is empty */
  while (!BITBAND(SSP_REGS->SR, 0)) ;

  /* Disable SSP (FIXME: Is this required?) */
  SSP_REGS->CR1 = 0;

  /* Change clock divisor */
  if (speed == SPI_SPEED_FAST) {
    SSP_REGS->CPSR = SSP_CLK_DIVISOR_FAST;
  } else {
    SSP_REGS->CPSR = SSP_CLK_DIVISOR_SLOW;
  }

  /* Enable SSP */
  SSP_REGS->CR1 = BV(1);
}

void spi_select_device(spi_device_t dev) {
  /* Wait until TX fifo is empty */
  while (!BITBAND(SSP_REGS->SR, 0)) ;

  if (dev == SPIDEV_CARD0 || dev == SPIDEV_ALLCARDS)
    sdcard_set_ss(0);
  else
    sdcard_set_ss(1);

#ifdef CONFIG_TWINSD
  if (dev == SPIDEV_CARD1 || dev == SPIDEV_ALLCARDS)
    sdcard2_set_ss(0);
  else
    sdcard2_set_ss(1);
#endif
}
