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

#include "rvgpu_private.h"

static VkResult
rvgpu_create_cmd_buffer(struct vk_command_pool *pool,
                        struct vk_command_buffer **cmd_buffer_out)
{
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
