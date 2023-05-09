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

#include "vk_format.h"
#include "vk_util.h"
#include "vk_log.h"

#include "rvgpu_private.h"

static void
rvgpu_destroy_image(struct rvgpu_device *device, const VkAllocationCallbacks *pAllocator,
                    struct rvgpu_image *image)
{
   if ((image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) && image->bindings[0].bo) {
      device->ws->ops.buffer_destroy(device->ws, image->bindings[0].bo);
   }

   if (image->owned_memory != VK_NULL_HANDLE) {
      RVGPU_FROM_HANDLE(rvgpu_device_memory, mem, image->owned_memory);
      rvgpu_free_memory(device, pAllocator, mem);
   }

   vk_image_finish(&image->vk);
   vk_free2(&device->vk.alloc, pAllocator, image);
}

static void
rvgpu_image_reset_layout(const struct rvgpu_physical_device *pdev, struct rvgpu_image *image)
{
   image->size = 0;
   image->alignment = 1;

   image->tc_compatible_cmask = 0;
   image->fce_pred_offset = image->dcc_pred_offset = 0;
   image->clear_value_offset = image->tc_compat_zrange_offset = 0;

   unsigned plane_count = vk_format_get_plane_count(image->vk.format);
   for (unsigned i = 0; i < plane_count; ++i) {
      VkFormat format = vk_format_get_plane_format(image->vk.format, i);
      if (vk_format_has_depth(format))
         format = vk_format_depth_only(format);

      uint64_t flags = image->planes[i].surface.flags;
      uint64_t modifier = image->planes[i].surface.modifier;
      memset(image->planes + i, 0, sizeof(image->planes[i]));

      image->planes[i].surface.flags = flags;
      image->planes[i].surface.modifier = modifier;
      image->planes[i].surface.blk_w = vk_format_get_blockwidth(format);
      image->planes[i].surface.blk_h = vk_format_get_blockheight(format);
      image->planes[i].surface.bpe = vk_format_get_blocksize(format);

      /* align byte per element on dword */
      if (image->planes[i].surface.bpe == 3) {
         image->planes[i].surface.bpe = 4;
      }
   }
}

VkResult
rvgpu_image_create_layout(struct rvgpu_device *device, struct rvgpu_image_create_info create_info,
                          struct rvgpu_image *image)
{
   /* Clear the pCreateInfo pointer so we catch issues in the delayed case when we test in the
    * common internal case. */
   create_info.vk_info = NULL;

   struct rvgpu_surf_info image_info = image->info;
   rvgpu_image_reset_layout(device->physical_device, image);

   unsigned plane_count = vk_format_get_plane_count(image->vk.format);
   for (unsigned plane = 0; plane < plane_count; ++plane) {
      struct rvgpu_surf_info info = image_info;
      uint64_t offset;
      unsigned stride;

      info.width = util_format_get_plane_width(vk_format_to_pipe_format(image->vk.format), plane, info.width);
      info.height = util_format_get_plane_height(vk_format_to_pipe_format(image->vk.format), plane, info.height);

      device->ws->ops.surface_init(device->ws, &info, &image->planes[plane].surface);

      offset = image->disjoint ? 0 : align64(image->size, 1ull << image->planes[plane].surface.alignment_log2);
      stride = 0; /* 0 means no override */

      image->size = MAX2(image->size, offset + image->planes[plane].surface.total_size);
      image->alignment = MAX2(image->alignment, 1 << image->planes[plane].surface.alignment_log2);

      image->planes[plane].format = vk_format_get_plane_format(image->vk.format, plane);
   }

   assert(image->planes[0].surface.surf_size);
   return VK_SUCCESS;
}

VkResult
rvgpu_image_create(VkDevice _device, const struct rvgpu_image_create_info *create_info,
                   const VkAllocationCallbacks *alloc, VkImage *pImage, bool is_internal)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
   struct rvgpu_image *image = NULL;
   VkFormat format = pCreateInfo->format;
   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

   unsigned plane_count = vk_format_get_plane_count(format);

   const size_t image_struct_size = sizeof(*image) + sizeof(struct rvgpu_image_plane) * plane_count;

   assert(pCreateInfo->mipLevels > 0);
   assert(pCreateInfo->arrayLayers > 0);
   assert(pCreateInfo->samples > 0);
   assert(pCreateInfo->extent.width > 0);
   assert(pCreateInfo->extent.height > 0);
   assert(pCreateInfo->extent.depth > 0);

   image =
      vk_zalloc2(&device->vk.alloc, alloc, image_struct_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_image_init(&device->vk, &image->vk, pCreateInfo);

   image->info.width = pCreateInfo->extent.width;
   image->info.height = pCreateInfo->extent.height;
   image->info.depth = pCreateInfo->extent.depth;
   image->info.samples = pCreateInfo->samples;
   image->info.storage_samples = pCreateInfo->samples;
   image->info.array_size = pCreateInfo->arrayLayers;
   image->info.levels = pCreateInfo->mipLevels;
   image->info.num_channels = vk_format_get_nr_components(format);

   image->plane_count = vk_format_get_plane_count(format);
   image->disjoint = image->plane_count > 1 && pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT;

   image->exclusive = pCreateInfo->sharingMode == VK_SHARING_MODE_EXCLUSIVE;
   if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT) {
      for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; ++i)
         if (pCreateInfo->pQueueFamilyIndices[i] == VK_QUEUE_FAMILY_EXTERNAL ||
             pCreateInfo->pQueueFamilyIndices[i] == VK_QUEUE_FAMILY_FOREIGN_EXT)
            image->queue_family_mask |= (1u << RVGPU_MAX_QUEUE_FAMILIES) - 1u;
         else
            image->queue_family_mask |= 1u << vk_queue_to_rvgpu(pCreateInfo->pQueueFamilyIndices[i]);
   }

   const VkExternalMemoryImageCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_IMAGE_CREATE_INFO);

   image->shareable = external_info;

   VkResult result = rvgpu_image_create_layout(device, *create_info, image);
   if (result != VK_SUCCESS) {
      rvgpu_destroy_image(device, alloc, image);
      return result;
   }

   *pImage = rvgpu_image_to_handle(image);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateImage(VkDevice _device, const VkImageCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
   /* Ignore swapchain creation info on Android. Since we don't have an implementation in Mesa,
    * we're guaranteed to access an Android object incorrectly.
    */
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(device->physical_device->vk.wsi_device,
                                               pCreateInfo,
                                               swapchain_info->swapchain,
                                               pImage);
   } 

   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   bool scanout = wsi_info && wsi_info->scanout;
   bool prime_blit_src = wsi_info && wsi_info->blit_src;
   
   return rvgpu_image_create(_device,
                            &(struct rvgpu_image_create_info){
                               .vk_info = pCreateInfo,
                               .scanout = scanout,
                               .prime_blit_src = prime_blit_src,
                            },
                            pAllocator, pImage, false);
}
