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

#ifndef __RVGPU_CONSTANTS_H__
#define __RVGPU_CONSTANTS_H__

// pci vendor id
#define SIETIUM_VENDOR_ID 0x16c3
#define RVGPU_DEVICE_ID   0x0001

#define MAX_VBS                        32
#define MAX_VERTEX_ATTRIBS             32
#define MAX_RTS                        8
#define MAX_VIEWPORTS                  16
#define MAX_SCISSORS                   16
#define MAX_DISCARD_RECTANGLES         4
#define MAX_SAMPLE_LOCATIONS           32
#define MAX_PUSH_CONSTANTS_SIZE        256
#define MAX_PUSH_DESCRIPTORS           32
#define MAX_DYNAMIC_UNIFORM_BUFFERS    16
#define MAX_DYNAMIC_STORAGE_BUFFERS    8
#define MAX_DYNAMIC_BUFFERS            (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define MAX_SAMPLES_LOG2               4
#define NUM_META_FS_KEYS               12
#define RVGPU_MAX_DRM_DEVICES          8
#define MAX_VIEWS                      8
#define MAX_SO_STREAMS                 4
#define MAX_SO_BUFFERS                 4
#define MAX_SO_OUTPUTS                 64
#define MAX_INLINE_UNIFORM_BLOCK_SIZE  (4ull * 1024 * 1024)
#define MAX_INLINE_UNIFORM_BLOCK_COUNT 64
#define MAX_BIND_POINTS                3 /* compute + graphics + raytracing */

#define NUM_DEPTH_CLEAR_PIPELINES      2
#define NUM_DEPTH_DECOMPRESS_PIPELINES 3
#define MAX_FRAMEBUFFER_WIDTH (1u << 14)
#define MAX_FRAMEBUFFER_HEIGHT (1u << 14)

/* max number of descriptor sets */
#define MAX_SETS 32

/* Make sure everything is addressable by a signed 32-bit int, and
 * our largest descriptors are 96 bytes.
 */
#define RVGPU_MAX_PER_SET_DESCRIPTORS ((1ull << 31) / 96)

/* Our buffer size fields allow only 2**32 - 1. We round that down to a multiple
 * of 4 bytes so we can align buffer sizes up.
 */
#define RVGPU_MAX_MEMORY_ALLOCATION_SIZE 0xFFFFFFFCull

/* Number of invocations in each subgroup. */
#define RVGPU_SUBGROUP_SIZE 64

/* The spec requires this to be 32. */
#define RVGPU_RT_HANDLE_SIZE 32

#define RVGPU_MAX_HIT_ATTRIB_SIZE 32

#define RVGPU_SHADER_ALLOC_ALIGNMENT      256
#define RVGPU_SHADER_ALLOC_MIN_ARENA_SIZE (256 * 1024)
/* 256 KiB << 5 = 8 MiB */
#define RVGPU_SHADER_ALLOC_MAX_ARENA_SIZE_SHIFT 5u
#define RVGPU_SHADER_ALLOC_MIN_SIZE_CLASS 8
#define RVGPU_SHADER_ALLOC_MAX_SIZE_CLASS 15
#define RVGPU_SHADER_ALLOC_NUM_FREE_LISTS                                                           \
   (RVGPU_SHADER_ALLOC_MAX_SIZE_CLASS - RVGPU_SHADER_ALLOC_MIN_SIZE_CLASS + 1)

#endif //__RVGPU_CONSTANTS_H__
