/*
 * This file is part of libmtk.
 * Copyright (C) 2026 Martin Lind
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (no later version applies).
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#ifndef MTK_INTERNAL_H
#define MTK_INTERNAL_H

#include <mtk/mtk.h>

struct MtkTimer {
    int id;
    uint64_t deadline;
    void (*cb)(void *data);
    void *data;
    bool dead;
};

/* Shade a 0xRRGGBB color by factor (1.0 = unchanged). */
unsigned long mtk_shade(MtkApp *app, uint32_t rgb, double factor);
unsigned long mtk_alloc_color(MtkApp *app, uint32_t rgb);

char *mtk_strdup(const char *s);

#endif
