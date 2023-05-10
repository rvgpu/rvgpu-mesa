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

#ifndef RVGPU_SURFACE_H__
#define RVGPU_SURFACE_H__

#include <stdint.h>

struct rvgpu_surf_info {
   uint32_t width;
   uint32_t height;
   uint32_t depth;
   uint8_t samples;         /* For Z/S: samples; For color: FMASK coverage samples */
   uint8_t storage_samples; /* For color: allocated samples */
   uint8_t levels;
   uint8_t num_channels; /* heuristic for displayability */
   uint16_t array_size;
   uint32_t *surf_index; /* Set a monotonic counter for tile swizzling. */
   uint32_t *fmask_surf_index;
}; 

struct rvgpu_surf {
   /* Format properties. */
   uint8_t blk_w : 4;
   uint8_t blk_h : 4;
   uint8_t bpe : 5;
   /* Display, standard(thin), depth, render(rotated). AKA D,S,Z,R swizzle modes. */
   uint8_t micro_tile_mode : 3;
   /* Number of mipmap levels where DCC or HTILE is enabled starting from level 0.
    * Non-zero levels may be disabled due to alignment constraints, but not
    * the first level.
    */
   uint8_t num_meta_levels : 4;
   uint8_t is_linear : 1;
   uint8_t has_stencil : 1;
   /* This might be true even if micro_tile_mode isn't displayable or rotated. */
   uint8_t is_displayable : 1;
   uint8_t first_mip_tail_level : 4;

   /* These are return values. Some of them can be set by the caller, but
    * they will be treated as hints (e.g. bankw, bankh) and might be
    * changed by the calculator.
    */

   /* Not supported yet for depth + stencil. */
   uint16_t prt_tile_width;
   uint16_t prt_tile_height;
   uint16_t prt_tile_depth;

   /* Tile swizzle can be OR'd with low bits of the BASE_256B address.
    * The value is the same for all mipmap levels. Supported tile modes:
    * - GFX6: Only macro tiling.
    * - GFX9: Only *_X and *_T swizzle modes. Level 0 must not be in the mip
    *   tail.
    *
    * Only these surfaces are allowed to set it:
    * - color (if it doesn't have to be displayable)
    * - DCC (same tile swizzle as color)
    * - FMASK
    * - CMASK if it's TC-compatible or if the gen is GFX9
    * - depth/stencil if HTILE is not TC-compatible and if the gen is not GFX9
    */
   uint16_t tile_swizzle; /* it has 16 bits because gfx11 shifts it by 2 bits */
   uint8_t fmask_tile_swizzle;

   /* Use (1 << log2) to compute the alignment. */
   uint8_t surf_alignment_log2;
   uint8_t fmask_alignment_log2;
   uint8_t meta_alignment_log2; /* DCC or HTILE */
   uint8_t cmask_alignment_log2;
   uint8_t alignment_log2;

   /* DRM format modifier. Set to DRM_FORMAT_MOD_INVALID to have addrlib
    * select tiling parameters instead.
    */
   uint64_t modifier;
   uint64_t flags;

   uint64_t surf_size;
   uint64_t fmask_size;
   uint32_t fmask_slice_size; /* max 2^31 (16K * 16K * 8) */

   /* DCC and HTILE (they are very small) */
   uint32_t meta_size;
   uint32_t meta_slice_size;
   uint32_t meta_pitch;

   uint32_t cmask_size;
   uint32_t cmask_slice_size;
   uint16_t cmask_pitch; /* GFX9+ */
   uint16_t cmask_height; /* GFX9+ */

   /* All buffers combined. */
   uint64_t meta_offset; /* DCC or HTILE */
   uint64_t fmask_offset;
   uint64_t cmask_offset;
   uint64_t display_dcc_offset;
   uint64_t total_size;
};

#define RVGPU_SURF_TYPE_MASK     0xFF
#define RVGPU_SURF_TYPE_SHIFT    0
#define RVGPU_SURF_TYPE_1D       0
#define RVGPU_SURF_TYPE_2D       1
#define RVGPU_SURF_TYPE_3D       2
#define RVGPU_SURF_TYPE_CUBEMAP  3
#define RVGPU_SURF_TYPE_1D_ARRAY 4
#define RVGPU_SURF_TYPE_2D_ARRAY 5
#define RVGPU_SURF_MODE_MASK     0xFF
#define RVGPU_SURF_MODE_SHIFT    8

#define RVGPU_SURF_GET(v, field)                                                                   \
   (((v) >> RVGPU_SURF_##field##_SHIFT) & RVGPU_SURF_##field##_MASK)
#define RVGPU_SURF_SET(v, field) (((v)&RVGPU_SURF_##field##_MASK) << RVGPU_SURF_##field##_SHIFT)
#define RVGPU_SURF_CLR(v, field)                                                                   \
   ((v) & ~(RVGPU_SURF_##field##_MASK << RVGPU_SURF_##field##_SHIFT))

struct rvgpu_winsys;

void rvgpu_winsys_surface_init_functions(struct rvgpu_winsys *ws);

#endif
