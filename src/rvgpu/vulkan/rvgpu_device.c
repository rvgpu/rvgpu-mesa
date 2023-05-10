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
#include "vk_util.h"
#include "vk_log.h"

#include "rvgpu_private.h"
#include "rvgpu_entrypoints.h"

#define DEF_DRIVER(str_name)                        \
   {                                                \
      .name = str_name, .len = sizeof(str_name) - 1 \
   }

struct dispatch_table_builder {
   struct vk_device_dispatch_table *tables[RVGPU_DISPATCH_TABLE_COUNT];
   bool used[RVGPU_DISPATCH_TABLE_COUNT];
   bool initialized[RVGPU_DISPATCH_TABLE_COUNT];
};

static void
add_entrypoints(struct dispatch_table_builder *b,
                const struct vk_device_entrypoint_table *entrypoints,
                enum rvgpu_dispatch_table table)
{
   for (int32_t i = table - 1; i >= RVGPU_DEVICE_DISPATCH_TABLE; i--) {
      if (i == RVGPU_DEVICE_DISPATCH_TABLE || b->used[i]) {
         vk_device_dispatch_table_from_entrypoints(b->tables[i], entrypoints, !b->initialized[i]);
         b->initialized[i] = true;
      }
   }

   if (table < RVGPU_DISPATCH_TABLE_COUNT)
      b->used[table] = true;
}

static void
init_dispatch_tables(struct rvgpu_device *device, struct rvgpu_physical_device *physical_device)
{
   struct dispatch_table_builder b = {0};
   b.tables[RVGPU_DEVICE_DISPATCH_TABLE] = &device->vk.dispatch_table;

   add_entrypoints(&b, &rvgpu_device_entrypoints, RVGPU_DISPATCH_TABLE_COUNT);
   add_entrypoints(&b, &wsi_device_entrypoints, RVGPU_DISPATCH_TABLE_COUNT);
   add_entrypoints(&b, &vk_common_device_entrypoints, RVGPU_DISPATCH_TABLE_COUNT);
}

static VkResult
rvgpu_check_status(struct vk_device *vk_device)
{
   return VK_SUCCESS;
}


VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, physical_device, physicalDevice);
   VkResult result;
   struct rvgpu_device *device;

   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator, sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device) {
      return vk_error(physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   result = vk_device_init(&device->vk, &physical_device->vk, NULL, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return result;
   }

   /* Create one context per queue priority. */
   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
      uint32_t qfi = queue_create->queueFamilyIndex;
      const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority = 
                vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);

      device->queues[qfi] =
         vk_alloc(&device->vk.alloc, queue_create->queueCount * sizeof(struct rvgpu_queue), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!device->queues[qfi]) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail_queue;
      }

      memset(device->queues[qfi], 0, queue_create->queueCount * sizeof(struct rvgpu_queue));

      device->queue_count[qfi] = queue_create->queueCount;

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         result = rvgpu_queue_init(device, &device->queues[qfi][q], q, queue_create, global_priority);
         if (result != VK_SUCCESS)
            goto fail_queue;
      }
   }

   init_dispatch_tables(device, physical_device);

   device->vk.command_buffer_ops = &rvgpu_cmd_buffer_ops;
   device->vk.check_status = rvgpu_check_status;

   device->instance = physical_device->instance;
   device->physical_device = physical_device;

   device->ws = physical_device->ws;

   *pDevice = rvgpu_device_to_handle(device);
   return VK_SUCCESS;
fail_queue:
   for (unsigned i = 0; i < RVGPU_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++) {
         rvgpu_queue_finish(&device->queues[i][q]);
      }
      if (device->queue_count[i]) {
         vk_free(&device->vk.alloc, device->queues[i]);
      }
   }

   return result;
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetImageMemoryRequirements2(VkDevice _device, const VkImageMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   RVGPU_FROM_HANDLE(rvgpu_image, image, pInfo->image);

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      ((1u << device->physical_device->memory_properties.memoryTypeCount) - 1u) &
      ~device->physical_device->memory_types_32bit;

   pMemoryRequirements->memoryRequirements.size = image->size;
   pMemoryRequirements->memoryRequirements.alignment = image->alignment;

   vk_foreach_struct(ext, pMemoryRequirements->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation =
            image->shareable && image->vk.tiling != VK_IMAGE_TILING_LINEAR;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_BindImageMemory2(VkDevice _device, uint32_t bindInfoCount,
                       const VkBindImageMemoryInfo *pBindInfos)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      RVGPU_FROM_HANDLE(rvgpu_device_memory, mem, pBindInfos[i].memory);
      RVGPU_FROM_HANDLE(rvgpu_image, image, pBindInfos[i].image);

      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);

      if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
         struct rvgpu_image *swapchain_img =
            rvgpu_image_from_handle(wsi_common_get_image(
                                   swapchain_info->swapchain, swapchain_info->imageIndex));

         image->bindings[0].bo = swapchain_img->bindings[0].bo;
         image->bindings[0].offset = swapchain_img->bindings[0].offset;
         continue;
      }

      if (mem->alloc_size) {
         VkImageMemoryRequirementsInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = pBindInfos[i].image,
         };
         VkMemoryRequirements2 reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
         };

         rvgpu_GetImageMemoryRequirements2(_device, &info, &reqs);

         if (pBindInfos[i].memoryOffset + reqs.memoryRequirements.size > mem->alloc_size) {
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "Device memory object too small for the image.\n");
         }
      }

      if (image->disjoint) {
         const VkBindImagePlaneMemoryInfo *plane_info =
            vk_find_struct_const(pBindInfos[i].pNext, BIND_IMAGE_PLANE_MEMORY_INFO);

         switch (plane_info->planeAspect) {
            case VK_IMAGE_ASPECT_PLANE_0_BIT:
               image->bindings[0].bo = mem->bo;
               image->bindings[0].offset = pBindInfos[i].memoryOffset;
               break;
            case VK_IMAGE_ASPECT_PLANE_1_BIT:
               image->bindings[1].bo = mem->bo;
               image->bindings[1].offset = pBindInfos[i].memoryOffset;
               break;
            case VK_IMAGE_ASPECT_PLANE_2_BIT:
               image->bindings[2].bo = mem->bo;
               image->bindings[2].offset = pBindInfos[i].memoryOffset;
               break;
            default:
               break;
         }
      } else {
         image->bindings[0].bo = mem->bo;
         image->bindings[0].offset = pBindInfos[i].memoryOffset;
      }
   }
   return VK_SUCCESS;
}
