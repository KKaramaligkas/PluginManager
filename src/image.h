/*
    Plugin Manager for ARK-5
    image.h: PNG loading into textures.
*/

#ifndef PM_IMAGE_H
#define PM_IMAGE_H

#include "gfx.h"

/* Loads a PNG file (max 512x512). Returns NULL on any error. */
texture *image_load_png(const char *path);

/* Loads the ICON0.PNG embedded in an EBOOT.PBP. */
texture *image_load_pbp_icon(const char *pbp_path);

/* Returns 1 when the file looks like a complete PNG no larger than 512x512. */
int image_png_valid(const char *path);

#endif
