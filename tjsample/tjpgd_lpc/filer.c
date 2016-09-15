/*----------------------------------------------------------------------*/
/* Text Filer                                          (C)ChaN, 2011    */
/*----------------------------------------------------------------------*/

#include <string.h>
#include "filer.h"
#include "disp.h"
#include "sound.h"
#include "xprintf.h"
#include "uart.h"


#define N_MAXDIR	100
#define	SZ_PATH		80

#define	FC_WHITE	RGB16(192,192,192)
#define	FC_BLUE		RGB16(0,0,192)
#define C_NORMAL	FC_WHITE
#define C_HIDDEN	FC_BLUE
#define C_DIRECTORY	C_CYAN
#define C_READONLY	C_GREEN
#define C_TITLE		((FC_BLUE << 16) | C_WHITE)
#define C_STAT		((FC_BLUE << 16) | C_WHITE)
#define C_WBASE		C_WHITE
#define C_WTITLE	((C_CYAN << 16) | C_BLACK)
#define C_WINPUT	((C_GRAY << 16) | C_WHITE)




typedef struct {
	char str[SZ_PATH];
	FILINFO	diritems[N_MAXDIR];
} FILER;



static
void put_size (
	DWORD sz
)
{
	if (sz < 10000) {
		xfprintf(disp_putc, "%6lu B", sz);
	} else if (sz < 100 * 1024) {
		xfprintf(disp_putc, "%4lu.%luKB", sz / 1024, (sz % 1024) * 10 / 1024);
	} else if (sz < 10000 * 1024) {
		xfprintf(disp_putc, "%6luKB", sz / 1024);
	} else {
		xfprintf(disp_putc, "%4lu.%luMB", sz / 1048576, (sz % 1048576) * 10 / 1048576);
	}
}



static
void put_item (
	const FILINFO *item,	/* Pointer to the dir item */
	int vpos,				/* Row position in the file area */
	int csr					/* Cursor on the item */
)
{
	uint32_t col;


	/* Set item color */
	col = C_NORMAL;
	if (item->fattrib & AM_DIR) col = C_DIRECTORY;
	if (item->fattrib & AM_RDO) col = C_READONLY;
	if (item->fattrib & AM_HID) col = C_HIDDEN;
	if (csr) col = (col << 16) | ((col >> 16) & 0xFFFF);	/* Inverted cursor */
	disp_font_color(col);

	disp_locate(0, vpos + 1);
	xfprintf(disp_putc, "%c%-12s", (item->fattrib & 0x80) ? '*' : ' ', item->fname);
	if (TS_WIDTH >= 40)
		xfprintf(disp_putc, " %2u/%02u/%02u %2u:%02u", (item->fdate >> 9) + 1980, (item->fdate >> 5) & 15, item->fdate & 31, item->ftime >> 11, (item->ftime >> 5) & 63);
	xfputs(disp_putc, "    ");
	if (item->fattrib & AM_DIR)
		xfputs(disp_putc, " <DIR>  ");
	else
		put_size(item->fsize);
}



static
void rfsh_list (	/* Draw directory items */
	const FILINFO *diritems,	/* Pointer to directory item teble */
	int item,		/* Current item */
	int items,		/* Number of items */
	int ofs			/* Display offset */
)
{
	int i, j;


	if (items > 0) {
		for (i = 0; i < TS_HEIGHT - 2 && i + ofs < items; i++)	/* Put items */
			put_item(&diritems[ofs + i], i, ofs + i == item ? 1 : 0);
	} else {
		disp_font_color(C_WHITE);
		disp_locate(0, 1);
		xfputs(disp_putc, "No Item");
		for (j = 0; j < TS_WIDTH; j++) disp_putc(' ');
		disp_putc('\n');
		i = 1;
	}
	disp_font_color(C_WHITE);
	while (i < TS_HEIGHT - 1) {
		for (j = 0; j < TS_WIDTH; j++) disp_putc(' ');
		disp_putc('\n');
		i++;
	}
}


static
void rfsh_stat (		/* Draw status line */
	const FILINFO *diritems,
	int items
)
{
	int i, sel;
	uint32_t szsel, sztot;


	for (i = sel = szsel = sztot = 0; i < items; i++) {
		sztot += diritems[i].fsize;
		if (diritems[i].fattrib & 0x80) {
			szsel += diritems[i].fsize;
			sel++;
		}
	}

	disp_locate(0, TS_HEIGHT - 1);
	disp_font_color(C_STAT);
	if (sel) {
		xfprintf(disp_putc, " %d marked, ", sel);
		put_size(szsel);
	} else {
		xfprintf(disp_putc, " %d items, ", items);
		put_size(sztot);
	}
	for (i = 0; i < TS_WIDTH; i++) disp_putc(' ');
}



static
void rfsh_base (		/* Draw title line */
	const char *path	/* Pointer to current directory path */
)
{
	int i;

	disp_font_color(C_TITLE);
	i = (strlen(path) > TS_WIDTH) ? strlen(path) - TS_WIDTH : 0;
	xfprintf(disp_putc, "\f%s", path + i);
	for (i = 0; i < TS_WIDTH; i++) disp_putc(' ');
}



static
int selection (
	FILINFO *diritems,
	int items,
	int op			/* 0:Check, 1:Clear all, 2:Set all */
)
{
	int i, n = 0;

	for (i = 0; i < items; i++) {
		switch (op) {
		case 0:	/* Count selected */
			if (diritems[i].fattrib & 0x80) n++;
			break;
		case 1:	/* Deselect all */
			diritems[i].fattrib &= ~0x80;
			break;
		case 2:	/* Select all */
			if (!(diritems[i].fattrib & AM_DIR))
				diritems[i].fattrib |= 0x80;
			break;
		}
	}
	return n;
}



static
FRESULT load_dir (
	char *path,			/* Pointer to the current path name buffer */
	FILINFO *diritems,	/* Pointer to directory item table */
	int *items
)
{
	int i;
	DIR dir;
	FRESULT res;

	i = 0;
	res = f_getcwd(path, SZ_PATH);
	if (res == FR_OK) {
		res = f_opendir(&dir, "");
		if (res == FR_OK) {
			do {
#if _USE_LFN
				diritems[i].lfname = 0;
#endif
				res = f_readdir(&dir, &diritems[i]);
				if (res || !diritems[i].fname[0]) break;
				if (diritems[i].fname[0] != '.') i++;
			} while (i < N_MAXDIR);
		}
	}
	*items = i;

	return res;
}




static
void dlg_str (
	const char *title,
	const char *text,
	int width
)
{
	int row, col, h;


	row = TS_HEIGHT / 2 - 1;
	col = (TS_WIDTH - width) / 2;

	disp_locate(col, row);
	disp_font_color(C_WBASE);
	disp_putc(1);
	disp_font_color(C_WTITLE);
	for (h = 1; h < width - 1 && *title; h++) disp_putc(*title++);
	disp_font_color(C_WBASE);
	for ( ; h < width - 1; h++) disp_putc(6);
	disp_putc(2);

	disp_locate(col, row + 1);
	disp_putc(5);
	for (h = 1; h < width - 1 && *text; h++) disp_putc(*text++);
	for ( ; h < width - 1; h++) disp_putc(' ');
	disp_putc(5);

	disp_locate(col, row + 2);
	disp_putc(3);
	for (h = 1; h < width - 1; h++) disp_putc(6);
	disp_putc(4);
}



static
int dlg_input (
	char *str,
	const char *title,
	const char *text,
	int width
)
{
	int row, col, h, i, j;
	uint8_t c;


	row = TS_HEIGHT / 2 - 1;
	col = (TS_WIDTH - width) / 2;

	dlg_str(title, text, width);

	h = strlen(text);
	if (h > width - 3) return 0;
	width -= h + 2;
	col += h + 1;

	for (i = 0; str[i] && i < SZ_PATH - 1; i++) ;
	memset(&str[i], ' ', SZ_PATH - i);
	str[i] = '_';
	disp_font_color(C_WINPUT);
	for (;;) {
		j = i + 1 - width;
		if (j < 0) j = 0;
		disp_locate(col, row + 1);
		for (h = 0; h < width; h++) disp_putc(str[j++]);
		c = uart0_getc();
		if (c == KEY_CAN) return 0;
		if (c == KEY_OK) { str[i] = 0; return 1; }
		if (c == KEY_BS) {
			if (i) {
				str[i--] = ' ';
				str[i] = '_';
			}
			continue;
		}
		if (c >= ' ' && i < SZ_PATH - 1) {
			str[i++] = c;
			str[i] = '_';
		}
	}
}



static
int strstr_ext (
	const char *src,
	const char *dst
)
{
	int si, di;
	char s, d;

	si = strlen(src);
	di = strlen(dst);
	if (si < di) return 0;
	si -= di; di = 0;
	do {
		s = src[si++];
		if (s >= 'a' && s <= 'z') s -= 0x20;
		d = dst[di++];
		if (d >= 'a' && d <= 'z') d -= 0x20;
	} while (s && s == d);
	return (s == d);
}



/*------------------------------------------*/
/* Opens a file with associated method      */

void load_file (
	const char *fn,		/* Pointer to the file name */
	void *work,			/* Pointer to filer work area (must be word aligned) */
	UINT sz_work		/* Size of the filer work area */
)
{
	FIL fil;			/* Pointer to a file object */


	if (f_open(&fil, fn, FA_READ) == FR_OK) {
		if (strstr_ext(fn, ".BMP")) {	/* BMP image viewer (24bpp, 1280pix in width max) */
			load_bmp(&fil, work, sz_work);
		}
		if (strstr_ext(fn, ".JPG")) {	/* Motion image viewer */
			load_jpg(&fil, work, sz_work);
		}
		if (strstr_ext(fn, ".IMG")) {	/* Motion image viewer */
			load_img(&fil, work, sz_work);
		}
		if (strstr_ext(fn, ".WAV")) {	/* Sound file player (RIFF-WAVE only) */
			load_wav(&fil, fn, work, sz_work);
		}
		f_close(&fil);
	}
}



/*------------------------------------------*/
/* File Explorer                            */

void filer (
	void *work,		/* Pointer to filer work area (must be word aligned) */
	UINT sz_work	/* Size of the filer work area */
)
{
	FILER *fw = (FILER*)work;
	FRESULT res;
	int item, ofs, items, i;
	char k;


	if (sz_work < sizeof(FILER)) return;
	item = ofs = 0;

	for (;;) {
		res = load_dir(fw->str, fw->diritems, &items);	/* Load directory items */
		if (item >= items) item = items ? items - 1 : 0;
		if (ofs > item) ofs = item;
		rfsh_base(fw->str);
		rfsh_stat(fw->diritems, items);
		rfsh_list(fw->diritems, item, items, ofs);
		if (res) {	/* Dir load error */
			disp_font_color(C_RED);
			disp_locate(0, 1);
			xfprintf(disp_putc, "FS error #%u.\nPush OK to retry...", res);
		}
		for (;;) {
			k = uart0_getc();		/* Get a button/key command */
			if (k >= 'a' && k <= 'z') k -= 0x20;

			if (k == KEY_CAN) {				/* [Esc] Exit filer */
				disp_putc('\f');
				return;	/* Exit filer */
			}
			if (k == KEY_OK) {				/* [Enter] Open an item */
				if (fw->diritems[item].fattrib & AM_DIR) {
					f_chdir(fw->diritems[item].fname);	/* Enter the directory */
					item = ofs = 0;
				} else {
					load_file(fw->diritems[item].fname, work, sz_work);	/* Opens the file with associated method */
				}
				break;	/* Re-load dir */
			}
			if (k == KEY_LEFT || k == KEY_BS) {	/* ^[S] [BS] Go to parent dir */
				f_chdir("..");
				item = ofs = 0;
				break;	/* Re-load dir */
			}
#if 0
			if (k == KEY_DRIVE) {			/* [D] Change current drive */
				fw->str[0] = 0;
				if (!dlg_input(fw->str, "Change Drive", "Drive#:", 14)) {
					rfsh_list(fw->diritems, item, items, ofs);
					continue;
				}
				f_chdrive(fw->str[0] - '0');
				break;	/* Re-load dir */
			}
#endif
			if (res) continue;

			if (k == KEY_RENAME) {			/* [N] Rename an item */
				fw->str[0] = 0;
				if (dlg_input(fw->str, "Rename", "", 18)) {
					f_rename(fw->diritems[item].fname, fw->str);
					break;	/* Re-load dir */
				}
				k = 0;	/* Redraw list */
			}
			if (k == KEY_MKDIR) {			/* [K] Make a directory */
				fw->str[0] = 0;
				if (dlg_input(fw->str, "Make a dir", "", 14)) {
					f_mkdir(fw->str);
					break;	/* Re-load dir */
				}
				k = 0;	/* Redraw list */
			}
#if 0
			if (k == KEY_COPY) {			/* [C] Copy file(s) */
				fw->str[0] = 0;
				if (dlg_input(fw->str, "Copy file to", "", 18)) {
					res = FR_OK; c = 0;
					if (!selection(fw->diritems, items, 0)) {
						res = cp_file(&fil, fw, item, sz_work);		/* Copy the file */
						c++;
					} else {
						for (i = res = 0; i < items || res; i++) {
							if (fw->diritems[i].fattrib & 0x80) {	/* Remove selected items */
								res = cp_file(&fil, fw, i, sz_work);	/* Copy the file */
								if (res == FR_OK) c++;
							}
						}
						selection(fw->diritems, items, 1);
					}
					if (res)
						xsprintf(fw->str, "FS error #%d", res);
					else
						xsprintf(fw->str, "%d files coied", c);
					dlg_str("Copy file", fw->str, 18);
					k = GETCH();
					break;	/* Re-load dir */
				}
				k = 0;	/* Redraw list */
			}
			if (k == KEY_ATTRIB) {			/* [T] Change attribute */
				a = 0;
				if (fw->diritems[item].fattrib & AM_RDO) a |= 8;
				if (fw->diritems[item].fattrib & AM_HID) a |= 4;
				if (fw->diritems[item].fattrib & AM_SYS) a |= 2;
				if (fw->diritems[item].fattrib & AM_ARC) a |= 1;
				xsprintf(s = fw->str, "0b%04b", a);
				if (dlg_input(fw->str, "Arrtibute", "RHSA=", 14) && xatoi(&s, &a)) {
					a <<= 8;
					if (a & 0x800) a |= AM_RDO;
					if (a & 0x400) a |= AM_HID;
					if (a & 0x200) a |= AM_SYS;
					if (a & 0x100) a |= AM_ARC;
					if (!selection(fw->diritems, items, 0)) {
						f_chmod(fw->diritems[item].fname, a, AM_RDO|AM_HID|AM_SYS|AM_ARC);		/* Remove the item */
					} else {
						for (i = 0; i < items; i++) {
							if (fw->diritems[i].fattrib & 0x80)		/* Remove selected items */
								f_chmod(fw->diritems[i].fname, a, AM_RDO|AM_HID|AM_SYS|AM_ARC);	/* Remove the item */
						}
					}
					break;	/* Re-load dir */
				}
				k = 0;	/* Redraw list */
			}
#endif
			if (k == KEY_REMOVE) {			/* [U] Remove item(s) */
				fw->str[0] = 0;
				if (dlg_input(fw->str, "Remove item", "OK?<y/n>:", 14) && fw->str[0] == 'y') {
					if (!selection(fw->diritems, items, 0)) {
						f_unlink(fw->diritems[item].fname);		/* Remove the item */
					} else {
						for (i = 0; i < items; i++) {
							if (fw->diritems[i].fattrib & 0x80) {	/* Remove selected items */
								dlg_str("Removing...", fw->diritems[i].fname, 14);
								f_unlink(fw->diritems[i].fname);
							}
						}
					}
					break;	/* Re-load dir */
				}
				k = 0;	/* Redraw list */
			}
			if (k == KEY_ALL) {				/* [A] Select/Deselect all button */
				selection(fw->diritems, items, selection(fw->diritems, items, 0) ? 1 : 2);
				rfsh_stat(fw->diritems, items);
				k = 0;	/* Redraw list */
			}
			if (k == KEY_SPC) {				/* [Space] Select/Deselect button */
				fw->diritems[item].fattrib ^= 0x80;	/* Select the item */
				rfsh_stat(fw->diritems, items);
				if (item + 1 >= items) {
					put_item(&fw->diritems[item], item - ofs, 1);
					continue;
				}
				k = KEY_DOWN;	/* Move down */
			}
			if (k == KEY_DOWN) {			/* ^[X] Down cursor */
				if (item + 1 >= items) continue;
				if (item < TS_HEIGHT - 3 + ofs) {	/* Move cursor */
					put_item(&fw->diritems[item], item - ofs, 0);
					item++;
					put_item(&fw->diritems[item], item - ofs, 1);
					continue;
				} 
				item++; ofs++;	/* Scroll file list */
				k = 0;	/* Redraw list */
			}
			if (k == KEY_UP) {				/* ^[E] Up cursor */
				if (item == 0) continue;
				if (item > ofs) {			/* Move cursor */
					put_item(&fw->diritems[item], item - ofs, 0);
					item--;
					put_item(&fw->diritems[item], item - ofs, 1);
					continue;
				}
				item--; ofs--;		/* Scroll file list */
				k = 0;	/* Redraw list */
			}
			if (k == 0)
				rfsh_list(fw->diritems, item, items, ofs);
		}
	}
}

