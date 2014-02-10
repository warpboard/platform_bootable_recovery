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

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/cdefs.h>
#include <sys/mman.h>

#include <adf/adf.h>

#include "graphics.h"

struct adf_pdata {
    struct minui_backend base;
    int intf_fd;
    adf_id_t eng_id;
    GGLSurface *surfaces;
};

struct adf_surface_pdata {
    int fd;
    __u32 format;
    __u32 offset;
    __u32 pitch;
};

static void adf_set_active_framebuffer(struct minui_backend *, unsigned);
static void adf_blank(struct minui_backend *, bool);
static void adf_exit(struct minui_backend *);

static int adf_surface_init(struct adf_pdata *pdata, __u32 format,
        struct drm_mode_modeinfo *mode, GGLSurface *fb)
{
    struct adf_surface_pdata *surf;

    surf = malloc(sizeof(*surf));
    if (!surf)
        return -ENOMEM;

    surf->fd = adf_interface_simple_buffer_alloc(pdata->intf_fd, mode->hdisplay,
            mode->vdisplay, format, &surf->offset, &surf->pitch);
    if (surf->fd < 0)
        goto err_alloc;
    surf->format = format;

    memset(fb, 0, sizeof(*fb));
    fb->version = sizeof(*fb);
    fb->width = mode->hdisplay;
    fb->height = mode->vdisplay;
    fb->stride = surf->pitch / PIXEL_SIZE;
    fb->format = PIXEL_FORMAT;

    fb->data = mmap(NULL, surf->pitch * fb->height, PROT_WRITE, MAP_SHARED,
            surf->fd, surf->offset);
    if (fb->data == MAP_FAILED)
        goto err_mmap;

    fb->reserved = surf;
    return 0;

err_mmap:
    close(surf->fd);
err_alloc:
    free(surf);
    return -errno;
}

static int adf_interface_init(struct adf_pdata *pdata, __u32 format)
{
    struct adf_interface_data intf_data;
    int ret = 0;
    int err;

    err = adf_get_interface_data(pdata->intf_fd, &intf_data);
    if (err < 0)
        return err;

    err = adf_surface_init(pdata, format, &intf_data.current_mode,
            &pdata->surfaces[0]);
    if (err < 0) {
        fprintf(stderr, "allocating fb surface 0 failed: %s\n", strerror(-err));
        ret = err;
        goto done;
    }

    err = adf_surface_init(pdata, format, &intf_data.current_mode,
            &pdata->surfaces[1]);
    if (err < 0) {
        fprintf(stderr, "allocating fb surface 1 failed: %s\n", strerror(-err));
        memset(&pdata->surfaces[1], 0, sizeof(pdata->surfaces[1]));
        pdata->base.double_buffering = 0;
    } else {
        pdata->base.double_buffering = 1;
    }

    pdata->base.set_active_framebuffer = adf_set_active_framebuffer;
    pdata->base.blank = adf_blank;
    pdata->base.exit = adf_exit;

done:
    adf_free_interface_data(&intf_data);
    return ret;
}

static int adf_device_init(struct adf_pdata *pdata, struct adf_device *dev,
        __u32 format, GGLSurface fb[NUM_BUFFERS])
{
    adf_id_t intf_id;
    int err;

    pdata->surfaces = fb;

    err = adf_find_simple_post_configuration(dev, &format, 1, &intf_id,
            &pdata->eng_id);
    if (err < 0)
        return err;

    err = adf_device_attach(dev, pdata->eng_id, intf_id);
    if (err < 0 && err != -EALREADY)
        return err;

    pdata->intf_fd = adf_interface_open(dev, intf_id, O_RDWR);
    if (pdata->intf_fd < 0)
        return pdata->intf_fd;

    err = adf_interface_init(pdata, format);
    if (err < 0)
        close(pdata->intf_fd);

    return err;
}

struct minui_backend *adf_init(GGLSurface fb[NUM_BUFFERS])
{
    uint32_t format;
    adf_id_t *dev_ids = NULL;
    ssize_t n_dev_ids, i;
    bool found_dev = false;
    struct adf_pdata *pdata = malloc(sizeof(*pdata));

    if (!pdata) {
        perror("allocating adf backend failed");
        return NULL;
    }

    switch (PIXEL_FORMAT) {
    case GGL_PIXEL_FORMAT_BGRA_8888:
        format = DRM_FORMAT_BGRA8888;
        break;

    case GGL_PIXEL_FORMAT_RGBX_8888:
        format = DRM_FORMAT_RGBX8888;
        break;

    default:
        format = DRM_FORMAT_RGB565;
        break;
    }

    n_dev_ids = adf_devices(&dev_ids);
    if (n_dev_ids == 0) {
        errno = ENOENT;
        goto err;
    } else if (n_dev_ids < 0) {
        fprintf(stderr, "enumerating adf devices failed: %s\n",
                strerror(-n_dev_ids));
        goto err;
    }

    for (i = 0; i < n_dev_ids && !found_dev; i++) {
        struct adf_device dev;

        int err = adf_device_open(dev_ids[i], O_RDWR, &dev);
        if (err < 0) {
            fprintf(stderr, "opening adf device %u failed: %s\n", dev_ids[i],
                    strerror(-err));
            continue;
        }

        err = adf_device_init(pdata, &dev, format, fb);
        if (err < 0)
            fprintf(stderr, "initializing adf device %u failed: %s\n",
                    dev_ids[i], strerror(err));
        else
            found_dev = true;

        adf_device_close(&dev);
    }

    if (!found_dev)
        goto err;

    return &pdata->base;

err:
    free(dev_ids);
    free(pdata);
    return NULL;
}

static void adf_set_active_framebuffer(struct minui_backend *backend,
        unsigned n)
{
    struct adf_pdata *pdata = (struct adf_pdata *)backend;
    GGLSurface *fb = &pdata->surfaces[n];
    struct adf_surface_pdata *surf = fb->reserved;

    int fence_fd = adf_interface_simple_post(pdata->intf_fd, pdata->eng_id,
            fb->width, fb->height, surf->format, surf->fd, surf->offset,
            surf->pitch, -1);
    if (fence_fd >= 0)
        close(fence_fd);
}

static void adf_blank(struct minui_backend *backend, bool blank)
{
    struct adf_pdata *pdata = (struct adf_pdata *)backend;
    adf_interface_blank(pdata->intf_fd,
            blank ? DRM_MODE_DPMS_OFF : DRM_MODE_DPMS_ON);
}

static void adf_surface_destroy(GGLSurface *fb)
{
    struct adf_surface_pdata *surf = fb->reserved;

    munmap(fb->data, surf->pitch * fb->height);
    close(surf->fd);
    free(surf);
}

static void adf_exit(struct minui_backend *backend)
{
    struct adf_pdata *pdata = (struct adf_pdata *)backend;

    adf_surface_destroy(&pdata->surfaces[0]);
    if (pdata->surfaces[1].reserved)
        adf_surface_destroy(&pdata->surfaces[1]);
    close(pdata->intf_fd);
    free(pdata);
}
