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

#include "util/u_debug.h"

#include "rvgpu_instance.h"

#include "vk_log.h"
#include "vk_alloc.h"
#include "vk_instance.h"

static const struct debug_control rvgpu_debug_options[] = {
   {NULL, 0}
};

const char *
rvgpu_get_debug_option_name(int id)
{  
   assert(id < ARRAY_SIZE(rvgpu_debug_options) - 1);
   return rvgpu_debug_options[id].string;
}

static const struct debug_control rvgpu_perftest_options[] = {
   {NULL, 0}};

const char *
rvgpu_get_perftest_option_name(int id)
{  
   assert(id < ARRAY_SIZE(rvgpu_perftest_options) - 1);
   return rvgpu_perftest_options[id].string;
}

static const struct vk_instance_extension_table rvgpu_instance_extensions_supported = {
   .KHR_device_group_creation = true,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,

   .KHR_get_surface_capabilities2 = true,
   .KHR_surface = true,
   .KHR_surface_protected_capabilities = true,
   .EXT_surface_maintenance1 = true,
   .EXT_swapchain_colorspace = true,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .KHR_get_display_properties2 = true,
   .EXT_direct_mode_display = true,
   .EXT_display_surface_counter = true,
   .EXT_acquire_drm_display = true,
#endif
};

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
   struct rvgpu_instance *instance;
   VkResult result;

   if (!pAllocator)
      pAllocator = vk_default_allocator();

   instance = vk_zalloc(pAllocator, sizeof(*instance), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &rvgpu_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk, &rvgpu_instance_extensions_supported, &dispatch_table,
                             pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(instance, result);
   }

   instance->debug_flags = parse_debug_string(getenv("RVGPU_DEBUG"), rvgpu_debug_options);
   instance->perftest_flags = parse_debug_string(getenv("RVGPU_PERFTEST"), rvgpu_perftest_options);

   /* When RVGPU_FORCE_FAMILY is set, the driver creates a null
    * device that allows to test the compiler without having an
    * AMDGPU instance.
    */
   if (getenv("RVGPU_FORCE_FAMILY"))
      instance->vk.physical_devices.enumerate = create_null_physical_device;
   else
      instance->vk.physical_devices.try_create_for_drm = create_drm_physical_device;

   instance->vk.physical_devices.destroy = rvgpu_physical_device_destroy;

   // if (instance->debug_flags & RVGPU_DEBUG_STARTUP)
   //   fprintf(stderr, "rvgpu: info: Created an instance.\n");
  

   // rvgpu_init_dri_options(instance);

   *pInstance = rvgpu_instance_to_handle(instance);

   return VK_SUCCESS;

}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(&rvgpu_instance_extensions_supported,
                                                     pPropertyCount, pProperties);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
rvgpu_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   RVGPU_FROM_HANDLE(vk_instance, instance, _instance);
   return vk_instance_get_proc_addr(instance, &rvgpu_instance_entrypoints, pName);
}


PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char *pName);


PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char *pName)
{
   return rvgpu_GetInstanceProcAddr(instance, pName);
}
