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

#include "vk_format.h"
#include "vk_util.h"
#include "vk_log.h"

#include "rvgpu_private.h"

static void
rvgpu_destroy_buffer(struct rvgpu_device *device, const VkAllocationCallbacks *pAllocator,
                     struct rvgpu_buffer *buffer)
{  
   if ((buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) && buffer->bo)
      device->ws->ops.buffer_destroy(device->ws, buffer->bo);
   
   rvgpu_buffer_finish(buffer); 
   vk_free2(&device->vk.alloc, pAllocator, buffer);
}

VkResult
rvgpu_create_buffer(struct rvgpu_device *device, const VkBufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer, bool is_internal)
{
   struct rvgpu_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*buffer), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_init(&device->vk, &buffer->vk, pCreateInfo);
   buffer->bo = NULL;
   buffer->offset = 0;

   if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      enum radeon_bo_flag flags = RADEON_FLAG_VIRTUAL;
      if (pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= RADEON_FLAG_REPLAYABLE;

      uint64_t replay_address = 0;
      const VkBufferOpaqueCaptureAddressCreateInfo *replay_info =
         vk_find_struct_const(pCreateInfo->pNext, BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
      if (replay_info && replay_info->opaqueCaptureAddress)
         replay_address = replay_info->opaqueCaptureAddress;

      VkResult result =
         device->ws->ops.buffer_create(device->ws, align64(buffer->vk.size, 4096), 4096, 0, flags,
                                   RVGPU_BO_PRIORITY_VIRTUAL, replay_address, &buffer->bo);
      if (result != VK_SUCCESS) {
         rvgpu_destroy_buffer(device, pAllocator, buffer);
         return vk_error(device, result);
      }
   }

   *pBuffer = rvgpu_buffer_to_handle(buffer);
   return VK_SUCCESS;
}

void
rvgpu_buffer_finish(struct rvgpu_buffer *buffer)
{
   vk_buffer_finish(&buffer->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateBuffer(VkDevice _device, const VkBufferCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   return rvgpu_create_buffer(device, pCreateInfo, pAllocator, pBuffer, false);
}
