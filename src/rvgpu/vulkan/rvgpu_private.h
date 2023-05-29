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

#ifndef __RVGPU_PRIVATE_H__
#define __RVGPU_PRIVATE_H__

#include "vk_object.h"
#include "vk_format.h"

#include "rvgpu_constants.h"
#include "rvgpu_instance.h"
#include "rvgpu_physical_device.h"
#include "rvgpu_queue.h"
#include "rvgpu_device.h"
#include "rvgpu_wsi.h"
#include "rvgpu_image.h"
#include "rvgpu_buffer.h"
#include "rvgpu_device_memory.h"
#include "rvgpu_cmd_buffer.h"
#include "rvgpu_descriptor_set.h"
#include "rvgpu_pipeline.h"
#include "rvgpu_util.h"

#define RVGPU_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)

#define RVGPU_UNUSED_VARIABLE(var)  ((void)(var))

extern const struct vk_command_buffer_ops rvgpu_cmd_buffer_ops;

#define RVGPU_FROM_HANDLE(__rvgpu_type, __name, __handle) VK_FROM_HANDLE(__rvgpu_type, __name, __handle)

VK_DEFINE_HANDLE_CASTS(rvgpu_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(rvgpu_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(rvgpu_physical_device, vk.base, VkPhysicalDevice, VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(rvgpu_cmd_buffer, vk.base, VkCommandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER)


VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_buffer, vk.base, VkBuffer, VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_device_memory, base, VkDeviceMemory, VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_image_view, vk.base, VkImageView, VK_OBJECT_TYPE_IMAGE_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_descriptor_set_layout, vk.base, VkDescriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_pipeline, base, VkPipeline, VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(rvgpu_pipeline_layout, vk.base, VkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT)

/**   
 * Warn on ignored extension structs.
 *    
 * The Vulkan spec requires us to ignore unsupported or unknown structs in
 * a pNext chain. In debug mode, emitting warnings for ignored structs may
 * help us discover structs that we should not have ignored.
 *
 * 
 * From the Vulkan 1.0.38 spec:
 *
 *    Any component of the implementation (the loader, any enabled layers,
 *    and drivers) must skip over, without processing (other than reading the
 *    sType and pNext members) any chained structures with sType values not
 *    defined by extensions supported by that component.
 */
#define rvgpu_debug_ignored_stype(sType) \
   mesa_logd("%s: ignored VkStructureType %u\n", __func__, (sType))


static inline enum pipe_format
rvgpu_vk_format_to_pipe_format(VkFormat format)
{
   /* Some formats cause problems with CTS right now.*/
   if (format == VK_FORMAT_R4G4B4A4_UNORM_PACK16 ||
       format == VK_FORMAT_R8_SRGB ||
       format == VK_FORMAT_R8G8_SRGB ||
       format == VK_FORMAT_R64G64B64A64_SFLOAT ||
       format == VK_FORMAT_R64_SFLOAT ||
       format == VK_FORMAT_R64G64_SFLOAT ||
       format == VK_FORMAT_R64G64B64_SFLOAT ||
       format == VK_FORMAT_A2R10G10B10_SINT_PACK32 ||
       format == VK_FORMAT_A2B10G10R10_SINT_PACK32 ||
       format == VK_FORMAT_G8B8G8R8_422_UNORM ||
       format == VK_FORMAT_B8G8R8G8_422_UNORM ||
       format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM ||
       format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM ||
       format == VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM ||
       format == VK_FORMAT_G8_B8R8_2PLANE_422_UNORM ||
       format == VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM ||
       format == VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM ||
       format == VK_FORMAT_G16_B16R16_2PLANE_420_UNORM ||
       format == VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM ||
       format == VK_FORMAT_G16_B16R16_2PLANE_422_UNORM ||
       format == VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM ||
       format == VK_FORMAT_D16_UNORM_S8_UINT)
      return PIPE_FORMAT_NONE;

   return vk_format_to_pipe_format(format);
}

bool rvgpu_is_buffer_format_supported(VkFormat format, bool *scaled);
uint32_t rvgpu_translate_buffer_dataformat(const struct util_format_description *desc, int first_non_void);
uint32_t rvgpu_translate_buffer_numformat(const struct util_format_description *desc, int first_non_void);
#endif // __RVGPU_PRIVATE_H__
