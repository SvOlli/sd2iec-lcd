/*
   SD2IEC LCD - SD/MMC to Commodore IEC bus controller with LCD support.
   Created 2008,2009 by Sascha Bader <sbader01@hotmail.com>

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

   display_lcd.h: lcd display routines and data
*/

#include "display_lcd.h"
#include <avr/pgmspace.h>

#include "uart.h"
#include <util/delay.h>

uint8_t buffer_msg[3];
uint8_t fs_mode;
uint8_t lcdcontrast;  // andi6510: global LCD contrast setting


static const char  mychars[64] PROGMEM = {
			0x00, 0x01, 0x07, 0x0F, 0x0F, 0x1E, 0x1C, 0x1C,
			 0x1F, 0x1F, 0x1F, 0x11, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x1F, 0x1E, 0x1C, 0x00,
			 0x1C, 0x1E, 0x0F, 0x0F, 0x07, 0x01, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x11, 0x1F, 0x1F, 0x1F, 0x00,
			 0x1C, 0x1E, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x05, 0x07, 0x02, 0x00, 0x00,
			0x00, 0x1B, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00
	 };

#ifdef EEE
static const char eee1[16] PROGMEM  = {
 0x20, 0x06, 0x07, 0x06, 0x20, 0x20, 0x08, 0x01, 0x02, 0x06, 0x07, 0x06, 0x20, 0x07, 0x20, 0x20
};

static const char eee2[16] PROGMEM  = {
 0x20, 0x06, 0x20, 0x07, 0x06, 0x20, 0x03, 0x04, 0x05, 0x20, 0x06, 0x07, 0x06, 0x20, 0x06, 0x20
};
#endif

void lcd_clrline(int line)
{
	if (lcd_controller_type() != 0)
	{
		lcd_gotoxy(0,line);

		for (int i=0; i < MAXLINELENGHT; i++)
		{
    		lcd_putc(0x20);
		}
		lcd_gotoxy(0,line);
	}
}


void lcd_cmdseq(char * cmdseq)
{
	if (lcd_controller_type() != 0)
	{
		lcd_command(cmdseq[0]);
		lcd_puts(cmdseq+1);
	}
}

void lcd_setCustomChars()
{
	char * progmem_s;

	if (lcd_controller_type() != 0)
	{
		lcd_command(0x40);  // write to CGRAM
		progmem_s  = (char *) mychars;
		for (int i=0;i<64;i++)
		{
		  _delay_us(64);
		  lcd_data( pgm_read_byte(progmem_s++) );
		}
	}
}

void lcd_eee()
{
#ifdef EEE
	char * progmem_s;
	if (lcd_controller_type() != 0)
	{
		lcd_setCustomChars();
		lcd_clrscr();
		lcd_gotoxy(0,0);
		progmem_s = (char *) eee1;
		for (int j=0;j<16;j++)
		{
		  lcd_putc( pgm_read_byte(progmem_s++) );
		}
		lcd_gotoxy(0,1);
		progmem_s = (char *) eee2;
		for (int j=0;j<16;j++)
		{
		  lcd_putc( pgm_read_byte(progmem_s++) );
		}
	}
#endif
}

void lcd_logo()
{
	if (lcd_controller_type() != 0)
	{
		lcd_setCustomChars();
		lcd_clrscr();
		lcd_putc(0); // andi6510 - initally the character 8 was set but on my DOG-M display this was not the correct one...
		lcd_putc(1);
		lcd_putc(2);
		lcd_gotoxy(0,1);
		lcd_putc(3);
		lcd_putc(4);
		lcd_putc(5);
		lcd_gotoxy(4,0);
		lcd_puts_p(PSTR("Commodore"));
		lcd_gotoxy(4,1);
		lcd_puts_p(PSTR("never dies!"));
	}
}

void lcd_ready(uint8_t dev_addr)
{
    char *msg = (char *) buffer_msg;

	if (lcd_controller_type() != 0)
	{
		//memset(buffer_da,0,sizeof(buffer_da));
		lcd_clrline(1);
		lcd_puts_p(PSTR("READY:"));
		*msg++ = '0' + dev_addr/10;
		*msg++ = '0' + dev_addr%10;
		*msg++ = '\0';
		lcd_puts((char*)buffer_msg); 
	}
}

void lcd_path(char * fs_path)
{
    char *msg = (char *) buffer_msg;

	if (lcd_controller_type() != 0)
	{
		//memset(buffer_da,0,sizeof(buffer_da));
		lcd_clrline(0);
		if (fs_mode == 0) *msg++ = 'D';
		else 
		   *msg++ = 'I';
	//    *msg++ = '0' + fs_mode%10;
		*msg++ = '\0';
		lcd_puts((char*)buffer_msg); 
		lcd_puts_p(PSTR(":"));
		lcd_puts((char*)fs_path); 
	}
}

void lcd_contrast(uint8_t contrast)
{
	// andi6510: in case we have a st7036 LCD set contrast value by software
	if (lcd_controller_type() == 7)
	{
		lcdcontrast = contrast;
		lcd_command(0x29); // activate instruction table 1
		lcd_command(0x50 | ((0x30 & contrast) >> 4)); // icon off, booster off, contrast XX....
		lcd_command(0x70 |  (0x0F & contrast)      ); // contrast ...XXXX
		lcd_command(0x28); // activate instruction table 0
	}
}