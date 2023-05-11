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

/* hardware can texture up to 65536 x 65536 x 65536 and render up to 16384
 * x 16384, but 8192 x 8192 should be enough for anyone.  The OpenGL game
 * "Cathedral" requires a texture of width 8192 to start.
 */
#define MAX_MIP_LEVELS (14)

struct rvgpu_image_slice_layout {
   unsigned offset;

   /* 
    * For an images, the number of bytes between two rows of texels.
    * For linear images, this will equal the logical stride. For
    * images that are compressed or interleaved, this will be greater than
    * the logical stride.
    */
   unsigned row_stride;

   unsigned surface_stride;

   unsigned size;
};

enum rvgpu_texture_dimension
{
   RVGPU_TEXTURE_DIMENSION_1D = 0,
   RVGPU_TEXTURE_DIMENSION_2D = 1,
   RVGPU_TEXTURE_DIMENSION_3D = 2,
   RVGPU_TEXTURE_DIMENSION_MAX_ENUM = 0x7FFFFFFF
};

struct rvgpu_image_layout {
   uint64_t modifier;
   enum pipe_format format;
   unsigned width, height, depth;
   unsigned nr_samples;
   enum rvgpu_texture_dimension dim;
   unsigned nr_slices;
   unsigned array_size;

   /* The remaining fields may be derived from the above by calling
    * pan_image_layout_init
    */ 
   struct rvgpu_image_slice_layout slices[MAX_MIP_LEVELS];

   unsigned data_size;
   unsigned array_stride;

};

struct rvgpu_image {
   struct vk_image vk;

   /* Image Memory */
   struct rvgpu_winsys_bo *bo;
   unsigned offset;

   /* Image Layout */
   struct rvgpu_image_layout layout;

   VkDeviceSize size;
   uint32_t alignment;
};

static VkResult rvgpu_image_create(VkDevice _device, 
                                   const VkImageCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *alloc, 
                                   VkImage *pImage,
                                   uint64_t modifier);

#endif // RVGPU_IMAGE_H__
