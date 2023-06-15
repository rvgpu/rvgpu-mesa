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

static VkResult
rvgpu_winsys_bo_create(struct rvgpu_winsys *ws, uint64_t size, uint32_t flags, struct rvgpu_winsys_bo **out_bo)
{
   struct rvgpu_winsys_bo *bo;
   uint64_t va = 0;

   *out_bo = NULL;

   bo = CALLOC_STRUCT(rvgpu_winsys_bo);
   if (!bo) {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   // use malloc now
   va = (uint64_t)malloc(size);
   
   bo->va = va;
   bo->size = size;

   *out_bo = (struct rvgpu_winsys_bo *)bo;

   return VK_SUCCESS;
}

static void 
rvgpu_winsys_bo_destroy(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo)
{
   if (bo->va) {
      free((void *)bo->va);
   }

   free((void *)bo);
}

static void *
rvgpu_winsys_bo_map(struct rvgpu_winsys_bo *bo)
{
   void *data;
   data = (void *)bo->va;

   return data;
}

static void rvgpu_winsys_bo_unmap(struct rvgpu_winsys_bo *bo)
{

}

static VkResult
rvgpu_winsys_bo_import(struct rvgpu_winsys *ws, int fd, struct rvgpu_winsys_bo **out_bo)
{
   return VK_SUCCESS;
}

void
rvgpu_winsys_bo_init_functions(struct rvgpu_winsys *ws)
{
   ws->ops.bo_map = rvgpu_winsys_bo_map;
   ws->ops.bo_unmap = rvgpu_winsys_bo_unmap;

   ws->ops.bo_create = rvgpu_winsys_bo_create;
   ws->ops.bo_destroy = rvgpu_winsys_bo_destroy;
   ws->ops.bo_import = rvgpu_winsys_bo_import;
}
