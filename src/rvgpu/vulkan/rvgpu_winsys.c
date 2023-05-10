/*
 * Copyright © 2023 Sietium Semiconductor.
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include <stdlib.h>

#include "vk_drm_syncobj.h"

#include "rvgpu_winsys.h"
#include "rvgpu_device.h"
#include "rvgpu_winsys_bo.h"

static const struct vk_sync_type *const *
rvgpu_winsys_get_sync_types(struct rvgpu_winsys *ws)
{
   return ws->sync_types;
}

void rvgpu_winsys_destroy(struct rvgpu_winsys *ws)
{
    ws->ops.destroy(ws);
}

struct rvgpu_winsys *
rvgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags)
{
   uint32_t drm_major, drm_minor, r;
   rvgpu_drm_device_handle dev;
   struct rvgpu_winsys *ws = NULL;

   r = rvgpu_drm_device_initialize(fd, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to initialize device.\n");
      return NULL;
   }

   ws = calloc(1, sizeof(struct rvgpu_winsys));
   if (!ws)
      goto fail;

   ws->dev = dev;

   int num_sync_types = 0;
   
   ws->syncobj_sync_type = vk_drm_syncobj_get_type(dev->fd);
   if (ws->syncobj_sync_type.features) {
      ws->sync_types[num_sync_types++] = &ws->syncobj_sync_type;
      if (!(ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE)) {
         ws->emulated_timeline_sync_type = vk_sync_timeline_get_type(&ws->syncobj_sync_type);
         ws->sync_types[num_sync_types++] = &ws->emulated_timeline_sync_type.sync;
      }
   }
   
   ws->sync_types[num_sync_types++] = NULL;

   ws->ops.get_sync_types = rvgpu_winsys_get_sync_types;
   rvgpu_winsys_bo_init_functions(ws);
   rvgpu_winsys_surface_init_functions(ws);

   return ws;

fail:
   // rvgpu_device_deinitialize(dev);
   return NULL;
}
