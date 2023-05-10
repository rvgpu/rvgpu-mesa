/*
 * Copyright © 2023 Sietium Semiconductor.
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

#ifndef DRM_IOCTL_H__
#define DRM_IOCTL_H__

#include <xf86drm.h>

#define RVGPU_DRM_IOC_W(cmdIndex, size) DRM_IOC( DRM_IOC_WRITE, DRM_IOCTL_BASE, DRM_COMMAND_BASE + cmdIndex, size);

#define AMDGPU_INFO_ACCEL_WORKING   

/* Input structure for the INFO ioctl */
struct drm_amdgpu_info {
    /* Where the return value will be stored */
    __u64 return_pointer;
    /* The size of the return value. Just like "size" in "snprintf",
     * it limits how many bytes the kernel can write. */
    __u32 return_size;
    /* The query request id. */
    __u32 query;
};

struct rvgpu_drm_device {
    int fd;
};

typedef struct rvgpu_drm_device *rvgpu_drm_device_handle;

int rvgpu_drm_device_initialize(int fd, uint32_t *major_version, uint32_t *minor_version, rvgpu_drm_device_handle *device_handle);
void rvgpu_drm_device_deinitialize(rvgpu_drm_device_handle device_handle);

#endif // DRM_IOCTL_H__
