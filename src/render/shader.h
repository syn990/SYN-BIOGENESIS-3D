/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Taken from SYN-TREK's src/render/shader.h.
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

#ifndef RENDER_SHADER_H
#define RENDER_SHADER_H

#include <epoxy/gl.h>

/* Compiles one shader stage. Returns 0 on failure (compile log printed to
   stderr), never a partially-valid handle. */
GLuint shader_compile(GLenum type, const char *src);

/* Links a vertex+fragment pair into a program. Consumes (deletes) both input
   shaders either way, matching normal GL ownership -- a linked program keeps
   its own copy. Returns 0 on failure (link log printed to stderr). */
GLuint shader_link_program(GLuint vs, GLuint fs);

/* Both in one: a program from vertex and fragment source, or 0. */
GLuint shader_program(const char *vs_src, const char *fs_src);

#endif
