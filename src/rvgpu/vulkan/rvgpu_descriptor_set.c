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

#include "rvgpu_private.h"

struct rvgpu_pipeline_layout *
rvgpu_pipeline_layout_create(struct rvgpu_device *device,
                             const VkPipelineLayoutCreateInfo* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator)
{
   struct rvgpu_pipeline_layout *layout = vk_pipeline_layout_zalloc(&device->vk, sizeof(*layout), pCreateInfo);

   for (uint32_t set = 0; set < layout->vk.set_count; set++) {
      if (layout->vk.set_layouts[set] == NULL)
         continue;

      const struct rvgpu_descriptor_set_layout *set_layout =
         vk_to_rvgpu_descriptor_set_layout(layout->vk.set_layouts[set]);

      for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
         layout->stage[i].uniform_block_size += set_layout->stage[i].uniform_block_size;
         for (unsigned j = 0; j < set_layout->stage[i].uniform_block_count; j++) {
            assert(layout->stage[i].uniform_block_count + j < MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS);
            layout->stage[i].uniform_block_sizes[layout->stage[i].uniform_block_count + j] = set_layout->stage[i].uniform_block_sizes[j];
         }
         layout->stage[i].uniform_block_count += set_layout->stage[i].uniform_block_count;
      }
   }

#ifndef NDEBUG
   /* this otherwise crashes later and is annoying to track down */
   unsigned array[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_COMPUTE_BIT,
   };
   for (unsigned i = 0; i <= MESA_SHADER_COMPUTE; i++) {
      uint16_t const_buffer_count = 0;
      uint16_t shader_buffer_count = 0;
      uint16_t sampler_count = 0;
      uint16_t sampler_view_count = 0;
      uint16_t image_count = 0;
      for (unsigned j = 0; j < layout->vk.set_count; j++) {
         if (layout->vk.set_layouts[j] == NULL)
            continue;

         const struct rvgpu_descriptor_set_layout *set_layout =
            vk_to_rvgpu_descriptor_set_layout(layout->vk.set_layouts[j]);

         if (set_layout->shader_stages & array[i]) {
            const_buffer_count += set_layout->stage[i].const_buffer_count;
            shader_buffer_count += set_layout->stage[i].shader_buffer_count;
            sampler_count += set_layout->stage[i].sampler_count;
            sampler_view_count += set_layout->stage[i].sampler_view_count;
            image_count += set_layout->stage[i].image_count;
         }
      }
      assert(const_buffer_count <= device->physical_device->device_limits.maxPerStageDescriptorUniformBuffers);
      assert(shader_buffer_count <= device->physical_device->device_limits.maxPerStageDescriptorStorageBuffers);
      assert(sampler_count <= device->physical_device->device_limits.maxPerStageDescriptorSamplers);
      assert(sampler_view_count <= device->physical_device->device_limits.maxPerStageDescriptorSampledImages);
      assert(image_count <= device->physical_device->device_limits.maxPerStageDescriptorStorageImages);
   }
#endif

   layout->push_constant_size = 0;
   for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
      const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
      layout->push_constant_size = MAX2(layout->push_constant_size,
                                        range->offset + range->size);
      layout->push_constant_stages |= (range->stageFlags & BITFIELD_MASK(MESA_SHADER_STAGES));
   }
   layout->push_constant_size = align(layout->push_constant_size, 16);
   return layout;
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateDescriptorSetLayout(VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkDescriptorSetLayout *pSetLayout)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   struct rvgpu_descriptor_set_layout *set_layout;

   uint32_t size = sizeof(struct rvgpu_descriptor_set_layout);
   set_layout = vk_descriptor_set_layout_zalloc(&device->vk, size);
   if (!set_layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   *pSetLayout = rvgpu_descriptor_set_layout_to_handle(set_layout);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL 
rvgpu_CreatePipelineLayout(VkDevice _device, 
                           const VkPipelineLayoutCreateInfo* pCreateInfo,
                           const VkAllocationCallbacks* pAllocator,
                           VkPipelineLayout* pPipelineLayout)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   struct rvgpu_pipeline_layout *layout = rvgpu_pipeline_layout_create(device, pCreateInfo, pAllocator); 
   *pPipelineLayout = rvgpu_pipeline_layout_to_handle(layout);

   return VK_SUCCESS;
}
