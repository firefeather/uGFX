/*
 * This file is subject to the terms of the GFX License. If a copy of
 * the license was not distributed with this file, you can obtain one at:
 *
 *              http://ugfx.org/license.html
 */

#include "gfx.h"

#if GFX_USE_GDISP

#define GDISP_DRIVER_VMT		GDISPVMT_PCF8812
#include "drivers/gdisp/PCF8812/gdisp_lld_config.h"
#include "src/gdisp/driver.h"
#include "board_PCF8812.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

#define GDISP_MATRIX_HEIGHT		72
#define GDISP_MATRIX_WIDTH		102

#define GDISP_SCREEN_HEIGHT		65
#define GDISP_SCREEN_WIDTH		96

#define GDISP_INITIAL_CONTRAST	51
#define GDISP_INITIAL_BACKLIGHT	100

#define GDISP_FLG_NEEDFLUSH			(GDISP_FLG_DRIVER << 0)

#include "drivers/gdisp/PCF8812/PCF8812.h"

/*===========================================================================*/
/* Driver local routines    .                                                */
/*===========================================================================*/

// Some common routines and macros
#define RAM(g)			((uint8_t *)g->priv)

//#define xyaddr(x, y)	((x) + ((y) >> 3) * GDISP_MATRIX_WIDTH)
//#define xybit(y)		(1 << ((y) & 7))

#define xyaddr(x, y)	((((y) / 8) * GDISP_MATRIX_WIDTH) + (x))
#define xybit(y)		(1 << ((y) % 8))

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/*
 * As this controller can't update on a pixel boundary we need to maintain the
 * the entire display surface in memory so that we can do the necessary bit
 * operations. Fortunately it is a small display in monochrome.
 * Matrix 102 * 65 / 8 = 828,75 bytes.
 * Display 96 * 65 / 8 = 780
 */

#define GDISP_SCREEN_BYTES ((GDISP_SCREEN_WIDTH * GDISP_SCREEN_HEIGHT) / 8)
#define GDISP_MATRIX_BYTES ((GDISP_MATRIX_WIDTH * GDISP_MATRIX_HEIGHT) / 8) // real height 65 pixels, this fix 65 / 8 != 9

LLDSPEC bool_t gdisp_lld_init(GDisplay *g) {
	// The private area is the display surface.
	if (!(g->priv = gfxAlloc(GDISP_MATRIX_BYTES)))
		gfxHalt("GDISP PCF8812: Failed to allocate private memory");

	// Initialise the board interface
	init_board(g);

	// Hardware reset
	setpin_reset(g, TRUE);
	gfxSleepMilliseconds(100);
	setpin_reset(g, FALSE);
	gfxSleepMilliseconds(100);

	acquire_bus(g);

	write_index(g, PCF8812_SET_FUNC  | PCF8812_H);
	write_index(g, PCF8812_SET_TEMP  | PCF8812_TEMP_MODE_1);
	write_index(g, PCF8812_SET_VMULT | PCF8812_VMULT_MODE_1);
	write_index(g, PCF8812_SET_VOP   | 0x00);
	write_index(g, PCF8812_SET_FUNC);
	write_index(g, PCF8812_DISPLAY   | PCF8812_DISPLAY_MODE_NORMAL);
	write_index(g, PCF8812_SET_X); // X = 0
	write_index(g, PCF8812_SET_Y); // Y = 0

	coord_t i;

	for (i = 0; i < GDISP_MATRIX_BYTES; i++) {
		write_data(g, 0x00, 1);
	}

	// Finish Init
	post_init_board(g);

 	// Release the bus
	release_bus(g);

	/* Turn on the back-light */
	set_backlight(g, GDISP_INITIAL_BACKLIGHT);

	/* Initialise the GDISP structure */
	g->g.Width = GDISP_SCREEN_WIDTH;
	g->g.Height = GDISP_SCREEN_HEIGHT;
	g->g.Orientation = GDISP_ROTATE_0;
	g->g.Powermode = powerOn;
	g->g.Backlight = GDISP_INITIAL_BACKLIGHT;
	g->g.Contrast = GDISP_INITIAL_CONTRAST;

	return TRUE;
}

#if GDISP_HARDWARE_FLUSH
	LLDSPEC void gdisp_lld_flush(GDisplay *g) {

		// Don't flush if we don't need it.
		if (!(g->flags & GDISP_FLG_NEEDFLUSH)) {
			return;
		}

		acquire_bus(g);

		write_index(g, PCF8812_SET_X | 0);  // X = 0
		write_index(g, PCF8812_SET_Y | 0);  // Y = 0

		coord_t i;

		for (i = 0; i < GDISP_MATRIX_BYTES; i++) {
			write_data(g, RAM(g)[i], 1);
		}

		release_bus(g);
	}
#endif

#if GDISP_HARDWARE_DRAWPIXEL
	LLDSPEC void gdisp_lld_draw_pixel(GDisplay *g) {
		coord_t x, y;
		
		#if GDISP_NEED_CONTROL
			switch(g->g.Orientation) {
			default:
			case GDISP_ROTATE_0:
				x = g->p.x;
				y = g->p.y;
				break;
			case GDISP_ROTATE_90:
				x = g->p.y;
				y = g->g.Height - g->p.x-1;
				break;
			case GDISP_ROTATE_180:
				x = g->g.Width  - g->p.x-1;
				y = g->g.Height - g->p.y-1;
				break;
			case GDISP_ROTATE_270:
				x = g->g.Height - g->p.y-1;
				y = g->p.x;
				break;
			}
		#else
			x = g->p.x;
			y = g->p.y;
		#endif

		if (gdispColor2Native(g->p.color) != Black) {
			RAM(g)[xyaddr(x, y)] |= xybit(y);
		} else {
			RAM(g)[xyaddr(x, y)] &= ~xybit(y);
		}

		g->flags |= GDISP_FLG_NEEDFLUSH;
	}
#endif

/*
#if GDISP_NEED_CONTROL && GDISP_HARDWARE_CONTROL
	LLDSPEC void gdisp_lld_control(GDisplay *g) {
		switch(g->p.x) {
		case GDISP_CONTROL_POWER:
			if (g->g.Powermode == (powermode_t)g->p.ptr)
				return;
			switch((powermode_t)g->p.ptr) {
			case powerOff:
				acquire_bus(g);
				
				release_bus(g);
				break;
			case powerOn:
				acquire_bus(g);
				
				release_bus(g);
				break;
			case powerSleep:
				acquire_bus(g);
				
				release_bus(g);
				break;
			default:
				return;
			}
			g->g.Powermode = (powermode_t)g->p.ptr;
			return;

		case GDISP_CONTROL_ORIENTATION:
			if (g->g.Orientation == (orientation_t)g->p.ptr)
				return;
			switch((orientation_t)g->p.ptr) {
			case GDISP_ROTATE_0:
				acquire_bus(g);
								
				release_bus(g);
				g->g.Height = GDISP_SCREEN_HEIGHT;
				g->g.Width = GDISP_SCREEN_WIDTH;
				break;
			case GDISP_ROTATE_90:
				acquire_bus(g);
								
				release_bus(g);
				g->g.Height = GDISP_SCREEN_WIDTH;
				g->g.Width = GDISP_SCREEN_HEIGHT;
				break;
			case GDISP_ROTATE_180:
				acquire_bus(g);
								
				release_bus(g);
				g->g.Height = GDISP_SCREEN_HEIGHT;
				g->g.Width = GDISP_SCREEN_WIDTH;
				break;
			case GDISP_ROTATE_270:
				acquire_bus(g);
								
				release_bus(g);
				g->g.Height = GDISP_SCREEN_WIDTH;
				g->g.Width = GDISP_SCREEN_HEIGHT;
				break;
			default:
				return;
			}
			g->g.Orientation = (orientation_t)g->p.ptr;
			return;

        case GDISP_CONTROL_BACKLIGHT:
            if ((unsigned)g->p.ptr > 100)
            	g->p.ptr = (void *)100;
            set_backlight(g, (unsigned)g->p.ptr);
            g->g.Backlight = (unsigned)g->p.ptr;
            return;

		//case GDISP_CONTROL_CONTRAST:
        default:
            return;
		}
	}
#endif
*/
#if GDISP_NEED_CONTROL
	LLDSPEC void gdisp_lld_control(GDisplay *g) {
		switch(g->p.x) {
		case GDISP_CONTROL_POWER:
			if (g->g.Powermode == (powermode_t)g->p.ptr)
				return;
			switch((powermode_t)g->p.ptr) {
			case powerOff: case powerOn: case powerSleep: case powerDeepSleep:
				//board_power(g, (powermode_t)g->p.ptr);
				break;
			default:
				return;
			}
			g->g.Powermode = (powermode_t)g->p.ptr;
			return;

		case GDISP_CONTROL_ORIENTATION:
			if (g->g.Orientation == (orientation_t)g->p.ptr)
				return;
			switch((orientation_t)g->p.ptr) {
				case GDISP_ROTATE_0:
				case GDISP_ROTATE_180:
					write_index(g, PCF8812_SET_FUNC | PCF8812_H | 0x01);
					if (g->g.Orientation == GDISP_ROTATE_90 || g->g.Orientation == GDISP_ROTATE_270) {
						coord_t		tmp;

						tmp = g->g.Width;
						g->g.Width = g->g.Height;
						g->g.Height = tmp;
					}
					break;
				case GDISP_ROTATE_90:
				case GDISP_ROTATE_270:
					write_index(g, PCF8812_SET_FUNC | PCF8812_V | 0x01);
					if (g->g.Orientation == GDISP_ROTATE_0 || g->g.Orientation == GDISP_ROTATE_180) {
						coord_t		tmp;

						tmp = g->g.Width;
						g->g.Width = g->g.Height;
						g->g.Height = tmp;
					}
					break;
				default:
					return;
			}
			g->g.Orientation = (orientation_t)g->p.ptr;
			return;

		case GDISP_CONTROL_BACKLIGHT:
			if ((unsigned)g->p.ptr > 100) g->p.ptr = (void *)100;
			//board_backlight(g, (unsigned)g->p.ptr);
			g->g.Backlight = (unsigned)g->p.ptr;
			return;

		case GDISP_CONTROL_CONTRAST:
			if ((unsigned)g->p.ptr > 100) g->p.ptr = (void *)100;
			//board_contrast(g, (unsigned)g->p.ptr);
			g->g.Contrast = (unsigned)g->p.ptr;
			return;
		}
	}
#endif

#endif // GFX_USE_GDISP
