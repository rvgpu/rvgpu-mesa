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

#ifndef __RVGPU_DEVICE_H__
#define __RVGPU_DEVICE_H__

/**
 * Describe GPU h/w info needed for UMD correct initialization
 */ 
struct rvgpu_gpu_info {
    /** Asic id */
    uint32_t asic_id;
    /** Chip revision */
    uint32_t chip_rev;
    /** Chip external revision */
    uint32_t chip_external_rev;
    /** Family ID */
    uint32_t family_id;
    /** Special flags */
    uint64_t ids_flags;
    /** max engine clock*/
    uint64_t max_engine_clk;
    /** max memory clock */
    uint64_t max_memory_clk;
    /** number of shader engines */
    uint32_t num_shader_engines;
    /** number of shader arrays per engine */
    uint32_t num_shader_arrays_per_engine;
    /**  Number of available good shader pipes */
    uint32_t avail_quad_shader_pipes;
    /**  Max. number of shader pipes.(including good and bad pipes  */
    uint32_t max_quad_shader_pipes;
    /** Number of parameter cache entries per shader quad pipe */
    uint32_t cache_entries_per_quad_pipe;
    /**  Number of available graphics context */
    uint32_t num_hw_gfx_contexts;
    /** Number of render backend pipes */
    uint32_t rb_pipes;
    /**  Enabled render backend pipe mask */
    uint32_t enabled_rb_pipes_mask;
    /** Frequency of GPU Counter */
    uint32_t gpu_counter_freq;
    /** CC_RB_BACKEND_DISABLE.BACKEND_DISABLE per SE */
    uint32_t backend_disable[4];
    /** Value of MC_ARB_RAMCFG register*/
    uint32_t mc_arb_ramcfg;
    /** Value of GB_ADDR_CONFIG */
    uint32_t gb_addr_cfg;
    /** Values of the GB_TILE_MODE0..31 registers */
    uint32_t gb_tile_mode[32];
    /** Values of GB_MACROTILE_MODE0..15 registers */
    uint32_t gb_macro_tile_mode[16];
    /** Value of PA_SC_RASTER_CONFIG register per SE */
    uint32_t pa_sc_raster_cfg[4];
    /** Value of PA_SC_RASTER_CONFIG_1 register per SE */
    uint32_t pa_sc_raster_cfg1[4];
    /* CU info */
    uint32_t cu_active_number;
    uint32_t cu_ao_mask;
    uint32_t cu_bitmap[4][4];
    /* video memory type info*/
    uint32_t vram_type;
    /* video memory bit width*/
    uint32_t vram_bit_width;
    /** constant engine ram size*/
    uint32_t ce_ram_size;
    /* vce harvesting instance */
    uint32_t vce_harvest_config;
    /* PCI revision ID */
    uint32_t pci_rev_id;
};

struct rvgpu_device {
    struct rvgpu_device *next;
    int fd;
    int flink_fd;
    unsigned major_version;
    unsigned minor_version;

    char *marketing_name;
    struct rvgpu_gpu_info info;
#if 0
    /** The VA manager for the lower virtual address space */
    struct rvgpu_bo_va_mgr vamgr;
    /** The VA manager for the 32bit address space */
    struct rvgpu_bo_va_mgr vamgr_32;
    /** The VA manager for the high virtual address space */
    struct rvgpu_bo_va_mgr vamgr_high;
    /** The VA manager for the 32bit high address space */
    struct rvgpu_bo_va_mgr vamgr_high_32;
#endif
};

typedef struct rvgpu_device *rvgpu_device_handle;

int rvgpu_device_initialize(int fd, uint32_t *major_version, uint32_t *minor_version, rvgpu_device_handle *device_handle);

#endif // __RVGPU_DEVICE_H__