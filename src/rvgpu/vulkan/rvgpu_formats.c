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
#include "vk_log.h"
#include "vk_enum_defines.h"

#include "rvgpu_private.h"


static bool rvgpu_is_filter_minmax_format_supported(VkFormat format)
{
   /* From the Vulkan spec 1.1.71:
    *
    * "The following formats must support the
    *  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT feature with
    *  VK_IMAGE_TILING_OPTIMAL, if they support
    *  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT."
    */
   /* TODO: enable more formats. */
   switch (format) {
   case VK_FORMAT_R8_UNORM:
   case VK_FORMAT_R8_SNORM:
   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_R16_SNORM:
   case VK_FORMAT_R16_SFLOAT:
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D16_UNORM_S8_UINT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return true;
   default:
      return false;
   }
}


// rvgpu_is_format_supported is copied from llvmpipe_is_format_supported 
static bool
rvgpu_is_format_supported(enum pipe_format format,
                          enum pipe_texture_target target,
                          unsigned sample_count,
                          unsigned storage_sample_count,
                          unsigned bind) {
   const struct util_format_description *format_desc = util_format_description(format);

   assert(target == PIPE_BUFFER ||
            target == PIPE_TEXTURE_1D ||
            target == PIPE_TEXTURE_1D_ARRAY ||
            target == PIPE_TEXTURE_2D ||
            target == PIPE_TEXTURE_2D_ARRAY ||
            target == PIPE_TEXTURE_RECT ||
            target == PIPE_TEXTURE_3D ||
            target == PIPE_TEXTURE_CUBE ||
            target == PIPE_TEXTURE_CUBE_ARRAY);

    if ((sample_count != 0) && (sample_count != 1) && (sample_count != 4))
        return false;

    if (bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SHADER_IMAGE)) {
        if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
            /* this is a lie actually other formats COULD exist where we would fail */
            if (format_desc->nr_channels < 3)
                return false;
        } else if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_RGB) {
            return false;
        }

        if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN &&
            format != PIPE_FORMAT_R11G11B10_FLOAT)
            return false;

        assert(format_desc->block.width == 1);
        assert(format_desc->block.height == 1);

        if (format_desc->is_mixed)
            return false;

        if (!format_desc->is_array && !format_desc->is_bitmask && format != PIPE_FORMAT_R11G11B10_FLOAT)
            return false;
    }        

   if (bind & PIPE_BIND_SHADER_IMAGE) {
      switch (format) {
         case PIPE_FORMAT_R32G32B32A32_FLOAT:
         case PIPE_FORMAT_R16G16B16A16_FLOAT:
         case PIPE_FORMAT_R32G32_FLOAT:
         case PIPE_FORMAT_R16G16_FLOAT:
         case PIPE_FORMAT_R11G11B10_FLOAT:
         case PIPE_FORMAT_R32_FLOAT:
         case PIPE_FORMAT_R16_FLOAT:
         case PIPE_FORMAT_R32G32B32A32_UINT:
         case PIPE_FORMAT_R16G16B16A16_UINT:
         case PIPE_FORMAT_R10G10B10A2_UINT:
         case PIPE_FORMAT_R8G8B8A8_UINT:
         case PIPE_FORMAT_R32G32_UINT:
         case PIPE_FORMAT_R16G16_UINT:
         case PIPE_FORMAT_R8G8_UINT:
         case PIPE_FORMAT_R32_UINT:
         case PIPE_FORMAT_R16_UINT:
         case PIPE_FORMAT_R8_UINT:
         case PIPE_FORMAT_R32G32B32A32_SINT:
         case PIPE_FORMAT_R16G16B16A16_SINT:
         case PIPE_FORMAT_R8G8B8A8_SINT:
         case PIPE_FORMAT_R32G32_SINT:
         case PIPE_FORMAT_R16G16_SINT:
         case PIPE_FORMAT_R8G8_SINT:
         case PIPE_FORMAT_R32_SINT:
         case PIPE_FORMAT_R16_SINT:
         case PIPE_FORMAT_R8_SINT:
         case PIPE_FORMAT_R16G16B16A16_UNORM:
         case PIPE_FORMAT_R10G10B10A2_UNORM:
         case PIPE_FORMAT_R8G8B8A8_UNORM:
         case PIPE_FORMAT_R16G16_UNORM:
         case PIPE_FORMAT_R8G8_UNORM:
         case PIPE_FORMAT_R16_UNORM:
         case PIPE_FORMAT_R8_UNORM:
         case PIPE_FORMAT_R16G16B16A16_SNORM:
         case PIPE_FORMAT_R8G8B8A8_SNORM:
         case PIPE_FORMAT_R16G16_SNORM:
         case PIPE_FORMAT_R8G8_SNORM:
         case PIPE_FORMAT_R16_SNORM:
         case PIPE_FORMAT_R8_SNORM:
         case PIPE_FORMAT_B8G8R8A8_UNORM:
            break;

         default:
            return false;
      }
   }

   if ((bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
       ((bind & PIPE_BIND_DISPLAY_TARGET) == 0)) {
      /* Disable all 3-channel formats, where channel size != 32 bits.
       * In some cases we run into crashes (in generate_unswizzled_blend()),
       * for 3-channel RGB16 variants, there was an apparent LLVM bug.
       * In any case, disabling the shallower 3-channel formats avoids a
       * number of issues with GL_ARB_copy_image support.
       */
      if (format_desc->is_array &&
          format_desc->nr_channels == 3 &&
          format_desc->block.bits != 96) {
         return false;
      }

      /* Disable 64-bit integer formats for RT/samplers.
       * VK CTS crashes with these and they don't make much sense.
       */
      int c = util_format_get_first_non_void_channel(format_desc->format);
      if (c >= 0) {
         if (format_desc->channel[c].pure_integer &&
             format_desc->channel[c].size == 64)
            return false;
      }

   }

   if (!(bind & PIPE_BIND_VERTEX_BUFFER) &&
       util_format_is_scaled(format))
      return false;
   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
         return false;

      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
       format_desc->layout == UTIL_FORMAT_LAYOUT_ATC) {
      /* Software decoding is not hooked up. */
      return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       format != PIPE_FORMAT_ETC1_RGB8)
      return false;

   /*
    * Everything can be supported by u_format
    * (those without fetch_rgba_float might be not but shouldn't hit that)
    */

   return true;

}



static void
rvgpu_physical_device_get_format_properties(struct rvgpu_physical_device *physical_device,
                                            VkFormat format, VkFormatProperties3 *out_properties)
{
    enum pipe_format pformat = rvgpu_vk_format_to_pipe_format(format);
    VkFormatFeatureFlags2 features = 0, buffer_features = 0;
    if (pformat == PIPE_FORMAT_NONE) {
      out_properties->linearTilingFeatures = 0;
      out_properties->optimalTilingFeatures = 0;
      out_properties->bufferFeatures = 0;
      return;
    }

    if (rvgpu_is_format_supported(pformat, PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_DEPTH_STENCIL)) {
        out_properties->linearTilingFeatures = 0;
        out_properties->optimalTilingFeatures = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT | 
                                                VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
                                                VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | 
                                                VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
                                                VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | 
                                                VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
                                                VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT |  
                                                VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

        if (rvgpu_is_filter_minmax_format_supported(format)) {
            out_properties->optimalTilingFeatures |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT;
        }
        out_properties->bufferFeatures = 0;
        return;
    }

    if (util_format_is_compressed(pformat)) {
        if (rvgpu_is_format_supported(pformat, PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
            features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
            features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;
            features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
        }
        out_properties->linearTilingFeatures = features;
        out_properties->optimalTilingFeatures = features;
        out_properties->bufferFeatures = buffer_features;
        return;
    }

   if (!util_format_is_srgb(pformat) && rvgpu_is_format_supported(pformat,PIPE_BUFFER,0,0,PIPE_BIND_VERTEX_BUFFER)) {
       buffer_features |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;
   }

   if (rvgpu_is_format_supported(pformat,PIPE_BUFFER,0,0,PIPE_BIND_CONSTANT_BUFFER)) {
       buffer_features |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
   }

   if (rvgpu_is_format_supported(pformat, PIPE_BUFFER, 0, 0, PIPE_BIND_SHADER_IMAGE)) {
       buffer_features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT;
       //TODO: VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT, VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT
   } 

   if (rvgpu_is_format_supported(pformat,PIPE_TEXTURE_2D,0,0,PIPE_BIND_SAMPLER_VIEW)) {
       features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
       if (util_format_has_depth(util_format_description(pformat)))
           features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
       if (!util_format_is_pure_integer(pformat))
           features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
       if (rvgpu_is_filter_minmax_format_supported(format))   
           features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT;
   }

   if (rvgpu_is_format_supported(pformat,PIPE_TEXTURE_2D,0,0,PIPE_BIND_RENDER_TARGET)) {
       features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
       /* SNORM blending on llvmpipe fails CTS - disable for now */
       if (!util_format_is_snorm(pformat) && !util_format_is_pure_integer(pformat))
           features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
   }

   if (rvgpu_is_format_supported(pformat,PIPE_TEXTURE_2D,0,0,PIPE_BIND_SHADER_IMAGE)) {
       features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
       // TODO: VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT, VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT 
   }

   if (pformat == PIPE_FORMAT_R32_UINT || pformat == PIPE_FORMAT_R32_SINT || pformat == PIPE_FORMAT_R32_FLOAT) { 
       features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;
       buffer_features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   if (pformat == PIPE_FORMAT_R11G11B10_FLOAT || pformat == PIPE_FORMAT_R9G9B9E5_FLOAT)
       features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;

   if (features && buffer_features != VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT) 
       features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
   if (pformat == PIPE_FORMAT_B5G6R5_UNORM)
       features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
   if ((pformat != PIPE_FORMAT_R9G9B9E5_FLOAT) && util_format_get_nr_components(pformat) != 3 &&
       pformat != PIPE_FORMAT_R10G10B10A2_SNORM && pformat != PIPE_FORMAT_B10G10R10A2_SNORM &&
       pformat != PIPE_FORMAT_B10G10R10A2_UNORM) {
       features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT; 
   }

   out_properties->linearTilingFeatures = features;
   out_properties->optimalTilingFeatures = features;
   out_properties->bufferFeatures = buffer_features;
   return;
}

static void
get_external_image_format_properties(struct rvgpu_physical_device *physical_device,
                                     const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                     VkExternalMemoryHandleTypeFlagBits handleType,
                                     VkExternalMemoryProperties *external_properties,
                                     VkImageFormatProperties *format_properties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;
   // const struct util_format_description *desc = vk_format_description(pImageFormatInfo->format);

   if (pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT)
      return;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      if (pImageFormatInfo->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         break;

      switch (pImageFormatInfo->type) {
      case VK_IMAGE_TYPE_2D:
         flags =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;

         compat_flags = export_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         break;
      }
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      switch (pImageFormatInfo->type) {
      case VK_IMAGE_TYPE_2D:
         flags =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         if (pImageFormatInfo->tiling != VK_IMAGE_TILING_LINEAR)
            flags |= VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;

         compat_flags = export_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
         break;
      default:
         break;
      }
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      break;
   }

   *external_properties = (VkExternalMemoryProperties){
      .externalMemoryFeatures = flags,
      .exportFromImportedHandleTypes = export_flags,
      .compatibleHandleTypes = compat_flags,
   };
}

static VkResult
rvgpu_get_image_format_properties(struct rvgpu_physical_device *physical_device,
                                  const VkPhysicalDeviceImageFormatInfo2 *info, VkFormat format,
                                  VkImageFormatProperties *pImageFormatProperties)

{
   VkFormatProperties3 format_props;
   VkFormatFeatureFlags2 format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   const struct util_format_description *desc = vk_format_description(format);
   VkImageTiling tiling = info->tiling;
   VkResult result = VK_ERROR_FORMAT_NOT_SUPPORTED;

   rvgpu_physical_device_get_format_properties(physical_device, format, &format_props);
   if (tiling == VK_IMAGE_TILING_LINEAR) {
      format_feature_flags = format_props.linearTilingFeatures;
   } else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
      format_feature_flags = format_props.optimalTilingFeatures;
   } else if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      printf("TODO.zac \n");
   } else {
      unreachable("bad VkImageTiling");
   }

   if (format_feature_flags == 0)
      goto unsupported;

   if (info->type != VK_IMAGE_TYPE_2D && vk_format_is_depth_or_stencil(format))
      goto unsupported;

   switch (info->type) {
   default:
      unreachable("bad vkimage type\n");
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 16384;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 8192;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent.width = 16384;
      maxExtent.height = 16384;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 8192;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent.width = 8192;
      maxExtent.height = 8192;
      maxExtent.depth = 8192;
      maxMipLevels = util_logbase2(maxExtent.width) + 1;
      maxArraySize = 1;
      break;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      /* Might be able to support but the entire format support is
       * messy, so taking the lazy way out. */
      maxArraySize = 1;
   }

   if (tiling == VK_IMAGE_TILING_OPTIMAL && info->type == VK_IMAGE_TYPE_2D &&
       (format_feature_flags & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                                VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(info->usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)) {
      sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
   }

   if (tiling == VK_IMAGE_TILING_LINEAR &&
       (format == VK_FORMAT_R32G32B32_SFLOAT || format == VK_FORMAT_R32G32B32_SINT ||
        format == VK_FORMAT_R32G32B32_UINT)) {
      /* R32G32B32 is a weird format and the driver currently only
       * supports the barely minimum.
       * TODO: Implement more if we really need to.
       */
      if (info->type == VK_IMAGE_TYPE_3D)
         goto unsupported;
      maxArraySize = 1;
      maxMipLevels = 1;
   }

   /* From the Vulkan 1.3.206 spec:
    *
    * "VK_IMAGE_CREATE_EXTENDED_USAGE_BIT specifies that the image can be created with usage flags
    * that are not supported for the format the image is created with but are supported for at least
    * one format a VkImageView created from the image can have."
    */
   VkImageUsageFlags image_usage = info->usage;
   if (info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)
      image_usage = 0;

   if (image_usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (image_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (image_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (image_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (image_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT)) {
         goto unsupported;
      }
   }

   if (image_usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT)) {
         goto unsupported;
      }
   }

   if (image_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      if (!(format_feature_flags & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT))) {
         goto unsupported;
      }
   }

   /* Sparse resources with multi-planar formats are unsupported. */
   if (info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      if (vk_format_get_plane_count(format) > 1)
         goto unsupported;
   }

   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* FINISHME: Accurately calculate
       * VkImageFormatProperties::maxResourceSize.
       */
      .maxResourceSize = UINT32_MAX,
   };

   return VK_SUCCESS;
unsupported:
   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = {0, 0, 0},
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return result;
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

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                              const VkPhysicalDeviceImageFormatInfo2 *base_info,
                                              VkImageFormatProperties2 *base_props)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   // VkTextureLODGatherFormatPropertiesAMD *texture_lod_props = NULL;
   VkResult result;
   VkFormat format = base_info->format;

   result = rvgpu_get_image_format_properties(physical_device, base_info, format,
                                             &base_props->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)s;
         break;
      default:
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID:
         break;
      case VK_STRUCTURE_TYPE_TEXTURE_LOD_GATHER_FORMAT_PROPERTIES_AMD:
         // texture_lod_props = (void *)s;
         break;
      default:
         break;
      }
   }

   /* From the Vulkan 1.0.97 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      VkExternalImageFormatProperties fallback_external_props;

      if (!external_props) {
         memset(&fallback_external_props, 0, sizeof(fallback_external_props));
         external_props = &fallback_external_props;
      }

      get_external_image_format_properties(physical_device, base_info, external_info->handleType,
                                           &external_props->externalMemoryProperties,
                                           &base_props->imageFormatProperties);
      if (!external_props->externalMemoryProperties.externalMemoryFeatures) {
         /* From the Vulkan 1.0.97 spec:
          *
          *    If handleType is not compatible with the [parameters] specified
          *    in VkPhysicalDeviceImageFormatInfo2, then
          *    vkGetPhysicalDeviceImageFormatProperties2 returns
          *    VK_ERROR_FORMAT_NOT_SUPPORTED.
          */
         result = vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                            "unsupported VkExternalMemoryTypeFlagBitsKHR 0x%x",
                            external_info->handleType);
         goto fail;
      }
   }

   if (ycbcr_props) {
      ycbcr_props->combinedImageSamplerDescriptorCount = vk_format_get_plane_count(format);
   }

   return VK_SUCCESS;

fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.97 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties){0};
   }

   return result;
}
