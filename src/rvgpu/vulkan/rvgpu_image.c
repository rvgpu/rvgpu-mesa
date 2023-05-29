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

#include "drm-uapi/drm_fourcc.h"

#include "vk_format.h"
#include "vk_util.h"
#include "vk_log.h"

#include "rvgpu_private.h"

static bool
rvgpu_image_layout_init(struct rvgpu_image_layout *layout)
{
   unsigned fmt_blocksize = util_format_get_blocksize(layout->format);
   /* MSAA is implemented as a 3D texture with z corresponding to the
    * sample #, horrifyingly enough */
   assert(layout->depth == 1 || layout->nr_samples == 1);
   bool linear = layout->modifier == DRM_FORMAT_MOD_LINEAR;

   unsigned offset = 0;

   unsigned width = layout->width;
   unsigned height = layout->height;
   unsigned depth = layout->depth;

   unsigned align_w = 1;
   unsigned align_h = 1;

   for (unsigned l = 0; l < layout->nr_slices; ++l) {
      struct rvgpu_image_slice_layout *slice = &layout->slices[l];

      unsigned effective_width =
         ALIGN_POT(util_format_get_nblocksx(layout->format, width), align_w);
      unsigned effective_height =
         ALIGN_POT(util_format_get_nblocksy(layout->format, height), align_h);

      /* Align levels to cache-line as a performance improvement for
       * linear/tiled and as a requirement for AFBC */

      offset = ALIGN_POT(offset, 64);

      slice->offset = offset;

      unsigned row_stride = fmt_blocksize * effective_width;

      if (linear) {
         /* Keep lines alignment on 64 byte for performance */
         row_stride = ALIGN_POT(row_stride, 64);
      }

      unsigned slice_one_size = row_stride * effective_height;

      slice->row_stride = row_stride;

      unsigned slice_full_size = slice_one_size * depth * layout->nr_samples;

      slice->surface_stride = slice_one_size;

      offset += slice_full_size;
      slice->size = slice_full_size;

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   /* Arrays and cubemaps have the entire miptree duplicated */
   layout->array_stride = ALIGN_POT(offset, 64);
   layout->data_size = ALIGN_POT(layout->array_stride * layout->array_size, 4096);

   return true;
}

static VkResult
rvgpu_image_create(VkDevice _device, const VkImageCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *alloc, VkImage *pImage,
                   uint64_t modifier)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   struct rvgpu_image *image = NULL;

   image = vk_image_create(&device->vk, pCreateInfo, alloc, sizeof(*image));
   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   image->layout = (struct rvgpu_image_layout){
      .modifier = modifier,
      .format = vk_format_to_pipe_format(image->vk.format),
      .dim = image->vk.image_type,
      .width = image->vk.extent.width,
      .height = image->vk.extent.height,
      .depth = image->vk.extent.depth,
      .array_size = image->vk.array_layers,
      .nr_samples = image->vk.samples,
      .nr_slices = image->vk.mip_levels,
   };

   rvgpu_image_layout_init(&image->layout);

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

   return rvgpu_image_create(_device, pCreateInfo, pAllocator, pImage, DRM_FORMAT_MOD_LINEAR);
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateImageView(VkDevice _device, const VkImageViewCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
   RVGPU_FROM_HANDLE(rvgpu_image, image, pCreateInfo->image);
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   struct rvgpu_image_view *view;

   view = vk_image_view_create(&device->vk, false, pCreateInfo,pAllocator, sizeof(*view));

   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
  
   view->format = rvgpu_vk_format_to_pipe_format(view->vk.format);
   view->image = image;

   //TODO: set the view->iv and view->sv, 参考lvp_CreateImageView

   *pView = rvgpu_image_view_to_handle(view);

   return VK_SUCCESS;
}
