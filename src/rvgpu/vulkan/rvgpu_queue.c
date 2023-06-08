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

#include "vk_queue.h"

#include "rvgpu_private.h"

static void
destroy_pipelines(struct rvgpu_queue *queue)
{
   simple_mtx_lock(&queue->pipeline_lock);
   while (util_dynarray_contains(&queue->pipeline_destroys, struct rvgpu_pipeline*)) {
      rvgpu_pipeline_destroy(queue->device, util_dynarray_pop(&queue->pipeline_destroys, struct rvgpu_pipeline*), NULL);
   }
   simple_mtx_unlock(&queue->pipeline_lock);
}

static VkResult
rvgpu_queue_submit(struct vk_queue *vqueue, struct vk_queue_submit *submit)
{
   struct rvgpu_queue *queue = container_of(vqueue, struct rvgpu_queue, vk);

   VkResult result = vk_sync_wait_many(&queue->device->vk,
                                       submit->wait_count, submit->waits,
                                       VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct rvgpu_cmd_buffer *cmd_buffer =
         container_of(submit->command_buffers[i], struct rvgpu_cmd_buffer, vk);

      rvgpu_execute_cmds(queue->device, queue, cmd_buffer);
   }

#if 0
   if (submit->command_buffer_count > 0)
      queue->ctx->flush(queue->ctx, &queue->last_fence, 0);

   for (uint32_t i = 0; i < submit->signal_count; i++) {
      struct rvgpu_pipe_sync *sync =
         vk_sync_as_rvgpu_pipe_sync(submit->signals[i].sync);
      rvgpu_pipe_sync_signal_with_fence(queue->device, sync, queue->last_fence);
   }
#endif
   destroy_pipelines(queue);

   return VK_SUCCESS;
}

enum rvgpu_ctx_priority
rvgpu_get_queue_global_priority(const VkDeviceQueueGlobalPriorityCreateInfoKHR *pObj)
{  
   /* Default to MEDIUM when a specific global priority isn't requested */
   if (!pObj)
      return RVGPU_CTX_PRIORITY_MEDIUM;
      
   switch (pObj->globalPriority) {
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      return RVGPU_CTX_PRIORITY_REALTIME;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return RVGPU_CTX_PRIORITY_HIGH;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return RVGPU_CTX_PRIORITY_MEDIUM;
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      return RVGPU_CTX_PRIORITY_LOW;
   default:
      unreachable("Illegal global priority value");
      return RVGPU_CTX_PRIORITY_INVALID;
   }
}

enum rvgpu_queue_family
vk_queue_to_rvgpu(int queue_family_index)
{
   if (queue_family_index == VK_QUEUE_FAMILY_EXTERNAL ||
       queue_family_index == VK_QUEUE_FAMILY_FOREIGN_EXT)
      return RVGPU_QUEUE_FOREIGN;
   if (queue_family_index == VK_QUEUE_FAMILY_IGNORED)
      return RVGPU_QUEUE_IGNORED;

   switch (queue_family_index) {
   case 0:  return RVGPU_QUEUE_GENERAL;
   case 1:  return RVGPU_QUEUE_COMPUTE;
   default: return RVGPU_QUEUE_GENERAL;
   }

   return RVGPU_QUEUE_GENERAL;
}

int
rvgpu_queue_init(struct rvgpu_device *device, struct rvgpu_queue *queue, int idx,
                 const VkDeviceQueueCreateInfo *create_info,
                 const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority)
{
   queue->device = device;
   queue->priority = rvgpu_get_queue_global_priority(global_priority);

   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info, idx);
   if (result != VK_SUCCESS)
      return result;

   queue->state = rvgpu_init_queue_rendering_state();
   queue->vk.driver_submit = rvgpu_queue_submit;
   return VK_SUCCESS;
}

void
rvgpu_queue_finish(struct rvgpu_queue *queue)
{
   vk_queue_finish(&queue->vk);
}
