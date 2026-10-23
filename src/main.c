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

/* One program: the world in a window, or with --headless, on its own as CSV.
 * Builds with SYN3D_NO_WINDOW (the determinism tests' other compilers) have
 * only the headless world. */

#include <string.h>

#include "headless.h"
#ifndef SYN3D_NO_WINDOW
#include "app/app.h"
#endif

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--headless") == 0) {
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
            return headless_main(argc - 1, argv);
        }
#ifdef SYN3D_NO_WINDOW
    return headless_main(argc, argv);
#else
    return app_main(argc, argv);
#endif
}
