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

#include "rvgpu_private.h"

void
rvgpu_pipeline_init(struct rvgpu_device *device, struct rvgpu_pipeline *pipeline)
{
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);
}

void
rvgpu_pipeline_destroy(struct rvgpu_device *device, struct rvgpu_pipeline *pipeline,
                       const VkAllocationCallbacks *allocator)
{
   vk_object_base_finish(&pipeline->base);
   vk_free2(&device->vk.alloc, allocator, pipeline);
}
