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

#include <fcntl.h>
#include <stdio.h>

#include <sys/mman.h>

#include <linux/fb.h>

#include "graphics.h"

struct fbdev_pdata {
    struct minui_backend base;
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    int fd;
};

static void fbdev_set_active_framebuffer(struct minui_backend *, unsigned);
static void fbdev_blank(struct minui_backend *, bool);
static void fbdev_exit(struct minui_backend *);

struct minui_backend *fbdev_init(GGLSurface fb[NUM_BUFFERS])
{
    void *bits;

    struct fbdev_pdata *pdata = malloc(sizeof(*pdata));
    if (!pdata) {
        perror("cannot allocate fbdev data");
        return NULL;
    }

    pdata->fd = open("/dev/graphics/fb0", O_RDWR);
    if (pdata->fd < 0) {
        perror("cannot open fb0");
        goto err_open;
    }

    if (ioctl(pdata->fd, FBIOGET_VSCREENINFO, &pdata->vi) < 0) {
        perror("failed to get fb0 info");
        goto err_ioctl;
    }

    pdata->vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_BGRA_8888) {
      pdata->vi.red.offset     = 8;
      pdata->vi.red.length     = 8;
      pdata->vi.green.offset   = 16;
      pdata->vi.green.length   = 8;
      pdata->vi.blue.offset    = 24;
      pdata->vi.blue.length    = 8;
      pdata->vi.transp.offset  = 0;
      pdata->vi.transp.length  = 8;
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGBX_8888) {
      pdata->vi.red.offset     = 24;
      pdata->vi.red.length     = 8;
      pdata->vi.green.offset   = 16;
      pdata->vi.green.length   = 8;
      pdata->vi.blue.offset    = 8;
      pdata->vi.blue.length    = 8;
      pdata->vi.transp.offset  = 0;
      pdata->vi.transp.length  = 8;
    } else { /* RGB565*/
      pdata->vi.red.offset     = 11;
      pdata->vi.red.length     = 5;
      pdata->vi.green.offset   = 5;
      pdata->vi.green.length   = 6;
      pdata->vi.blue.offset    = 0;
      pdata->vi.blue.length    = 5;
      pdata->vi.transp.offset  = 0;
      pdata->vi.transp.length  = 0;
    }
    if (ioctl(pdata->fd, FBIOPUT_VSCREENINFO, &pdata->vi) < 0) {
        perror("failed to put fb0 info");
        goto err_ioctl;
    }

    if (ioctl(pdata->fd, FBIOGET_FSCREENINFO, &pdata->fi) < 0) {
        perror("failed to get fb0 info");
        goto err_ioctl;
    }

    bits = mmap(0, pdata->fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, pdata->fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        goto err_ioctl;
    }

    pdata->base.set_active_framebuffer = fbdev_set_active_framebuffer;
    pdata->base.blank = fbdev_blank;
    pdata->base.exit = fbdev_exit;
    pdata->base.double_buffering = 0;

    fb->version = sizeof(*fb);
    fb->width = pdata->vi.xres;
    fb->height = pdata->vi.yres;
    fb->stride = pdata->fi.line_length/PIXEL_SIZE;
    fb->data = bits;
    fb->format = PIXEL_FORMAT;
    memset(fb->data, 0, pdata->vi.yres * pdata->fi.line_length);

    fb++;

    /* check if we can use double buffering */
    if (pdata->vi.yres * pdata->fi.line_length * 2 > pdata->fi.smem_len)
        return &pdata->base;

    pdata->base.double_buffering = 1;

    fb->version = sizeof(*fb);
    fb->width = pdata->vi.xres;
    fb->height = pdata->vi.yres;
    fb->stride = pdata->fi.line_length/PIXEL_SIZE;
    fb->data = (void*) (((char*) bits) + pdata->vi.yres * pdata->fi.line_length);
    fb->format = PIXEL_FORMAT;
    memset(fb->data, 0, pdata->vi.yres * pdata->fi.line_length);

    return &pdata->base;

err_ioctl:
    close(pdata->fd);
err_open:
    free(pdata);
    return NULL;
}

static void fbdev_set_active_framebuffer(struct minui_backend *backend,
        unsigned n)
{
    struct fbdev_pdata *pdata = (struct fbdev_pdata *)backend;

    pdata->vi.yres_virtual = pdata->vi.yres * NUM_BUFFERS;
    pdata->vi.yoffset = n * pdata->vi.yres;
    pdata->vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (ioctl(pdata->fd, FBIOPUT_VSCREENINFO, &pdata->vi) < 0) {
        perror("active fb swap failed");
    }
}

static void fbdev_exit(struct minui_backend *backend)
{
    struct fbdev_pdata *pdata = (struct fbdev_pdata *)backend;

    close(pdata->fd);
    pdata->fd = -1;
    free(pdata);
}

static void fbdev_blank(struct minui_backend *backend, bool blank)
{
    struct fbdev_pdata *pdata = (struct fbdev_pdata *)backend;
    int ret;

    ret = ioctl(pdata->fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
}
