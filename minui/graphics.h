/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _GRAPHICS_H_
#define _GRAPHICS_H_

#include <stdbool.h>
#include <pixelflinger/pixelflinger.h>

struct minui_backend {
    void (*set_active_framebuffer)(struct minui_backend *, unsigned);
    void (*blank)(struct minui_backend *, bool);
    void (*exit)(struct minui_backend *);
    bool double_buffering;
};

#if defined(RECOVERY_BGRA)
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_BGRA_8888
#define PIXEL_SIZE   4
#elif defined(RECOVERY_RGBX)
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGBX_8888
#define PIXEL_SIZE   4
#else
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGB_565
#define PIXEL_SIZE   2
#endif

#define NUM_BUFFERS 2

struct minui_backend *fbdev_init(GGLSurface fb[NUM_BUFFERS]);

#endif /* _GRAPHICS_H_ */
