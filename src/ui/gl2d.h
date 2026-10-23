/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. It is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License (LICENSE) for more details.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* The few drawing calls the SYN-OS skin makes (src/ui/skin.c), on OpenGL:
 * filled rectangles, coloured triangles in SDL_Vertex form, and textures
 * (text). SYN-BIOGENESIS draws the same skin with SDL_Renderer; here it
 * draws over the 3D scene in the same window. Everything goes into one
 * batch, flushed when the texture changes or the frame's interface ends:
 * a whole bar and card are a handful of draw calls. Coordinates are window
 * pixels, top-left origin, as SDL's mouse. */
#ifndef UI_GL2D_H
#define UI_GL2D_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    unsigned id;
    int w, h;
} Gl2dTex;

bool gl2d_init(void);
void gl2d_shutdown(void);
/* Start drawing the interface over a w x h frame; end flushes it. */
void gl2d_begin(int w, int h);
void gl2d_end(void);

/* A texture from an SDL surface of any format (TTF's text). */
bool gl2d_tex_from_surface(SDL_Surface *s, Gl2dTex *out);
void gl2d_tex_destroy(Gl2dTex *t);
void gl2d_tex_draw(const Gl2dTex *t, float x, float y, float w, float h, SDL_Color tint);

void gl2d_fill(float x, float y, float w, float h, SDL_Color c);
/* SDL_RenderGeometry without a texture. */
void gl2d_geometry(const SDL_Vertex *v, int nv, const int *idx, int ni);

#endif
