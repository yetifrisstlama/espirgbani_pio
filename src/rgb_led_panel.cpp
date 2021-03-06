#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_log.h"
#include "common.h"
#include "esp_heap_caps.h"
#include "frame_buffer.h"
#include "rgb_led_panel.h"
#include "assert.h"
#include "json_settings.h"

extern "C" {
	#include "i2s_parallel.h"
}

unsigned g_frames = 0; // frame counter
unsigned g_f_del = 33; // delay between frames [ms]
int g_rgbLedBrightness = 5;

// Double buffering has been removed to save RAM. No visual differences!
uint16_t *bitplane[BITPLANE_CNT];
//DISPLAY_WIDTH * 32 * 3 array with image data, 8R8G8B

// .json configurable parameters
static int latch_offset = 0;
static unsigned extra_blank = 0;
static bool isColumnSwapped = false;

void init_rgb()
{
	initFb();

	i2s_parallel_buffer_desc_t bufdesc[1 << BITPLANE_CNT];
	i2s_parallel_config_t cfg;

	cfg.gpio_clk = GPIO_CLK;
	cfg.gpio_bus[0] = GPIO_R1;
	cfg.gpio_bus[1] = GPIO_G1;
	cfg.gpio_bus[2] = GPIO_B1;
	cfg.gpio_bus[3] = GPIO_R2;
	cfg.gpio_bus[4] = GPIO_G2;
	cfg.gpio_bus[5] = GPIO_B2;
	cfg.gpio_bus[6] = GPIO_A;
	cfg.gpio_bus[7] = GPIO_B;
	cfg.gpio_bus[8] = GPIO_C;
	cfg.gpio_bus[9] = GPIO_D;
	cfg.gpio_bus[10] = GPIO_LAT;
	cfg.gpio_bus[11] = GPIO_BLANK;
	cfg.gpio_bus[12] = (gpio_num_t)(-1);
	cfg.gpio_bus[13] = (gpio_num_t)(-1);
	cfg.gpio_bus[14] = (gpio_num_t)(-1);
	cfg.gpio_bus[15] = (gpio_num_t)(-1);

	cfg.bits = I2S_PARALLEL_BITS_16,
	cfg.bufa = bufdesc;
	cfg.bufb = bufdesc;

	//--------------------------
	// .json configuration
	//--------------------------
	// get `panel` dictionary
	cJSON *jPanel = jGet(getSettings(), "panel");

	// delay between updateFrame calls [ms]
	g_f_del = 1000 / jGetI(jPanel, "max_frame_rate", 30);

	// Swap pixel x[0] with x[1]
	isColumnSwapped = jGetB(jPanel, "column_swap", false);
	if (isColumnSwapped)
		log_v("column_swap applied!");

	// adjust clock cycle of the latch pulse (nominally = 0 = last pixel)
	latch_offset = (DISPLAY_WIDTH - 1) + jGetI(jPanel, "latch_offset", 0);
	latch_offset %= DISPLAY_WIDTH;
	log_v("latch_offset = %d", latch_offset);

	// adjust extra blanking cycles to reduce ghosting effects
	extra_blank = jGetI(jPanel, "extra_blank", 1);

	// set clock divider
	cfg.clk_div = jGetI(jPanel, "clkm_div_num", 4);
	if (cfg.clk_div < 1) cfg.clk_div = 1;
	log_v("clkm_div_num = %d", cfg.clk_div);

	cfg.is_clk_inverted = jGetB(jPanel, "is_clk_inverted", true);

	//--------------------------
	// init the sub-frames
	//--------------------------
	for (int i=0; i<BITPLANE_CNT; i++) {
		bitplane[i] = (uint16_t*)heap_caps_malloc(BITPLANE_SZ*2, MALLOC_CAP_DMA);
		assert(bitplane[i] && "Can't allocate bitplane memory");
		memset(bitplane[i], 0, BITPLANE_SZ*2);
	}

	//Do binary time division setup. Essentially, we need n of plane 0, 2n of plane 1, 4n of plane 2 etc, but that
	//needs to be divided evenly over time to stop flicker from happening. This little bit of code tries to do that
	//more-or-less elegantly.
	int times[BITPLANE_CNT]={0};
	for (int i=0; i<((1<<BITPLANE_CNT)-1); i++) {
		int ch=0;

		//Find plane that needs insertion the most
		for (int j=0; j<BITPLANE_CNT; j++) {
			if (times[j] <= times[ch])
				ch=j;
		}

		//Insert the plane
		bufdesc[i].memory = bitplane[ch];
		bufdesc[i].size = BITPLANE_SZ * 2;

		//Magic to make sure we choose this bitplane an appropriate time later next time
		times[ch] += (1 << (BITPLANE_CNT - ch));
	}

	//End markers
	bufdesc[((1 << BITPLANE_CNT) - 1)].memory = NULL;

	//Setup I2S
	i2s_parallel_setup(&I2S1, &cfg);

	log_i("I2S setup done.");
}

void updateFrame() {
	// Wait until all the layers are done updating
	waitDrawingDone();

	for (unsigned int y=0; y<16; y++) {
		// Precalculate line bits of the *previous* line, which is the one we're displaying now
		int lbits = 0;

		if ((y - 1) & 1) lbits |= BIT_A;
		if ((y - 1) & 2) lbits |= BIT_B;
		if ((y - 1) & 4) lbits |= BIT_C;
		if ((y - 1) & 8) lbits |= BIT_D;

		for (int fx=0; fx<DISPLAY_WIDTH; fx++) {
			int x = fx;

			if (isColumnSwapped)
				x ^= 1;

			int v = lbits;

			// Do not show image while the line bits are changing
			if (fx < extra_blank || fx >= g_rgbLedBrightness) v |= BIT_BLANK;

			// latch on last bit...
			if (fx == latch_offset) v |= BIT_LAT;

			// Does alpha blending of all graphical layers, a rather
			// expensive operation and best kept out of innermost loop.
			int c1 = getBlendedPixel(x, y);
			int c2 = getBlendedPixel(x, y + 16);

			for (int pl=0; pl<BITPLANE_CNT; pl++) {
				// reset RGB bits
				int v_ = v;

				// bitmask for pixel data in input for this bitplane
				int mask = (1 << (8 - BITPLANE_CNT + pl));

				if (c1 & (mask << 16)) v_ |= BIT_R1;
				if (c1 & (mask <<  8)) v_ |= BIT_G1;
				if (c1 & (mask <<  0)) v_ |= BIT_B1;
				if (c2 & (mask << 16)) v_ |= BIT_R2;
				if (c2 & (mask <<  8)) v_ |= BIT_G2;
				if (c2 & (mask <<  0)) v_ |= BIT_B2;

				//Save the calculated value to the bitplane memory
				bitplane[pl][y * DISPLAY_WIDTH + x] = v_;
			}
		}
	}

	doneUpdating();
	g_frames++;
}
