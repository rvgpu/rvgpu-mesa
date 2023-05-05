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

#include <fcntl.h>

#include "vk_log.h"

#include "rvgpu_wsi.h"
#include "rvgpu_constants.h"
#include "rvgpu_instance.h"
#include "rvgpu_winsys.h"
#include "rvgpu_private.h"

static void
rvgpu_physical_device_get_supported_extensions(const struct rvgpu_physical_device *device,
                                               struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table) {
      .KHR_swapchain = true,
   };
}

static VkResult
rvgpu_physical_device_try_create(struct rvgpu_instance *instance, drmDevicePtr drm_device,
                                struct rvgpu_physical_device **device_out)
{
   VkResult result;
   int fd = -1;
   int master_fd = -1;

   if (drm_device) {
      const char *path = drm_device->nodes[DRM_NODE_RENDER];
      drmVersionPtr version;

      fd = drmOpen(path, NULL);
      if (fd < 0) {
         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER, "Could not open device %s: %m",
                          path);
      }

      version = drmGetVersion(fd);
      if (!version) {
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Could not get the kernel driver version for device %s: %m", path);
      }

      if (strcmp(version->name, "rvgpu cmodel")) {
         drmFreeVersion(version);
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Device '%s' is not using the AMDGPU kernel driver: %m", path);
      }
      drmFreeVersion(version);
   }

   struct rvgpu_physical_device *device = vk_zalloc2(&instance->vk.alloc, NULL, sizeof(*device), 8,
                                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_fd;
   }

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &rvgpu_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk, NULL, &dispatch_table);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   device->instance = instance;

   if (drm_device) {
      device->ws = rvgpu_winsys_create(fd, instance->debug_flags, instance->perftest_flags);
   } else {
      printf("null drm device\n");
   }

   if (!device->ws) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to initialize winsys");
      goto fail_base;
   }

   device->vk.supported_sync_types = device->ws->ops.get_sync_types(device->ws);

   rvgpu_physical_device_get_supported_extensions(device, &device->vk.supported_extensions);

   result = rvgpu_wsi_init(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_base;
   }

   *device_out = device;
   return VK_SUCCESS;

fail_base:
   vk_physical_device_finish(&device->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, device);
fail_fd:
   if (fd != -1)
      close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

VkResult
create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device,
                           struct vk_physical_device **out)
{
   if (!(device->available_nodes & (1 << DRM_NODE_RENDER)) || device->bustype != DRM_BUS_PCI ||
       device->deviceinfo.pci->vendor_id != SIETIUM_VENDOR_ID)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   return rvgpu_physical_device_try_create((struct rvgpu_instance *)vk_instance, device,
                                          (struct rvgpu_physical_device **)out);
}

void
rvgpu_physical_device_destroy(struct vk_physical_device *vk_device)
{
   struct rvgpu_physical_device *device = container_of(vk_device, struct rvgpu_physical_device, vk);

   rvgpu_finish_wsi(device);
   ac_destroy_perfcounters(&device->ac_perfcounters);
   rvgpu_winsys_destroy(device->ws);
   // disk_cache_destroy(device->vk.disk_cache);
   if (device->local_fd != -1)
      close(device->local_fd);
   if (device->master_fd != -1)
      close(device->master_fd);
   vk_physical_device_finish(&device->vk);
   vk_free(&device->instance->vk.alloc, device);
}

static void
rvgpu_get_physical_device_queue_family_properties(struct rvgpu_physical_device *pdevice,
                                                  uint32_t *pCount,
                                                  VkQueueFamilyProperties **pQueueFamilyProperties)
{
   int num_queue_families = 1;
   int idx;

   if (pQueueFamilyProperties == NULL) {
      *pCount = num_queue_families;
      return;
   }

   if (!*pCount) {
      return;
   }

   idx = 0;
   if (*pCount >= 1) {
      *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
         .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT |
                       VK_QUEUE_SPARSE_BINDING_BIT,
         .queueCount = 1,
         .timestampValidBits = 64,
         .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
      };
      idx++;
   }

   *pCount = idx;
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                              VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);
   if (!pQueueFamilyProperties) {
      rvgpu_get_physical_device_queue_family_properties(pdevice, pCount, NULL);  
      return ;
   }

   VkQueueFamilyProperties *properties[] = {
      &pQueueFamilyProperties[0].queueFamilyProperties,
      &pQueueFamilyProperties[1].queueFamilyProperties,
      &pQueueFamilyProperties[2].queueFamilyProperties,
   };

   rvgpu_get_physical_device_queue_family_properties(pdevice, pCount, properties);
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                   VkPhysicalDeviceProperties2 *pProperties)
{
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceFeatures2 *pFeatures)
{
}

void
rvgpu_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, 
                                         VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
}
