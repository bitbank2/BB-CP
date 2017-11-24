//
// BB-CP - a faster replacement for FBTFT + FBCP
//
// Copyright (c) 2017 Larry Bank
// email: bitbank@pobox.com
// Project started 11/22/2017
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <spi_lcd.h>

// Use dispmanx API on RPi0
#ifdef _RPIZERO_
#include <bcm_host.h>
DISPMANX_DISPLAY_HANDLE_T display;
DISPMANX_RESOURCE_HANDLE_T screen_resource;
DISPMANX_MODEINFO_T display_info;
VC_IMAGE_TRANSFORM_T transform;
uint32_t image_prt;
VC_RECT_T rect1;
#endif // _RPIZERO_

// We only support the ILI9341 (240x320), but define these just in case
#define LCD_CX 320
#define LCD_CY 240

// Pointers to the local copies of the framebuffer
static unsigned char *pScreen, *pAltScreen;
#ifndef _RPIZERO_
static unsigned char *pFB; // pointer to /dev/fb0
static int iFBPitch; // bytes per line of /dev/fb0
static int iScreenSize;
// Framebuffer variable and fixed info
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
#endif // !_RPIZERO_
static int iLCDPitch; // bytes per line of our LCD buffer
static int fbfd; // framebuffer file handle
static int iTileWidth, iTileHeight;
static int bRunning, bShowFPS, bLCDFlip, iSPIChan, iSPIFreq, iDC, iReset, iLED;
//
// Get the current time in nanoseconds
//
static uint64_t NanoClock()
{
	uint64_t ns;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	ns = time.tv_nsec + (time.tv_sec * 1000000000LL);
	return ns;
} /* NanoClock() */

//
// Sleep for N nanonseconds
//
static void NanoSleep(uint64_t ns)
{
struct timespec ts;

	if (ns <= 100LL || ns > 999999999LL) return;
	ts.tv_sec = 0;
	ts.tv_nsec = ns;
	nanosleep(&ts, NULL);
} /* NanoSleep() */

//
// Initialize the framebuffer and SPI LCD
//
// Return 0 for success, 1 for failure
//
static int InitDisplay(int bLCDFlip, int iSPIChan, int iSPIFreq, int iDC, int iReset, int iLED)
{

#ifdef _RPIZERO_
{
	int ret;
	bcm_host_init();
	display = vc_dispmanx_display_open(0);
	if (!display)
	{
		fprintf(stderr, "Unable to open primary display\n");
		return 1;
	}
	ret = vc_dispmanx_display_get_info(display, &display_info);
	if (ret)
	{
		fprintf(stderr, "Unable to get primary display information\n");
		return 1;
	}
	screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, LCD_CX, LCD_CY, &image_prt);
	if (!screen_resource)
	{
		fprintf(stderr, "Unable to create screen buffer\n");
		close(fbfd);
		vc_dispmanx_display_close(display);
		return 1;
	}
	vc_dispmanx_rect_set(&rect1, 0, 0, LCD_CX, LCD_CY);
}
#else
	// Open and map a pointer to the fb0 framebuffer
	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd)
	{
        // get the fixed screen info
        ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
      	// get the variable screen info
        ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
        iFBPitch = (vinfo.xres * vinfo.bits_per_pixel) / 8;
        iScreenSize = finfo.smem_len;
        pFB = (unsigned char *)mmap(0, iScreenSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	}
	else // can't open display
	{
		return 1;
	}
#endif // _RPIZERO_

	if (spilcdInit(LCD_ILI9341, bLCDFlip, iSPIChan, iSPIFreq, iDC, iReset, iLED))
		return 1;
	spilcdSetOrientation(LCD_ORIENTATION_LANDSCAPE);
	
	// Allocate 2 local copies of the framebuffer for comparison
	iLCDPitch = LCD_CX * 2;
	pScreen = malloc(iLCDPitch * LCD_CY);
	pAltScreen = malloc(iLCDPitch * LCD_CY); // our copy of the display

	return 0;
} /* InitDisplay() */

//
// Compare the current frame with the previous and mark changed tiles
// as a set bit in an array of flags
//
static int FindChangedRegion(unsigned char *pSrc, unsigned char *pDst, int iWidth,
 int iHeight, int iPitch, int iTileWidth, int iTileHeight, uint32_t *pRegions)
{
int x, y, xc, yc, dy;
int xCount, yCount;
uint32_t *s, *d, u32RowBits;
int iTotalChanged = 0;
//
// Divide the image into 16x16 tiles; each tile on a row is 1 bit in the changed bit field
// This makes managing the changes easier for display on a normal monitor and on LCDs with
// slow connections (e.g. SPI/I2C)
// The code has an "early exit" for each tile. In essence, the more the bitmaps are alike
// the harder this code will work and the less of the screen will need to be repainted
// In other words, the performance is balanced such that it should maintain a constant output rate
// no matter what the bitmap conditions are
//
   yCount = (iHeight+iTileHeight-1) / iTileHeight; // divide it into NxN tiles
   xCount = (iWidth+iTileWidth-1) / iTileWidth;

  // Loop through all of the tiles
   for (yc=0; yc<yCount; yc++)
   {
      u32RowBits = 0;
      for (xc=0; xc<xCount; xc++)
      {
         //point to the current tile
         s = (uint32_t *)&pSrc[(((yc*iTileHeight)/*+iPatOff*/) * iPitch) + (xc*iTileWidth*2)];
         d = (uint32_t *)&pDst[(((yc*iTileHeight)/*+iPatOff*/) * iPitch) + (xc*iTileWidth*2)];
         // loop through the pixels of this tile
	 if ((yc+1)*iTileHeight > iHeight)
		dy = iHeight - (yc*iTileHeight);
	 else
		dy = iTileHeight;
         for (y =0/* iPatOff */; y<dy; y++)
         {
            for (x = 0; x < iTileWidth/2; x++) // compare pairs of pixels
            { // test pairs of RGB565 pixels
               if (s[x] != d[x]) // any change means we mark this strip as being changed and move on
               {
                  u32RowBits |= (1 << xc);
                  iTotalChanged++;
                  y = iTileHeight; x = iTileWidth; // continue to next tile
               }
            } // for x
            s += (iPitch/4);
            d += (iPitch/4);
         } // for y
      } // for xc
   pRegions[yc] = u32RowBits;
   } // for yc
   return iTotalChanged;
} /* FindChangedRegion() */

//
// Take a snapshot of the current FrameBuffer
// Converts the pixels from RGB8888 to RGB565  if needed
//
static void FBCapture(void)
{
#ifdef _RPIZERO_
	vc_dispmanx_snapshot(display, screen_resource, 0);
	vc_dispmanx_resource_read_data(screen_resource, &rect1, pScreen, iLCDPitch);
#else
	if (vinfo.xres >= LCD_CX * 2) // need to shrink by 1/4
	{
		if (vinfo.bits_per_pixel == 16)
		{
		uint32_t *s, *d, u32Magic, u32_1, u32_2;
		int x, y;
			u32Magic = 0xf7def7de;
			for (y=0; y<LCD_CY; y++)
			{
				s = (uint32_t *)&pFB[y*2*iFBPitch];
				d = (uint32_t *)&pScreen[y*iLCDPitch];
				for (x=0; x<LCD_CX; x+=2)
				{
				// average horizontally
					u32_1 = s[0];
					u32_2 = s[1];
					u32_1 = (u32_1 & u32Magic) >> 1;
					u32_2 = (u32_2 & u32Magic) >> 1;
					u32_1 += (u32_1 << 16);
					u32_2 += (u32_2 >> 16); // average
					u32_1 = (u32_1 >> 16) | (u32_2 << 16);
					*d++ = u32_1;
					s += 2;
				} // for x
			} // for y
		}
		else // need to convert to RGB565
		{
		uint32_t u32, *pSrc;
		int x, y;
		uint16_t u16, *pDest;

			for (y=0; y<LCD_CY; y++)
			{
				pSrc = (uint32_t *)&pFB[iFBPitch * y * 2];
				pDest = (uint16_t *)&pScreen[iLCDPitch * y];
				for (x=0; x<LCD_CX; x++)
				{
					u32 = pSrc[0];
					pSrc += 2;					u16 = ((u32 >> 3) & 0x1f) | ((u32 >> 5) & 0x7e0) |
					((u32 >> 8) & 0xf800);
					*pDest++ = u16;
				}
			}
		}
	}
	else // 1:1
	{
		if (vinfo.bits_per_pixel == 16)
		{
			memcpy(pScreen, pFB, iFBPitch * vinfo.yres);
		}
		else // need to convert the pixels
		{
		uint32_t u32, *pSrc;
		int x, y;
		uint16_t u16, *pDest;

			for (y=0; y<LCD_CY; y++)
			{
				pSrc = (uint32_t *)&pFB[iFBPitch * y];
				pDest = (uint16_t *)&pScreen[iLCDPitch * y];
				for (x=0; x<LCD_CX; x++)
				{
					u32 = *pSrc++;
					u16 = ((u32 >> 3) & 0x1f) | ((u32 >> 5) & 0x7e0) |
					((u32 >> 8) & 0xf800);
					*pDest++ = u16;
				}
			}
		}
	}
#endif // _RPIZERO_
} /* FBCapture() */

static void CopyLoop(void)
{
int iChanged;
uint32_t u32Flags, u32Regions[32], *pRegions;
int i, j, k, x, y;

	// Capture the current framebuffer
	FBCapture();

	// divide display into 10 x 10 tiles (32x24 pixels each)
	iChanged = FindChangedRegion(pScreen, pAltScreen, LCD_CX, LCD_CY, iLCDPitch, iTileWidth, iTileHeight, u32Regions);
	if (iChanged) // some area of the image changed
	{
		// Copy the changed areas to our backup framebuffer
		k = 0;
		for (i=0; i<LCD_CY; i+= iTileHeight)
		{
			if (u32Regions[k++])
			{
				j = iTileHeight;
				if (i+j > LCD_CY) j = LCD_CY - i;
				memcpy(&pAltScreen[i*iLCDPitch], (void *)&pScreen[i*iLCDPitch], j * iLCDPitch); // copy regions which changed
			}
		}
		// Draw the changed tiles
		pRegions = u32Regions;
		for (y=0; y<LCD_CY; y+=iTileHeight)
		{
			u32Flags = *pRegions++; // next set of row tile flags
			for (x=0; x<LCD_CX; x += iTileWidth)
			{
				if (u32Flags & 1) // this tile is dirty
				{
					spilcdDrawTile(x, y, iTileWidth, iTileHeight, &pAltScreen[(y*iLCDPitch)+x*2], iLCDPitch);
				}
				u32Flags >>= 1; // shift down to next bit flag	
			}
		}

	}
} /* CopyLoop() */

static int ParseOpts(int argc, char *argv[])
{
    int i = 1;

    while (i < argc)
    {
        /* if it isn't a cmdline option, we're done */
        if (0 != strncmp("--", argv[i], 2))
            break;
        /* GNU-style separator to support files with -- prefix
         * example for a file named "--baz": ./foo --bar -- --baz
         */
        if (0 == strcmp("--", argv[i]))
        {
            i += 1;
            break;
        }
        /* test for each specific flag */
        if (0 == strcmp("--spi_bus", argv[i])) {
            iSPIChan = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--spi_freq", argv[i])) {
            iSPIFreq = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--flip", argv[i])) {
            i++; 
            bLCDFlip = 1;
        } else if (0 == strcmp("--lcd_dc", argv[i])) {
            iDC = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--lcd_rst", argv[i])) {
            iReset = atoi(argv[i+1]); 
            i += 2;
        } else if (0 == strcmp("--lcd_led", argv[i])) {
            iLED = atoi(argv[i+1]);
            i += 2; 
        } else if (0 == strcmp("--showfps",argv[i])) {        
            bShowFPS = 1;
            i++;
        }  else {
            fprintf(stderr, "Unknown parameter '%s'\n", argv[i]);
            exit(1);
        }   
    }       
    return i;

} /* ParseOpts() */

static void ShowHelp(void)
{
    printf(
        "BB-CP: copy framebuffer to SPI LCD\n"
        "Copyright (c) 2017 BitBank Software, Inc.\n"
        "Written by Larry Bank\n\n"
        "Options:\n"
        " --spi_bus <integer>      defaults to 0\n"
        " --spi_freq <integer>     defaults to 31250000\n"
        " --lcd_dc <pin number>    defaults to 18\n"
        " --lcd_rst <pin number>   defaults to 22\n"
        " --lcd_led <pin number>   defaults to 13\n"
        " --flip                   flips display 180 degrees\n"
        " --showfps                Show framerate\n"
        "\nExample usage:\n"
        "sudo ./bbcp --spi_bus 1 spi_freq 46000000 --flip\n"
    );
} /* ShowHelp() */

void *CopyThread(void *pArg)
{
int64_t ns;
uint64_t llTime, llFrameDelta, llTargetTime, llOldTime;
float fps;
int iVideoFrames = 0;

	llFrameDelta = 1000000000 / 60; // time slice in nanoseconds (60 FPS)
	llTargetTime = llOldTime = NanoClock() + llFrameDelta; // end of frame time

	while (bRunning)
	{
		CopyLoop(); // send the display to the LCD
		iVideoFrames++;
		llTime = NanoClock(); // get clock time in nanoseconds
		ns = llTargetTime - llTime;
		if (bShowFPS && (llTime - llOldTime) > 1000000000LL) // update every second
		{
			fps = (float)iVideoFrames;
			fps = fps * 1000000000.0;
			fps = fps / (float)(llTime-llOldTime);
			printf("%02.1f FPS\n", fps);
			iVideoFrames = 0;
			llOldTime = llTime;
		}
		if (ns > 4000LL)
		{
			NanoSleep(ns-4000LL);	// don't trust sleep for the last 4us
		}
		else
		{
			while (ns < 0)
			{
				ns += llFrameDelta;
				llTargetTime += llFrameDelta;
			}			
		}
		llTargetTime += llFrameDelta;
	} // while running
	return NULL;
} /* CopyThread() */

int main(int argc, char* argv[])
{
pthread_t tinfo;

	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}
	// Set default values
	bShowFPS = 0;
	iSPIChan = 0; // 0 for RPI, usually 1 for Orange Pi
	iSPIFreq = 31250000; // good for RPI, use higher for AllWinner
	bLCDFlip = 0;

	// These are the header pin numbers of the ILI9341 control lines
	// 18 means pin 18 on the 40 pin IO header
	iDC = 18; iReset = 22; iLED = 13;

	iTileWidth = 32; // for now - different values might be more efficient
	iTileHeight = 24;

	ParseOpts(argc, argv); // gather the command line parameters

	// Initialize the SPI_LCD library and get a pointer to /dev/fb0
        if (InitDisplay(bLCDFlip, iSPIChan, iSPIFreq, iDC, iReset, iLED))
	{
		printf("Error initializing the LCD/display\n");
		return 0;
	}

#ifndef _RPIZERO_
	if (vinfo.xres > 640)
		printf("Warning: the framebuffer is too large and will not be copied properly; sipported sizes are 640x480 and 320x240\n");
	if (vinfo.bits_per_pixel == 32)
		printf("Warning: the framebuffer bit depth is 32-bpp, ideally it should be 16-bpp for fastest results\n");
#endif // !_RPIZERO_

	// Start screen copy thread
	bRunning = 1;
        pthread_create(&tinfo, NULL, CopyThread, NULL);
	printf("Press ENTER to quit\n");

	getchar(); // wait for user to press enter

    // Quit library and free resources
	bRunning = 0; // tell background thread to stop
	NanoSleep(50000000LL); // wait 50ms for work to finish
	spilcdShutdown();
#ifdef _RPIZERO_
	vc_dispmanx_resource_delete(screen_resource);
	vc_dispmanx_display_close(display);
#endif // _RPIZERO_

   return 0;
} /* main() */
