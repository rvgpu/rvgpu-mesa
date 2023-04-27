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

#ifndef __RVGPU_INSTANCE_H__
#define __RVGPU_INSTANCE_H__

#include <vulkan/vulkan.h>

#include "rvgpu_entrypoints.h"
#include "rvgpu_physical_device.h"

#include "wsi_common.h"
#include "vk_instance.h"


struct rvgpu_instance {
   struct vk_instance vk;

   VkAllocationCallbacks alloc;

   uint64_t debug_flags;
   uint64_t perftest_flags; 

   /**
    * Workarounds for game bugs. 
    */
   bool enable_mrt_output_nan_fixup;
   bool disable_tc_compat_htile_in_general;
   bool disable_shrink_image_store;
   bool absolute_depth_bias;
   bool disable_aniso_single_level;
   bool zero_vram;
   bool disable_sinking_load_input_fs;
   bool flush_before_query_copy;
   bool enable_unified_heap_on_apu;
   bool tex_non_uniform;
   char *app_layer;
};

const char *rvgpu_get_debug_option_name(int id);

const char *rvgpu_get_perftest_option_name(int id);

#endif // __RVGPU_INSTANCE_H__
