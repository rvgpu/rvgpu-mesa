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

#include <vulkan/vulkan.h>

#include "vk_common_entrypoints.h"
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

   init_dispatch_tables(device, physical_device);

   device->vk.command_buffer_ops = &rvgpu_cmd_buffer_ops;
   device->vk.check_status = rvgpu_check_status;

   device->instance = physical_device->instance;
   device->physical_device = physical_device;

   device->ws = physical_device->ws;

   *pDevice = rvgpu_device_to_handle(device);
   return VK_SUCCESS;
}
