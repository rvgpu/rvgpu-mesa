/*
 * Copyright © 2023 Sietium Semiconductor
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <xf86drm.h>

#include <vulkan/vulkan.h>

#include "rvgpu_device.h"

#define DEF_DRIVER(str_name)                        \
   {                                                \
      .name = str_name, .len = sizeof(str_name) - 1 \
   }


int rvgpu_device_initialize(int fd, 
                            uint32_t *major_version, 
                            uint32_t *minor_version, 
                            rvgpu_device_handle *device_handle)
{
   struct rvgpu_device *dev;
   drmVersionPtr version;

   *device_handle = NULL;

   dev = calloc(1, sizeof(struct rvgpu_device));
   if (!dev) {
      fprintf(stderr, "%s: calloc failed\n", __func__);
      return -ENOMEM;
   }

   version = drmGetVersion(fd);

   dev->flink_fd = dev->fd;
   dev->major_version = version->version_major;
   dev->minor_version = version->version_minor;
   drmFreeVersion(version);

   *major_version = dev->major_version;
   *minor_version = dev->minor_version;

   *device_handle = dev;
   return 0;
}
