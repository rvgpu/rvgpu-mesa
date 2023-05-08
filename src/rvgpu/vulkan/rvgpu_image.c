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

#include "rvgpu_private.h"

VkResult
rvgpu_image_create(VkDevice _device, const struct rvgpu_image_create_info *create_info,
                   const VkAllocationCallbacks *alloc, VkImage *pImage, bool is_internal)
{
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
