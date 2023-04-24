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

#ifndef __RVGPU_PERFCOUNTER_H__
#define __RVGPU_PERFCOUNTER_H__

enum ac_pc_gpu_block {
   CPF     = 0x0,
   IA      = 0x1,
   VGT     = 0x2,
   PA_SU   = 0x3,
   PA_SC   = 0x4,
   SPI     = 0x5,
   SQ      = 0x6,
   SX      = 0x7,
   TA      = 0x8,
   TD      = 0x9,
   TCP     = 0xA,
   TCC     = 0xB,
   TCA     = 0xC,
   DB      = 0xD,
   CB      = 0xE,
   GDS     = 0xF,
   SRBM    = 0x10,
   GRBM    = 0x11,
   GRBMSE  = 0x12,
   RLC     = 0x13,
   DMA     = 0x14,
   MC      = 0x15,
   CPG     = 0x16,
   CPC     = 0x17,
   WD      = 0x18,
   TCS     = 0x19,
   ATC     = 0x1A,
   ATCL2   = 0x1B,
   MCVML2  = 0x1C,
   EA      = 0x1D,
   RPB     = 0x1E,
   RMI     = 0x1F,
   UMCCH   = 0x20,
   GE      = 0x21,
   GE1     = GE,
   GL1A    = 0x22,
   GL1C    = 0x23,
   GL1CG   = 0x24,
   GL2A    = 0x25,
   GL2C    = 0x26,
   CHA     = 0x27,
   CHC     = 0x28,
   CHCG    = 0x29,
   GUS     = 0x2A,
   GCR     = 0x2B,
   PA_PH   = 0x2C,
   UTCL1   = 0x2D,
   GEDIST  = 0x2E,
   GESE    = 0x2F,
   DF      = 0x30,
   NUM_GPU_BLOCK,
};

struct ac_pc_block_base {
   enum ac_pc_gpu_block gpu_block;
   const char *name;
   unsigned num_counters;
   unsigned flags;

   unsigned select_or;
   unsigned *select0;
   unsigned counter0_lo;
   unsigned *counters;

   /* SPM */
   unsigned num_spm_counters;
   unsigned num_spm_wires;
   unsigned *select1;
   unsigned spm_block_select;
}; 

struct ac_pc_block_gfxdescr {
   struct ac_pc_block_base *b;
   unsigned selectors; 
   unsigned instances;
};

struct ac_pc_block {
   const struct ac_pc_block_gfxdescr *b;
   unsigned num_instances;
   
   unsigned num_groups;
   char *group_names;
   unsigned group_name_stride;
   
   char *selector_names;
   unsigned selector_name_stride;
}; 

struct ac_perfcounters {
   unsigned num_groups;
   unsigned num_blocks;
   struct ac_pc_block *blocks;

   bool separate_se;
   bool separate_instance;
};

void ac_destroy_perfcounters(struct ac_perfcounters *pc);

#endif //__RVGPU_PERFCOUNTER_H__
