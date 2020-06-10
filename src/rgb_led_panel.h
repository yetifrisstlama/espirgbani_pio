#ifndef RGB_LED_PANEL_H
#define RGB_LED_PANEL_H
#include "common.h"

//This is the bit depth, per RGB subpixel, of the data that is sent to the display.
//The effective bit depth (in computer pixel terms) is less because of the PWM correction. With
//a bitplane count of 7, you should be able to reproduce an 16-bit image more or less faithfully, though.
#define BITPLANE_CNT 7

//x*DISPLAY_HEIGHT RGB leds, 2 pixels per 16-bit value...
#define BITPLANE_SZ (DISPLAY_WIDTH * DISPLAY_HEIGHT / 2)  // [16 bit words]

// -------------------------------------------
//  Meaning of the bits in a 16 bit DMA word
// -------------------------------------------
//Upper half RGB
#define BIT_R1 (1<<0)
#define BIT_G1 (1<<1)
#define BIT_B1 (1<<2)
//Lower half RGB
#define BIT_R2 (1<<3)
#define BIT_G2 (1<<4)
#define BIT_B2 (1<<5)
#define BIT_A (1<<6)
#define BIT_B (1<<7)
#define BIT_C (1<<8)
#define BIT_D (1<<9)
#define BIT_LAT (1<<10)
#define BIT_BLANK (1<<11)
// -1 = don't care
// -1
// -1
// -1

//Change to set the global brightness of the display, range 0 ... (DISPLAY_WIDTH-8)
extern int g_rgbLedBrightness;

void init_rgb();
void updateFrame();

#endif