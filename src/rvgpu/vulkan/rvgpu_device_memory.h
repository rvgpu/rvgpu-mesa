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

#ifndef RVGPU_DEVICE_MEMORY_H__
#define RVGPU_DEVICE_MEMORY_H__

#include "rvgpu_image.h"
#include "rvgpu_buffer.h"
#include "rvgpu_winsys.h"

struct rvgpu_device_memory {
   struct vk_object_base base;
   struct rvgpu_winsys_bo *bo;
   /* for dedicated allocations */
   struct rvgpu_image *image;
   struct rvgpu_buffer *buffer;
   uint32_t heap_index;
   uint64_t alloc_size;
   void *map;
   void *user_ptr;
};

VkResult rvgpu_alloc_memory(struct rvgpu_device *device, 
                            const VkMemoryAllocateInfo *pAllocateInfo, 
                            const VkAllocationCallbacks *pAllocator, 
                            VkDeviceMemory *pMem, 
                            bool is_internal);

void rvgpu_free_memory(struct rvgpu_device *device,
                       const VkAllocationCallbacks *pAllocator,
                       struct rvgpu_device_memory *mem);

void rvgpu_device_memory_init(struct rvgpu_device_memory *mem, 
                              struct rvgpu_device *device,
                              struct rvgpu_winsys_bo *bo);

void rvgpu_device_memory_finish(struct rvgpu_device_memory *mem);

#endif // RVGPU_DEVICE_MEMORY_H__
