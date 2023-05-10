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

#include "rvgpu_instance.h"
#include "rvgpu_physical_device.h"
#include "rvgpu_queue.h"
#include "rvgpu_device.h"
#include "rvgpu_wsi.h"
#include "rvgpu_image.h"
#include "rvgpu_buffer.h"
#include "rvgpu_device_memory.h"
#include "rvgpu_cmd_buffer.h"

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


#endif // __RVGPU_PRIVATE_H__
