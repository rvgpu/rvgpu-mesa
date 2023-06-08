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

#ifndef RVGPU_QUEUE_H__
#define RVGPU_QUEUE_H__

#include "vk_queue.h"

#include "rvgpu_winsys.h"

/* queue types */
enum rvgpu_queue_family {
   RVGPU_QUEUE_GENERAL,
   RVGPU_QUEUE_COMPUTE,
   RVGPU_QUEUE_TRANSFER,
   RVGPU_MAX_QUEUE_FAMILIES,
   RVGPU_QUEUE_FOREIGN = RVGPU_MAX_QUEUE_FAMILIES,
   RVGPU_QUEUE_IGNORED,
};

struct rvgpu_queue {
   struct vk_queue vk;
   struct rvgpu_device *device;
   enum rvgpu_ctx_priority priority;

   struct pipe_context *ctx;
   struct cso_context *cso;
   struct u_upload_mgr *uploader;
   struct pipe_fence_handle *last_fence;

   void *state;

   struct util_dynarray pipeline_destroys;
   simple_mtx_t pipeline_lock;
};

enum rvgpu_ctx_priority rvgpu_get_queue_global_priority(const VkDeviceQueueGlobalPriorityCreateInfoKHR *pObj);

int rvgpu_queue_init(struct rvgpu_device *device, struct rvgpu_queue *queue,  int idx, 
                     const VkDeviceQueueCreateInfo *create_info, 
                     const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority);
void rvgpu_queue_finish(struct rvgpu_queue *queue);

enum rvgpu_queue_family vk_queue_to_rvgpu(int queue_family_index);

#endif // RVGPU_QUEUE_H__
