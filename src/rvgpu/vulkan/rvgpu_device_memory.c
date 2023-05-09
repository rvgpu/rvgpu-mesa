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

#include "vk_common_entrypoints.h"
#include "vk_format.h"
#include "vk_util.h"
#include "vk_log.h"

#include "rvgpu_private.h"

void
rvgpu_device_memory_init(struct rvgpu_device_memory *mem, struct rvgpu_device *device,
                         struct rvgpu_winsys_bo *bo)
{
   memset(mem, 0, sizeof(*mem));
   vk_object_base_init(&device->vk, &mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY);

   mem->bo = bo;
}

void
rvgpu_device_memory_finish(struct rvgpu_device_memory *mem)
{
   vk_object_base_finish(&mem->base);
}

void
rvgpu_free_memory(struct rvgpu_device *device, const VkAllocationCallbacks *pAllocator,
                  struct rvgpu_device_memory *mem)
{
   if (mem == NULL)
      return;

   if (mem->bo) {
      device->ws->ops.buffer_destroy(device->ws, mem->bo);
      mem->bo = NULL;
   }

   rvgpu_device_memory_finish(mem);
   vk_free2(&device->vk.alloc, pAllocator, mem);
}

VkResult
rvgpu_alloc_memory(struct rvgpu_device *device, const VkMemoryAllocateInfo *pAllocateInfo,
                   const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem, bool is_internal)
{
   struct rvgpu_device_memory *mem;
   VkResult result;
   enum radeon_bo_domain domain;
   uint32_t flags = 0;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   const VkImportMemoryFdInfoKHR *import_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);
   const VkMemoryDedicatedAllocateInfo *dedicate_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   const VkExportMemoryAllocateInfo *export_info =
      vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);
   const VkImportMemoryHostPointerInfoEXT *host_ptr_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_HOST_POINTER_INFO_EXT);
   const struct VkMemoryAllocateFlagsInfo *flags_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_ALLOCATE_FLAGS_INFO);

   const struct wsi_memory_allocate_info *wsi_info =
      vk_find_struct_const(pAllocateInfo->pNext, WSI_MEMORY_ALLOCATE_INFO_MESA);

   if (pAllocateInfo->allocationSize == 0) {
      /* Apparently, this is allowed */
      *pMem = VK_NULL_HANDLE;
      return VK_SUCCESS;
   }

   mem =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   rvgpu_device_memory_init(mem, device, NULL);

   if (dedicate_info) {
      mem->image = rvgpu_image_from_handle(dedicate_info->image);
      mem->buffer = rvgpu_buffer_from_handle(dedicate_info->buffer);
   } else {
      mem->image = NULL;
      mem->buffer = NULL;
   }

   if (wsi_info && wsi_info->implicit_sync) {
      flags |= RADEON_FLAG_IMPLICIT_SYNC;

      /* Mark the linear prime buffer (aka the destination of the prime blit
       * as uncached.
       */
      if (mem->buffer)
         flags |= RADEON_FLAG_VA_UNCACHED;
   }

   float priority_float = 0.5;
   const struct VkMemoryPriorityAllocateInfoEXT *priority_ext =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_PRIORITY_ALLOCATE_INFO_EXT);
   if (priority_ext)
      priority_float = priority_ext->priority;

   uint64_t replay_address = 0;
   const VkMemoryOpaqueCaptureAddressAllocateInfo *replay_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO);
   if (replay_info && replay_info->opaqueCaptureAddress)
      replay_address = replay_info->opaqueCaptureAddress;

   unsigned priority = MIN2(RVGPU_BO_PRIORITY_APPLICATION_MAX - 1,
                            (int)(priority_float * RVGPU_BO_PRIORITY_APPLICATION_MAX));

   mem->user_ptr = NULL;

   if (import_info) {
      assert(import_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             import_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
      result = device->ws->ops.buffer_from_fd(device->ws, import_info->fd, priority, &mem->bo, NULL);
      if (result != VK_SUCCESS) {
         goto fail;
      } else {
         close(import_info->fd);
      }
#if 0
      if (mem->image && mem->image->plane_count == 1 &&
          !vk_format_is_depth_or_stencil(mem->image->vk.format) && mem->image->info.samples == 1 &&
          mem->image->vk.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
         struct radeon_bo_metadata metadata;
         device->ws->ops.buffer_get_metadata(device->ws, mem->bo, &metadata);

         struct rvgpu_image_create_info create_info = {.no_metadata_planes = true,
                                                       .bo_metadata = &metadata};

         /* This gives a basic ability to import radeonsi images
          * that don't have DCC. This is not guaranteed by any
          * spec and can be removed after we support modifiers. */
         result = radv_image_create_layout(device, create_info, NULL, mem->image);
         if (result != VK_SUCCESS) {
            device->ws->buffer_destroy(device->ws, mem->bo);
            goto fail;
         }
      }
#endif
   } else if (host_ptr_info) {
      assert(host_ptr_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);
      result = device->ws->ops.buffer_from_ptr(device->ws, host_ptr_info->pHostPointer,
                                           pAllocateInfo->allocationSize, priority, &mem->bo);
      if (result != VK_SUCCESS) {
         goto fail;
      } else {
         mem->user_ptr = host_ptr_info->pHostPointer;
      }
   } else {
      uint64_t alloc_size = align_u64(pAllocateInfo->allocationSize, 4096);
      uint32_t heap_index;

      heap_index =
         device->physical_device->memory_properties.memoryTypes[pAllocateInfo->memoryTypeIndex]
            .heapIndex;
      domain = device->physical_device->memory_domains[pAllocateInfo->memoryTypeIndex];
      flags |= device->physical_device->memory_flags[pAllocateInfo->memoryTypeIndex];

      if (export_info && export_info->handleTypes) {
         /* Setting RADEON_FLAG_GTT_WC in case the bo is spilled to GTT.  This is important when the
          * foreign queue is the display engine of iGPU.  The carveout of iGPU can be tiny and the
          * kernel driver refuses to spill without the flag.
          *
          * This covers any external memory user, including WSI.
          */
         if (domain == RADEON_DOMAIN_VRAM)
            flags |= RADEON_FLAG_GTT_WC;
      } else if (!import_info) {
         /* neither export nor import */
         flags |= RADEON_FLAG_NO_INTERPROCESS_SHARING;
      }

      if (flags_info && flags_info->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= RADEON_FLAG_REPLAYABLE;

      result = device->ws->ops.buffer_create(device->ws, alloc_size,
                                         64, domain,
                                         flags, priority, replay_address, &mem->bo);

      if (result != VK_SUCCESS) {
         goto fail;
      }

      mem->heap_index = heap_index;
      mem->alloc_size = alloc_size;
   }

   *pMem = rvgpu_device_memory_to_handle(mem);
   return VK_SUCCESS;

fail:
   rvgpu_free_memory(device, pAllocator, mem);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_AllocateMemory(VkDevice _device, const VkMemoryAllocateInfo *pAllocateInfo,
                     const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   return rvgpu_alloc_memory(device, pAllocateInfo, pAllocator, pMem, false);
}
