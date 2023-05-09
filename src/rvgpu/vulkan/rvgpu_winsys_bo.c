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

#include "util/u_memory.h"

#include "rvgpu_winsys.h"
#include "rvgpu_winsys_bo.h"

static VkResult
rvgpu_winsys_bo_create(struct rvgpu_winsys *ws, uint64_t size, unsigned alignment,
                       enum radeon_bo_domain initial_domain, enum radeon_bo_flag flags,
                       unsigned priority, uint64_t replay_address,
                       struct rvgpu_winsys_bo **out_bo)
{
   struct rvgpu_winsys_bo *bo;
   struct rvgpu_bo_alloc_request request = {0};
   struct rvgpu_map_range *ranges = NULL;
   uint64_t va = 0;
   int r;
   VkResult result = VK_SUCCESS;

   /* Just be robust for callers that might use NULL-ness for determining if things should be freed.
    */
   *out_bo = NULL;

   bo = CALLOC_STRUCT(rvgpu_winsys_bo);
   if (!bo) {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   unsigned virt_alignment = alignment;

   assert(!replay_address || (flags & RADEON_FLAG_REPLAYABLE));

   va = (uint64_t)malloc(size);

   bo->va = va;
   bo->size = size;

   *out_bo = (struct rvgpu_winsys_bo *)bo;
   return VK_SUCCESS;
}

void
rvgpu_winsys_bo_init_functions(struct rvgpu_winsys *ws)
{
   ws->ops.buffer_create = rvgpu_winsys_bo_create;
   // ws->ops.buffer_destroy = radv_amdgpu_winsys_bo_destroy;
   // ws->ops.buffer_map = radv_amdgpu_winsys_bo_map;
   // ws->ops.buffer_unmap = radv_amdgpu_winsys_bo_unmap;
   // ws->ops.buffer_from_ptr = radv_amdgpu_winsys_bo_from_ptr;
   // ws->ops.buffer_from_fd = radv_amdgpu_winsys_bo_from_fd;
   // ws->ops.buffer_get_fd = radv_amdgpu_winsys_get_fd;
   // ws->ops.buffer_set_metadata = radv_amdgpu_winsys_bo_set_metadata;
   // ws->ops.buffer_get_metadata = radv_amdgpu_winsys_bo_get_metadata;
   // ws->ops.buffer_virtual_bind = radv_amdgpu_winsys_bo_virtual_bind;
   // ws->ops.buffer_get_flags_from_fd = radv_amdgpu_bo_get_flags_from_fd;
   // ws->ops.buffer_make_resident = radv_amdgpu_winsys_bo_make_resident;
   // ws->ops.dump_bo_ranges = radv_amdgpu_dump_bo_ranges;
   // ws->ops.dump_bo_log = radv_amdgpu_dump_bo_log;
}
