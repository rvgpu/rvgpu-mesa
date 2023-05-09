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

#ifndef RVGPU_IMAGE_H__
#define RVGPU_IMAGE_H__

#include "vk_image.h"

#include "rvgpu_winsys.h"
#include "rvgpu_surface.h"

struct rvgpu_image_binding {
   /* Set when bound */
   struct rvgpu_winsys_bo *bo;
   VkDeviceSize offset;
};

struct rvgpu_image_plane {
   VkFormat format;
   struct rvgpu_surf surface;
}; 

struct rvgpu_image {
   struct vk_image vk;

   struct rvgpu_surf_info info;
   VkDeviceSize size;
   uint32_t alignment;

   unsigned queue_family_mask;
   bool exclusive;
   bool shareable;
   bool l2_coherent;
   bool dcc_sign_reinterpret;
   bool support_comp_to_single;

   struct rvgpu_image_binding bindings[3];
   bool tc_compatible_cmask;

   uint64_t clear_value_offset;
   uint64_t fce_pred_offset;
   uint64_t dcc_pred_offset;

   /*
    * Metadata for the TC-compat zrange workaround. If the 32-bit value
    * stored at this offset is UINT_MAX, the driver will emit
    * DB_Z_INFO.ZRANGE_PRECISION=0, otherwise it will skip the
    * SET_CONTEXT_REG packet.
    */
   uint64_t tc_compat_zrange_offset;

   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory owned_memory;

   unsigned plane_count;
   bool disjoint;
   struct rvgpu_image_plane planes[0];
};

struct rvgpu_image_create_info {
   const VkImageCreateInfo *vk_info;
   bool scanout;
   bool no_metadata_planes;
   bool prime_blit_src;
}; 
   
VkResult rvgpu_image_create(VkDevice _device, 
                            const struct rvgpu_image_create_info *info,
                            const VkAllocationCallbacks *alloc, 
                            VkImage *pImage, 
                            bool is_internal);

VkResult rvgpu_image_create_layout(struct rvgpu_device *device, 
                                   struct rvgpu_image_create_info create_info, 
                                   struct rvgpu_image *image);

#endif // RVGPU_IMAGE_H__
