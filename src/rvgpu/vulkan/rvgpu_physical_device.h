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

#ifndef __RVGPU_PHYSICAL_DEVICE_H__
#define __RVGPU_PHYSICAL_DEVICE_H__

#include <xf86drm.h>
#include <vulkan/vulkan.h>

#include "nir/nir.h"
#include "wsi_common.h"
#include "vk_physical_device.h"

#include "rvgpu_winsys.h"
#include "rvgpu_instance.h"
#include "rvgpu_queue.h"

struct rvgpu_physical_device {
   struct vk_physical_device vk;

   struct rvgpu_instance *instance;

   struct rvgpu_winsys *ws;

   char marketing_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;

   VkPhysicalDeviceMemoryProperties memory_properties;

   /* Bitmask of memory types that use the 32-bit address space. */
   uint32_t memory_types_32bit;

   enum rvgpu_queue_family vk_queue_to_rvgpu[RVGPU_MAX_QUEUE_FAMILIES];
   uint32_t num_queues;

   /* DRM Information */
   int available_nodes;
   drmPciBusInfo bus_info;
   dev_t primary_devid;
   dev_t render_devid;
};

VkResult create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device, struct vk_physical_device **out);
void rvgpu_physical_device_destroy(struct vk_physical_device *vk_device);

#endif //__RVGPU_PHYSICAL_DEVICE_H__
