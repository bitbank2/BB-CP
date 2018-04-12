BB-CP - A replacement for FBTFT + FBCP

The purpose of BB-CP is to provide an efficient means of mirroring the display
of an ARM SBC onto an ili9341 LCD. For many gaming projects based on the 
Raspberry Pi and other ARM boards, the ili9341 320x240 LCD screens provide
an inexpensive and flexible display. The only limitation of these displays is
that by using a serial connection (SPI), the framerate is limited. In most 
cases, the maximum framerate (every pixel rewritten) that can be attained is
30-33 frames per second. This is a reasonable framerate, but for gamers,
getting as close to 60 fps would be ideal. Since the game emulators were 
written to draw each frame on the framebuffer, they either need to be changed
or a middle layer needs to be added to copy the contents to the LCD. The fbtft
and fbcp projects are that middle layer. FBTFT is a kernel driver which creates
a virtual framebuffer (/dev/fb1). Any drawing to that memory is detected by
page faults (4k memory pages) and sent to the LCD in a timed loop. The second
part of this scheme is FBCP. This is a user-level program which copies the
image data from /dev/fb0 and scales it down to the size of /dev/fb1, then
copies it to that framebuffer. On Raspberry Pi boards, there is GPU assistance
to do the scaling/copying from an API called dispmanx. The flaw in this scheme
is that FBTFT and FBCP have no knowledge of each other nor when the game has
drawn a new screen. They handle this by both running in a loop with a sleep()
to try to run at a reasonable rate. This introduces latency for each.

BB-CP solves some of these issues by doing the job of both FBTFT and FBCP.
The BB-CP code also improves the framerate by comparing the current frame to
the previous and only sending the changed "tiles" to the display. For games
which don't have complex/scrolling backgrounds, this can improve the framerate.
 
Requirements:
1) An ARM SBC (Raspberry Pi, Orange Pi, NanoPi)
2) My SPI_LCD library (https://github.com/bitbank2/SPI_LCD)
3) PIGPIO library (if using RPI hardware - https://github.com/joan2937/pigpio)

How to build
------------
clone the SPI_LCD project
edit spi_lcd.c to use the SPI/GPIO access method appropriate for your board
make SPI_LCD (cd SPI_LCD & make)
make BB-CP (cd BB-CP & make)

How to run
----------
sudo ./bbcp <options>

Press ENTER to quit bbcp. This is necessary because if you kill it by pressing
ctrl-c, some SPI libraries (e.g. PIGPIO) won't shutdown and to run it again
will require a reboot.<br>
<br>

If you find this code useful, please consider buying me a cup of coffee<br>
[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=SR4F44J2UR8S4)
