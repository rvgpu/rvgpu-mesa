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

#ifndef __RVGPU_WINSYS_H__
#define __RVGPU_WINSYS_H__

#include "vk_sync.h"
#include "vk_sync_timeline.h"

#include "drm_ioctl.h"

#include "rvgpu_gpuinfo.h"
#include "rvgpu_surface.h"

enum rvgpu_ctx_priority {
   RVGPU_CTX_PRIORITY_INVALID = -1,
   RVGPU_CTX_PRIORITY_LOW = 0,
   RVGPU_CTX_PRIORITY_MEDIUM,
   RVGPU_CTX_PRIORITY_HIGH,
   RVGPU_CTX_PRIORITY_REALTIME,
};

enum amd_ip_type
{  
   AMD_IP_GFX = 0,
   AMD_IP_COMPUTE,
   AMD_IP_SDMA,
   AMD_IP_UVD,
   AMD_IP_VCE,
   AMD_IP_UVD_ENC,
   AMD_IP_VCN_DEC,
   AMD_IP_VCN_ENC,
   AMD_IP_VCN_UNIFIED = AMD_IP_VCN_ENC,
   AMD_IP_VCN_JPEG,
   AMD_NUM_IP_TYPES,
};

struct rvgpu_winsys_ctx;

enum radeon_bo_domain { /* bitfield */
                        RADEON_DOMAIN_GTT = 2,
                        RADEON_DOMAIN_VRAM = 4,
                        RADEON_DOMAIN_VRAM_GTT = RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GTT,
                        RADEON_DOMAIN_GDS = 8,
                        RADEON_DOMAIN_OA = 16,
}; 

enum radeon_bo_flag { /* bitfield */
                      RADEON_FLAG_GTT_WC = (1 << 0),
                      RADEON_FLAG_CPU_ACCESS = (1 << 1),
                      RADEON_FLAG_NO_CPU_ACCESS = (1 << 2),
                      RADEON_FLAG_VIRTUAL = (1 << 3),
                      RADEON_FLAG_VA_UNCACHED = (1 << 4),
                      RADEON_FLAG_IMPLICIT_SYNC = (1 << 5),
                      RADEON_FLAG_NO_INTERPROCESS_SHARING = (1 << 6),
                      RADEON_FLAG_READ_ONLY = (1 << 7),
                      RADEON_FLAG_32BIT = (1 << 8),
                      RADEON_FLAG_PREFER_LOCAL_BO = (1 << 9),
                      RADEON_FLAG_ZERO_VRAM = (1 << 10),
                      RADEON_FLAG_REPLAYABLE = (1 << 11),
                      RADEON_FLAG_DISCARDABLE = (1 << 12),
};

enum radeon_bo_layout {
   RADEON_LAYOUT_LINEAR = 0,
   RADEON_LAYOUT_TILED,
   RADEON_LAYOUT_SQUARETILED,

   RADEON_LAYOUT_UNKNOWN
};

/* Tiling info for display code, DRI sharing, and other data. */
struct radeon_bo_metadata {
   /* Tiling flags describing the texture layout for display code
    * and DRI sharing.
    */
   union {
      struct {
         enum radeon_bo_layout microtile;
         enum radeon_bo_layout macrotile;
         unsigned pipe_config;
         unsigned bankw;
         unsigned bankh;
         unsigned tile_split;
         unsigned mtilea;
         unsigned num_banks;
         unsigned stride;
         bool scanout;
      } legacy;

      struct {
         /* surface flags */
         unsigned swizzle_mode : 5;
         bool scanout;
         uint32_t dcc_offset_256b;
         uint32_t dcc_pitch_max;
         bool dcc_independent_64b_blocks;
         bool dcc_independent_128b_blocks;
         unsigned dcc_max_compressed_block_size;
      } gfx9;
   } u;

   /* Additional metadata associated with the buffer, in bytes.
    * The maximum size is 64 * 4. This is opaque for the winsys & kernel.
    * Supported by amdgpu only.
    */
   uint32_t size_metadata;
   uint32_t metadata[64];
};

struct rvgpu_winsys_bo {
   uint64_t va;
   bool is_local;
   bool vram_no_cpu_access;
   bool use_global_list;
   enum radeon_bo_domain initial_domain;
};

struct rvgpu_winsys_bo_list {
   struct rvgpu_winsys_bo **bos;
   unsigned count;
};

struct rvgpu_winsys_submit_info {
   enum amd_ip_type ip_type;
   int queue_index;
   unsigned cs_count;
   unsigned initial_preamble_count;
   unsigned continue_preamble_count;
   unsigned postamble_count;
   struct radeon_cmdbuf **cs_array;
   struct radeon_cmdbuf **initial_preamble_cs;
   struct radeon_cmdbuf **continue_preamble_cs;
   struct radeon_cmdbuf **postamble_cs;
   bool uses_shadow_regs;
};

enum radeon_value_id {
   RADEON_ALLOCATED_VRAM,
   RADEON_ALLOCATED_VRAM_VIS,
   RADEON_ALLOCATED_GTT,
   RADEON_TIMESTAMP,
   RADEON_NUM_BYTES_MOVED,
   RADEON_NUM_EVICTIONS,
   RADEON_NUM_VRAM_CPU_PAGE_FAULTS,
   RADEON_VRAM_USAGE,
   RADEON_VRAM_VIS_USAGE,
   RADEON_GTT_USAGE,
   RADEON_GPU_TEMPERATURE,
   RADEON_CURRENT_SCLK,
   RADEON_CURRENT_MCLK,
};

struct rvgpu_winsys;

struct rvgpu_winsys_ops {
   void (*destroy)(struct rvgpu_winsys *ws);

   void (*query_info)(struct rvgpu_winsys *ws, struct radeon_info *info);

   uint64_t (*query_value)(struct rvgpu_winsys *ws, enum radeon_value_id value);

   bool (*read_registers)(struct rvgpu_winsys *ws, unsigned reg_offset, unsigned num_registers,
                          uint32_t *out);

   const char *(*get_chip_name)(struct rvgpu_winsys *ws);

   VkResult (*buffer_create)(struct rvgpu_winsys *ws, uint64_t size, unsigned alignment,
                             enum radeon_bo_domain domain, enum radeon_bo_flag flags,
                             unsigned priority, uint64_t address, struct rvgpu_winsys_bo **out_bo);

   void (*buffer_destroy)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo);
   void *(*buffer_map)(struct rvgpu_winsys_bo *bo);

   VkResult (*buffer_from_ptr)(struct rvgpu_winsys *ws, void *pointer, uint64_t size,
                               unsigned priority, struct rvgpu_winsys_bo **out_bo);

   VkResult (*buffer_from_fd)(struct rvgpu_winsys *ws, int fd, unsigned priority,
                              struct rvgpu_winsys_bo **out_bo, uint64_t *alloc_size);

   bool (*buffer_get_fd)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo, int *fd);

   bool (*buffer_get_flags_from_fd)(struct rvgpu_winsys *ws, int fd,
                                    enum radeon_bo_domain *domains, enum radeon_bo_flag *flags);

   void (*buffer_unmap)(struct rvgpu_winsys_bo *bo);

   void (*buffer_set_metadata)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo,
                               struct radeon_bo_metadata *md);
   void (*buffer_get_metadata)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo,
                               struct radeon_bo_metadata *md);

   VkResult (*buffer_virtual_bind)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *parent,
                                   uint64_t offset, uint64_t size, struct rvgpu_winsys_bo *bo,
                                   uint64_t bo_offset);

   VkResult (*buffer_make_resident)(struct rvgpu_winsys *ws, struct rvgpu_winsys_bo *bo,
                                    bool resident);

   VkResult (*ctx_create)(struct rvgpu_winsys *ws, enum rvgpu_ctx_priority priority,
                          struct rvgpu_winsys_ctx **ctx);
   void (*ctx_destroy)(struct rvgpu_winsys_ctx *ctx);

   bool (*ctx_wait_idle)(struct rvgpu_winsys_ctx *ctx, enum amd_ip_type amd_ip_type, int ring_index);

   int (*ctx_set_pstate)(struct rvgpu_winsys_ctx *ctx, uint32_t pstate);

   enum rvgpu_reset_status (*ctx_query_reset_status)(struct rvgpu_winsys_ctx *rwctx);

   enum radeon_bo_domain (*cs_domain)(const struct rvgpu_winsys *ws);

   struct radeon_cmdbuf *(*cs_create)(struct rvgpu_winsys *ws, enum amd_ip_type amd_ip_type,
                                      bool is_secondary);

   void (*cs_destroy)(struct radeon_cmdbuf *cs);

   void (*cs_reset)(struct radeon_cmdbuf *cs);

   bool (*cs_chain)(struct radeon_cmdbuf *cs, struct radeon_cmdbuf *next_cs, bool pre_en);

   void (*cs_unchain)(struct radeon_cmdbuf *cs);

   VkResult (*cs_finalize)(struct radeon_cmdbuf *cs);

   void (*cs_grow)(struct radeon_cmdbuf *cs, size_t min_size);

   VkResult (*cs_submit)(struct rvgpu_winsys_ctx *ctx,
                         const struct rvgpu_winsys_submit_info *submit, uint32_t wait_count,
                         const struct vk_sync_wait *waits, uint32_t signal_count,
                         const struct vk_sync_signal *signals);

   void (*cs_add_buffer)(struct radeon_cmdbuf *cs, struct rvgpu_winsys_bo *bo);

   void (*cs_execute_secondary)(struct radeon_cmdbuf *parent, struct radeon_cmdbuf *child,
                                bool allow_ib2);

   void (*cs_dump)(struct radeon_cmdbuf *cs, FILE *file, const int *trace_ids, int trace_id_count);

   void (*dump_bo_ranges)(struct rvgpu_winsys *ws, FILE *file);

   void (*dump_bo_log)(struct rvgpu_winsys *ws, FILE *file);

   int (*surface_init)(struct rvgpu_winsys *ws, const struct ac_surf_info *surf_info,
                       struct radeon_surf *surf);

   int (*get_fd)(struct rvgpu_winsys *ws);

   struct ac_addrlib *(*get_addrlib)(struct rvgpu_winsys *ws);

   const struct vk_sync_type *const *(*get_sync_types)(struct rvgpu_winsys *ws);
};

struct rvgpu_winsys {
   struct rvgpu_winsys_ops ops;

   rvgpu_drm_device_handle dev;

   const struct vk_sync_type *sync_types[3];
   struct vk_sync_type syncobj_sync_type;
   struct vk_sync_timeline_type emulated_timeline_sync_type;
};

void rvgpu_winsys_destroy(struct rvgpu_winsys *ws);
struct rvgpu_winsys * rvgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags);

#endif // __RVGPU_WINSYS_H__
