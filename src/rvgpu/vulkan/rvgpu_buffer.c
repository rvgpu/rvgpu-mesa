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
#include "vk_buffer.h"
#include "vk_common_entrypoints.h"

#include "rvgpu_private.h"

static void
rvgpu_get_buffer_memory_requirements(struct rvgpu_device *device, VkDeviceSize size,
                                     VkBufferCreateFlags flags, VkBufferCreateFlags usage,
                                     VkMemoryRequirements2 *pMemoryRequirements)
{
   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      ((1u << device->physical_device->memory_properties.memoryTypeCount) - 1u);

   if (flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
      pMemoryRequirements->memoryRequirements.alignment = 4096;
   else
      pMemoryRequirements->memoryRequirements.alignment = 16;

   pMemoryRequirements->memoryRequirements.size =
      align64(size, pMemoryRequirements->memoryRequirements.alignment);

   vk_foreach_struct(ext, pMemoryRequirements->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
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
   struct rvgpu_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer =
      vk_buffer_create(&device->vk, pCreateInfo, pAllocator, sizeof(*buffer));
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   *pBuffer = rvgpu_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetDeviceBufferMemoryRequirements(VkDevice _device,
                                        const VkDeviceBufferMemoryRequirements *pInfo,
                                        VkMemoryRequirements2 *pMemoryRequirements)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);

   rvgpu_get_buffer_memory_requirements(device, pInfo->pCreateInfo->size, pInfo->pCreateInfo->flags,
                                        pInfo->pCreateInfo->usage, pMemoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_BindBufferMemory2(VkDevice _device, uint32_t bindInfoCount,
                        const VkBindBufferMemoryInfo *pBindInfos)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      RVGPU_FROM_HANDLE(rvgpu_device_memory, mem, pBindInfos[i].memory);
      RVGPU_FROM_HANDLE(rvgpu_buffer, buffer, pBindInfos[i].buffer);

      if (mem->alloc_size) {
         VkBufferMemoryRequirementsInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
            .buffer = pBindInfos[i].buffer,
         };
         VkMemoryRequirements2 reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
         };

         vk_common_GetBufferMemoryRequirements2(_device, &info, &reqs);

         if (pBindInfos[i].memoryOffset + reqs.memoryRequirements.size > mem->alloc_size) {
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "Device memory object too small for the buffer.\n");
         }
      }

      buffer->bo = mem->bo;
      buffer->offset = pBindInfos[i].memoryOffset;
   }
   return VK_SUCCESS;
}
