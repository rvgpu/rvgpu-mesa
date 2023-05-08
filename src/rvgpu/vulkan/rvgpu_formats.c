/*
 * Copyright © 2023 Sietium Semiconductor
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "vk_format.h"
#include "vk_util.h"
#include "vk_enum_defines.h"

#include "rvgpu_private.h"

static void
rvgpu_physical_device_get_format_properties(struct rvgpu_physical_device *physical_device,
                                            VkFormat format, VkFormatProperties3 *out_properties)
{
   VkFormatFeatureFlags2 linear = 0, tiled = 0, buffer = 0;
   const struct util_format_description *desc = vk_format_description(format);
   bool scaled = false;
   /* TODO: implement some software emulation of SUBSAMPLED formats. */
   if (desc->format == PIPE_FORMAT_NONE || desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      out_properties->linearTilingFeatures = linear;
      out_properties->optimalTilingFeatures = tiled;
      out_properties->bufferFeatures = buffer;
      return;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC) {
      out_properties->linearTilingFeatures = linear;
      out_properties->optimalTilingFeatures = tiled;
      out_properties->bufferFeatures = buffer;
      return;
   }

   if (vk_format_is_depth_or_stencil(format)) {
      tiled |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
      tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
      tiled |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
      tiled |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;

      if (vk_format_has_depth(format)) {
         tiled |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                     VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
      }

      /* Don't support blitting surfaces with depth/stencil. */
      if (vk_format_has_depth(format) && vk_format_has_stencil(format))
         tiled &= ~VK_FORMAT_FEATURE_2_BLIT_DST_BIT;

      /* Don't support linear depth surfaces */
      linear = 0;
   } else {
      if (tiled && !scaled) {
         tiled |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
      }

      /* Tiled formatting does not support NPOT pixel sizes */
      if (!util_is_power_of_two_or_zero(vk_format_get_blocksize(format)))
         tiled = 0;
   }

   if (linear && !scaled) {
      linear |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
   }

   switch (format) {
   case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
   case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
   case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
   case VK_FORMAT_A2R10G10B10_SINT_PACK32:
   case VK_FORMAT_A2B10G10R10_SINT_PACK32:
      buffer &=
         ~(VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT | VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT);
      linear = 0;
      tiled = 0;
      break;
   default:
      break;
   }

   switch (format) {
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32B32_SFLOAT:
   case VK_FORMAT_R32G32B32A32_SFLOAT:
   case VK_FORMAT_R16G16_SFLOAT:
   case VK_FORMAT_R16G16B16_SFLOAT:
   case VK_FORMAT_R16G16B16A16_SFLOAT:
   case VK_FORMAT_R16G16_SNORM:
   case VK_FORMAT_R16G16_UNORM:
   case VK_FORMAT_R16G16B16A16_SNORM:
   case VK_FORMAT_R16G16B16A16_UNORM:
   case VK_FORMAT_R8G8_SNORM:
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R8G8B8A8_SNORM:
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      buffer |= VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR;
      break;
   default:
      break;
   }
   /* addrlib does not support linear compressed textures. */
   if (vk_format_is_compressed(format))
      linear = 0;

   /* From the Vulkan spec 1.2.163:
    *
    * "VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT must be supported for the
    *  following formats if the attachmentFragmentShadingRate feature is supported:"
    *
    * - VK_FORMAT_R8_UINT
    */
   if (format == VK_FORMAT_R8_UINT) {
      tiled |= VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
   }

   /* It's invalid to expose buffer features with depth/stencil formats. */
   if (vk_format_is_depth_or_stencil(format)) {
      buffer = 0;
   }

   out_properties->linearTilingFeatures = linear;
   out_properties->optimalTilingFeatures = tiled;
   out_properties->bufferFeatures = buffer;
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                         VkFormatProperties2 *pFormatProperties)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, physical_device, physicalDevice);
   VkFormatProperties3 format_props;
   
   rvgpu_physical_device_get_format_properties(physical_device, format, &format_props);

   pFormatProperties->formatProperties.linearTilingFeatures =
      vk_format_features2_to_features(format_props.linearTilingFeatures);
   pFormatProperties->formatProperties.optimalTilingFeatures =
      vk_format_features2_to_features(format_props.optimalTilingFeatures);
   pFormatProperties->formatProperties.bufferFeatures =
      vk_format_features2_to_features(format_props.bufferFeatures);
}
