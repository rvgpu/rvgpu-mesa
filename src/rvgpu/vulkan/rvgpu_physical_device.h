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
#include "rvgpu_perfcounter.h"

/* queue types */
enum rvgpu_queue_family {
   RVGPU_QUEUE_GENERAL,
   RVGPU_QUEUE_COMPUTE,
   RVGPU_QUEUE_TRANSFER,
   RVGPU_QUEUE_VIDEO_DEC,
   RVGPU_QUEUE_VIDEO_ENC,
   RVGPU_MAX_QUEUE_FAMILIES,
   RVGPU_QUEUE_FOREIGN = RVGPU_MAX_QUEUE_FAMILIES,
   RVGPU_QUEUE_IGNORED,
};

struct rvgpu_binning_settings {
   unsigned context_states_per_bin;    /* allowed range: [1, 6] */
   unsigned persistent_states_per_bin; /* allowed range: [1, 32] */
   unsigned fpovs_per_batch;           /* allowed range: [0, 255], 0 = unlimited */
};

struct rvgpu_physical_device {
   struct vk_physical_device vk;

   struct rvgpu_instance *instance;

   struct rvgpu_winsys *ws;
   struct radeon_info rad_info;
   char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   char marketing_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   int local_fd;
   int master_fd;
   struct wsi_device wsi_device;

   /* Whether DCC should be enabled for MSAA textures. */
   bool dcc_msaa_allowed;

   /* Whether to enable FMASK compression for MSAA textures (GFX6-GFX10.3) */
   bool use_fmask;

   /* Whether to enable NGG. */
   bool use_ngg;

   /* Whether to enable NGG culling. */
   bool use_ngg_culling;

   /* Whether to enable NGG streamout. */
   bool use_ngg_streamout;

   /* Whether to emulate the number of primitives generated by GS. */
   bool emulate_ngg_gs_query_pipeline_stat;

   /* Number of threads per wave. */
   uint8_t ps_wave_size;
   uint8_t cs_wave_size;
   uint8_t ge_wave_size;
   uint8_t rt_wave_size;

   /* Maximum compute shared memory size. */
   uint32_t max_shared_size;
   /* Whether to use the LLVM compiler backend */
   bool use_llvm;

   /* Whether to emulate ETC2 image support on HW without support. */
   bool emulate_etc2;

   VkPhysicalDeviceMemoryProperties memory_properties;
   enum radeon_bo_domain memory_domains[VK_MAX_MEMORY_TYPES];
   enum radeon_bo_flag memory_flags[VK_MAX_MEMORY_TYPES];
   unsigned heaps;

   /* Bitmask of memory types that use the 32-bit address space. */
   uint32_t memory_types_32bit;

#ifndef _WIN32
   int available_nodes;
   drmPciBusInfo bus_info;

   dev_t primary_devid;
   dev_t render_devid;
#endif

   nir_shader_compiler_options nir_options[MESA_VULKAN_SHADER_STAGES];

   enum rvgpu_queue_family vk_queue_to_rvgpu[RVGPU_MAX_QUEUE_FAMILIES];
   uint32_t num_queues;

   uint32_t gs_table_depth;

   struct ac_hs_info hs;
   struct ac_task_info task_info;

   struct rvgpu_binning_settings binning_settings;

   /* Performance counters. */
   struct ac_perfcounters ac_perfcounters;

   uint32_t num_perfcounters;
   struct rvgpu_perfcounter_desc *perfcounters;

   struct {
      unsigned data0;
      unsigned data1;
      unsigned cmd;
      unsigned cntl;
   } vid_dec_reg;
};

VkResult create_null_physical_device(struct vk_instance *vk_instance);
VkResult create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device, struct vk_physical_device **out);
void rvgpu_physical_device_destroy(struct vk_physical_device *vk_device);

#endif //__RVGPU_PHYSICAL_DEVICE_H__