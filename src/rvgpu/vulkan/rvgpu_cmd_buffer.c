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

#include "vk_command_buffer.h"
#include "vk_command_pool.h"

#include "rvgpu_private.h"

static VkResult
rvgpu_create_cmd_buffer(struct vk_command_pool *pool,
                        struct vk_command_buffer **cmd_buffer_out)
{
   struct rvgpu_device *device = container_of(pool->base.device, struct rvgpu_device, vk);

   struct rvgpu_cmd_buffer *cmd_buffer;
   cmd_buffer = vk_zalloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_command_buffer_init(pool, &cmd_buffer->vk, &rvgpu_cmd_buffer_ops, 0);
   if (result != VK_SUCCESS) {
      vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;

   *cmd_buffer_out = &cmd_buffer->vk;

   return VK_SUCCESS;
}

static void
rvgpu_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                       UNUSED VkCommandBufferResetFlags flags)
{
}

static void
rvgpu_destroy_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer)
{
}

const struct vk_command_buffer_ops rvgpu_cmd_buffer_ops = {
   .create = rvgpu_create_cmd_buffer,
   .reset = rvgpu_reset_cmd_buffer,
   .destroy = rvgpu_destroy_cmd_buffer,
};

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
   RVGPU_FROM_HANDLE(rvgpu_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result = VK_SUCCESS;

   vk_command_buffer_begin(&cmd_buffer->vk, pBeginInfo);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                            uint32_t bindingCount, const VkBuffer *pBuffers,
                            const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes,
                            const VkDeviceSize *pStrides)
{
   // RVGPU_FROM_HANDLE(rvgpu_cmd_buffer, cmd_buffer, commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                          const VkDependencyInfo *pDependencyInfo)
{
   // RVGPU_FROM_HANDLE(rvgpu_cmd_buffer, cmd_buffer, commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                            const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   // RVGPU_FROM_HANDLE(rvgpu_cmd_buffer, cmd_buffer, commandBuffer);
   // RVGPU_FROM_HANDLE(rvgpu_image, src_image, pCopyImageToBufferInfo->srcImage);
   // RVGPU_FROM_HANDLE(rvgpu_buffer, dst_buffer, pCopyImageToBufferInfo->dstBuffer);

   // for (unsigned r = 0; r < pCopyImageToBufferInfo->regionCount; r++) {
      // copy_image_to_buffer(cmd_buffer, dst_buffer, src_image,
      //                     pCopyImageToBufferInfo->srcImageLayout,
      //                     &pCopyImageToBufferInfo->pRegions[r]);
   // }
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   RVGPU_FROM_HANDLE(rvgpu_cmd_buffer, cmd_buffer, commandBuffer);
   return vk_command_buffer_end(&cmd_buffer->vk);
}
