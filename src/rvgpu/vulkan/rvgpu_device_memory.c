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
   
   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
   
   if (pAllocateInfo->allocationSize == 0) {
      /* Apparently, this is allowed */
      *pMem = VK_NULL_HANDLE;
      return VK_SUCCESS;
   }
   
   mem = vk_object_alloc(&device->vk, pAllocator, sizeof(*mem),
                         VK_OBJECT_TYPE_DEVICE_MEMORY);
   if (mem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   
   const VkImportMemoryFdInfoKHR *fd_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);
   
   const VkImportMemoryHostPointerInfoEXT *host_ptr_info = 
       vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_HOST_POINTER_INFO_EXT);
   
   if (fd_info && !fd_info->handleType)
      fd_info = NULL;
   
   if (fd_info) {
      assert(
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
     
         result = device->ws->ops.bo_import(device->ws, fd_info->fd, &mem->bo);
      if (result != VK_SUCCESS)
         goto err_vk_object_free_mem;

      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
   } else if (host_ptr_info) {
       assert(host_ptr_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);

       // TODO: set the mem->user_ptr 
       printf("!!!!! [rvgpu_alloc_memory] should set the mem->user_ptr \n");
   } else {
      result = device->ws->ops.bo_create(device->ws,
                                         pAllocateInfo->allocationSize,
                                         0,
                                         &mem->bo);
      
      uint64_t alloc_size = align_u64(pAllocateInfo->allocationSize, 4096);
      mem->alloc_size = alloc_size;
      if (result != VK_SUCCESS)
         goto err_vk_object_free_mem;
   }

   assert(mem->bo);

   *pMem = rvgpu_device_memory_to_handle(mem);

   return VK_SUCCESS;

err_vk_object_free_mem:
   vk_object_free(&device->vk, pAllocator, mem);


   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_AllocateMemory(VkDevice _device, const VkMemoryAllocateInfo *pAllocateInfo,
                     const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   return rvgpu_alloc_memory(device, pAllocateInfo, pAllocator, pMem, false);
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_MapMemory2KHR(VkDevice _device, const VkMemoryMapInfoKHR *pMemoryMapInfo, void **ppData)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   RVGPU_FROM_HANDLE(rvgpu_device_memory, mem, pMemoryMapInfo->memory);

   if (mem->user_ptr)
      *ppData = mem->user_ptr;
   else
      *ppData = device->ws->ops.buffer_map(mem->bo);

   if (*ppData) {
      *ppData = (uint8_t *)*ppData + pMemoryMapInfo->offset;
      return VK_SUCCESS;
   }

   return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
}
