/* Modern display for the original game: any window size / aspect ratio,
   borderless fullscreen, and a dynamic zoom (pixel scale), on top of the
   game's own windowed GDI mode. Not a reconstruction: an enhancement layer,
   enabled from the launcher. */
#ifndef RCT_DISPLAY_H
#define RCT_DISPLAY_H

/* Call after rct_load_image and before starting the game. Settings live in
   the [display] section of `ini` and are saved there as they change. */
void display_install(const char *ini);

#endif
