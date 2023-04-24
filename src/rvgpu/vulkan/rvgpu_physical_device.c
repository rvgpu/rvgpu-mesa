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

#include "rvgpu_wsi.h"
#include "rvgpu_constants.h"
#include "rvgpu_instance.h"
#include "rvgpu_physical_device.h"

static VkResult
rvgpu_physical_device_try_create(struct rvgpu_instance *instance, drmDevicePtr drm_device,
                                struct rvgpu_physical_device **device_out)
{
   return VK_SUCCESS;

}

VkResult
create_null_physical_device(struct vk_instance *vk_instance)
{ 
   struct rvgpu_instance *instance = container_of(vk_instance, struct rvgpu_instance, vk);
   struct rvgpu_physical_device *pdevice;
  
   VkResult result = rvgpu_physical_device_try_create(instance, NULL, &pdevice);
   if (result != VK_SUCCESS)
      return result; 

   list_addtail(&pdevice->vk.link, &instance->vk.physical_devices.list);
   return VK_SUCCESS;
}

VkResult
create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device,
                           struct vk_physical_device **out)
{
   if (!(device->available_nodes & (1 << DRM_NODE_RENDER)) || device->bustype != DRM_BUS_PCI ||
       device->deviceinfo.pci->vendor_id != ATI_VENDOR_ID)
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
   device->ws->destroy(device->ws);
   // disk_cache_destroy(device->vk.disk_cache);
   if (device->local_fd != -1)
      close(device->local_fd);
   if (device->master_fd != -1)
      close(device->master_fd);
   vk_physical_device_finish(&device->vk);
   vk_free(&device->instance->vk.alloc, device);
}
