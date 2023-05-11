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

#ifndef __RVGPU_WINSYS_H__
#define __RVGPU_WINSYS_H__

#include "vk_sync.h"
#include "vk_sync_timeline.h"

#include "drm_ioctl.h"

#include "rvgpu_winsys_surface.h"

enum rvgpu_ctx_priority {
   RVGPU_CTX_PRIORITY_INVALID = -1,
   RVGPU_CTX_PRIORITY_LOW = 0,
   RVGPU_CTX_PRIORITY_MEDIUM,
   RVGPU_CTX_PRIORITY_HIGH,
   RVGPU_CTX_PRIORITY_REALTIME,
};

struct rvgpu_winsys_bo {
   uint64_t va;
   uint64_t size;
};

struct rvgpu_winsys;

struct rvgpu_winsys_ops {
   void (*destroy)(struct rvgpu_winsys *ws);

   void (*buffer_destroy)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo);
   void *(*buffer_map)(struct rvgpu_winsys_bo *bo);

   int (*surface_init)(struct rvgpu_winsys *ws, const struct rvgpu_surf_info *surf_info,
                       struct rvgpu_surf *surf);

   const struct vk_sync_type *const *(*get_sync_types)(struct rvgpu_winsys *ws);

   // BO Interface
   VkResult (*bo_import)(struct rvgpu_winsys *ws, int fd, struct rvgpu_winsys_bo **out_bo);
   VkResult (*bo_create)(struct rvgpu_winsys *ws, uint64_t size, uint32_t flags, struct rvgpu_winsys_bo **out_bo);

};

struct rvgpu_winsys {
   struct rvgpu_winsys_ops ops;

   rvgpu_drm_device_handle dev;

   const struct vk_sync_type *sync_types[3];
   struct vk_sync_type syncobj_sync_type;
   struct vk_sync_timeline_type emulated_timeline_sync_type;
};

void rvgpu_winsys_destroy(struct rvgpu_winsys *ws);
struct rvgpu_winsys * rvgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags);

void rvgpu_winsys_bo_init_functions(struct rvgpu_winsys *ws);

#endif // __RVGPU_WINSYS_H__
