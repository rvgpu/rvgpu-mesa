/*
 * Copyright © 2023 Sietium Semiconductor.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <xf86drm.h>

#include "drm_ioctl.h"

int rvgpu_drm_device_initialize(int fd, 
                                uint32_t *major_version, 
                                uint32_t *minor_version, 
                                rvgpu_drm_device_handle *device_handle)
{
   struct rvgpu_drm_device *dev;
   drmVersionPtr version;

   *device_handle = NULL;

   dev = calloc(1, sizeof(struct rvgpu_drm_device));
   if (!dev) {
      fprintf(stderr, "%s: calloc failed\n", __func__);
      return -ENOMEM;
   }

   version = drmGetVersion(fd);
   *major_version = version->version_major;
   *minor_version = version->version_minor;
   drmFreeVersion(version);

   *device_handle = dev;
   return 0;
}

