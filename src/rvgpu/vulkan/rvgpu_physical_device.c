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
#include "rvgpu_physical_device.h"

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

   return VK_SUCCESS;
#if 0
   device->vk.supported_sync_types = device->ws->get_sync_types(device->ws);

#ifndef _WIN32
   if (drm_device && instance->vk.enabled_extensions.KHR_display) {
      master_fd = open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         uint32_t accel_working = 0;
         struct drm_amdgpu_info request = {.return_pointer = (uintptr_t)&accel_working,
                                           .return_size = sizeof(accel_working),
                                           .query = AMDGPU_INFO_ACCEL_WORKING};

         if (drmCommandWrite(master_fd, DRM_AMDGPU_INFO, &request, sizeof(struct drm_amdgpu_info)) <
                0 ||
             !accel_working) {
            close(master_fd);
            master_fd = -1;
         }
      }
   }
#endif

   device->master_fd = master_fd;
   device->local_fd = fd;
   device->ws->query_info(device->ws, &device->rad_info);

   device->use_llvm = instance->debug_flags & RADV_DEBUG_LLVM;
#ifndef LLVM_AVAILABLE
   if (device->use_llvm) {
      fprintf(stderr, "ERROR: LLVM compiler backend selected for radv, but LLVM support was not "
                      "enabled at build time.\n");
      abort();
   }
#endif

#ifdef ANDROID
   device->emulate_etc2 = !radv_device_supports_etc(device);
#else
   device->emulate_etc2 = !radv_device_supports_etc(device) &&
                          driQueryOptionb(&device->instance->dri_options, "radv_require_etc2");
#endif

   snprintf(device->name, sizeof(device->name), "AMD RADV %s%s", device->rad_info.name,
            radv_get_compiler_string(device));

   const char *marketing_name = device->ws->get_chip_name(device->ws);
   snprintf(device->marketing_name, sizeof(device->name), "%s (RADV %s%s)",
            marketing_name ? marketing_name : "AMD Unknown", device->rad_info.name,
            radv_get_compiler_string(device));

#ifdef ENABLE_SHADER_CACHE
   if (radv_device_get_cache_uuid(device, device->cache_uuid)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "cannot generate UUID");
      goto fail_wsi;
   }

   /* The gpu id is already embedded in the uuid so we just pass "radv"
    * when creating the cache.
    */
   char buf[VK_UUID_SIZE * 2 + 1];
   disk_cache_format_hex_id(buf, device->cache_uuid, VK_UUID_SIZE * 2);
   device->vk.disk_cache = disk_cache_create(device->name, buf, 0);
#endif

   if (!radv_is_conformant(device))
      vk_warn_non_conformant_implementation("radv");

   radv_get_driver_uuid(&device->driver_uuid);
   radv_get_device_uuid(&device->rad_info, &device->device_uuid);

   device->dcc_msaa_allowed = (device->instance->perftest_flags & RADV_PERFTEST_DCC_MSAA);

   device->use_fmask =
      device->rad_info.gfx_level < GFX11 && !(device->instance->debug_flags & RADV_DEBUG_NO_FMASK);

   device->use_ngg =
      (device->rad_info.gfx_level >= GFX10 && device->rad_info.family != CHIP_NAVI14 &&
       !(device->instance->debug_flags & RADV_DEBUG_NO_NGG)) ||
      device->rad_info.gfx_level >= GFX11;

   /* TODO: Investigate if NGG culling helps on GFX11. */
   device->use_ngg_culling = device->use_ngg && device->rad_info.max_render_backends > 1 &&
                             (device->rad_info.gfx_level == GFX10_3 ||
                              (device->instance->perftest_flags & RADV_PERFTEST_NGGC)) &&
                             !(device->instance->debug_flags & RADV_DEBUG_NO_NGGC);

   device->use_ngg_streamout =
      device->use_ngg && (device->rad_info.gfx_level >= GFX11 ||
                          (device->instance->perftest_flags & RADV_PERFTEST_NGG_STREAMOUT));

   device->emulate_ngg_gs_query_pipeline_stat =
      device->use_ngg && device->rad_info.gfx_level < GFX11;

   /* Determine the number of threads per wave for all stages. */
   device->cs_wave_size = 64;
   device->ps_wave_size = 64;
   device->ge_wave_size = 64;
   device->rt_wave_size = 64;

   if (device->rad_info.gfx_level >= GFX10) {
      if (device->instance->perftest_flags & RADV_PERFTEST_CS_WAVE_32)
         device->cs_wave_size = 32;

      /* For pixel shaders, wave64 is recommanded. */
      if (device->instance->perftest_flags & RADV_PERFTEST_PS_WAVE_32)
         device->ps_wave_size = 32;

      if (device->instance->perftest_flags & RADV_PERFTEST_GE_WAVE_32)
         device->ge_wave_size = 32;

      /* Default to 32 on RDNA1-2 as that gives better perf due to less issues with divergence.
       * However, on GFX11 default to wave64 as ACO does not support VOPD yet, and with the VALU
       * dependence wave32 would likely be a net-loss (as well as the SALU count becoming more
       * problematic)
       */
      if (!(device->instance->perftest_flags & RADV_PERFTEST_RT_WAVE_64) &&
          device->rad_info.gfx_level < GFX11)
         device->rt_wave_size = 32;
   }

   device->max_shared_size = device->rad_info.gfx_level >= GFX7 ? 65536 : 32768;

   radv_physical_device_init_mem_types(device);

   radv_physical_device_get_supported_extensions(device, &device->vk.supported_extensions);

   radv_get_nir_options(device);

#ifndef _WIN32
   if (drm_device) {
      struct stat primary_stat = {0}, render_stat = {0};

      device->available_nodes = drm_device->available_nodes;
      device->bus_info = *drm_device->businfo.pci;

      if ((drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) &&
          stat(drm_device->nodes[DRM_NODE_PRIMARY], &primary_stat) != 0) {
         result =
            vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                      "failed to stat DRM primary node %s", drm_device->nodes[DRM_NODE_PRIMARY]);
         goto fail_perfcounters;
      }
      device->primary_devid = primary_stat.st_rdev;

      if ((drm_device->available_nodes & (1 << DRM_NODE_RENDER)) &&
          stat(drm_device->nodes[DRM_NODE_RENDER], &render_stat) != 0) {
         result =
            vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to stat DRM render node %s",
                      drm_device->nodes[DRM_NODE_RENDER]);
         goto fail_perfcounters;
      }
      device->render_devid = render_stat.st_rdev;
   }
#endif

   if ((device->instance->debug_flags & RADV_DEBUG_INFO))
      ac_print_gpu_info(&device->rad_info, stdout);

   radv_physical_device_init_queue_table(device);

   /* We don't check the error code, but later check if it is initialized. */
   ac_init_perfcounters(&device->rad_info, false, false, &device->ac_perfcounters);

   radv_init_physical_device_decoder(device);

   /* The WSI is structured as a layer on top of the driver, so this has
    * to be the last part of initialization (at least until we get other
    * semi-layers).
    */
   result = radv_init_wsi(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_perfcounters;
   }

   device->gs_table_depth =
      ac_get_gs_table_depth(device->rad_info.gfx_level, device->rad_info.family);

   ac_get_hs_info(&device->rad_info, &device->hs);
   ac_get_task_info(&device->rad_info, &device->task_info);
   radv_get_binning_settings(device, &device->binning_settings);

   *device_out = device;

   return VK_SUCCESS;

fail_perfcounters:
   ac_destroy_perfcounters(&device->ac_perfcounters);
   disk_cache_destroy(device->vk.disk_cache);
#ifdef ENABLE_SHADER_CACHE
fail_wsi:
#endif
   device->ws->destroy(device->ws);
#endif
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
