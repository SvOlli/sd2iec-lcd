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

   display_lcd.c: lcd display routines and data
*/

#ifndef DISPLAY_LCD
#define DISPLAY_LCD

#include "lcd.h"

#define MAXLINELENGHT 20

#define DS_CLR lcd_clrscr();
#define DS_CLRLINE(A) lcd_clrline(A);
#define DS_READY(A) lcd_ready(A);
#define DS_INIT lcd_init(LCD_DISP_ON);

#define DS_CTRL(A)  lcd_cmdseq(A);

#define DS_LOGO lcd_logo();

#define DS_PUTS(A)  lcd_puts(A);
#define DS_PUTSP(A)  lcd_puts_p(PSTR(A));
#define DS_PUTSPP(A)  lcd_puts_p(A);

#define DS_SHOW(A,B)  lcd_clrline(A); lcd_puts(B);
#define DS_SHOWP(A,B)  lcd_clrline(A); lcd_puts_p(PSTR(B));

#define EEE
#define DS_EEE lcd_eee();
#define DS_CONTRAST(A) lcd_contrast(A);

#define DS_TITLE lcd_clrline(0); lcd_puts_p(PSTR("SD2IEC " LCDVERSION));
#define DS_DIR lcd_clrline(1); lcd_puts_p(PSTR("L:$"));
#define DS_LOAD(A) lcd_clrline(1);   lcd_puts_p(PSTR("L:"));  lcd_puts(A);
#define DS_SAVE(A) lcd_clrline(1);   lcd_puts_p(PSTR("S:"));  lcd_puts(A);
#define DS_DEL(A) lcd_clrline(1);   lcd_puts_p(PSTR("R:"));  lcd_puts(A);
#define DS_CD(A) lcd_path(A);
#define DS_SHOWP1(A)  lcd_gotoxy(0,1); lcd_puts_p(PSTR(A));

void lcd_clrline(int line);
void lcd_cmdseq(char * seq);
void lcd_logo(void);
void lcd_eee(void);
void lcd_setCustomChars(void);
void lcd_ready(uint8_t device);
void lcd_path(char * path);
void lcd_contrast(uint8_t contrast);
extern uint8_t fs_mode;
extern uint8_t lcdcontrast;

#endif
