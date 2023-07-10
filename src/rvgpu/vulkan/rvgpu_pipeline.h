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

#ifndef RVGPU_PIPELINE_H__
#define RVGPU_PIPELINE_H__

#include "pipe/p_state.h"
#include "spirv/nir_spirv.h"

#include "vk_pipeline_layout.h"

#include "rvgpu_descriptor_set.h"

struct rvgpu_inline_variant {
   uint32_t mask;
   uint32_t vals[PIPE_MAX_CONSTANT_BUFFERS][MAX_INLINABLE_UNIFORMS];
   void *cso;
};

struct rvgpu_sampler {
    struct vk_object_base base;
    struct pipe_sampler_state state;
};

struct rvgpu_pipeline_layout {
    struct vk_pipeline_layout vk;

    uint32_t push_constant_size;
    VkShaderStageFlags push_constant_stages;
    struct {
        uint16_t uniform_block_size;
        uint16_t uniform_block_count;
        uint16_t uniform_block_sizes[MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS];
    } stage[MESA_SHADER_STAGES];
};

struct rvgpu_access_info {
    uint64_t images_read;
    uint64_t images_written;
    uint64_t buffers_written;
};

struct rvgpu_pipeline_nir {
    int ref_cnt;
    nir_shader *nir;
};

struct rvgpu_shader {
    struct vk_object_base base;
    struct rvgpu_pipeline_layout *layout;
    struct rvgpu_access_info access;
    struct rvgpu_pipeline_nir *pipeline_nir;
    struct rvgpu_pipeline_nir *tess_ccw;
    void *shader_cso;
    void *tess_ccw_cso;
    struct {
        uint32_t uniform_offsets[PIPE_MAX_CONSTANT_BUFFERS][MAX_INLINABLE_UNIFORMS];
        uint8_t count[PIPE_MAX_CONSTANT_BUFFERS];
        bool must_inline;
        uint32_t can_inline; //bitmask
        struct set variants;
    } inlines;
    struct pipe_stream_output_info stream_output;
    struct blob blob; //preserved for GetShaderBinaryDataEXT
};

struct rvgpu_pipeline {
    struct vk_object_base base;

    struct rvgpu_device *device;
    struct rvgpu_pipeline_layout *layout;

    void *state_data;
    bool is_compute_pipeline;
    bool force_min_sample;
    struct rvgpu_shader shaders[MESA_SHADER_STAGES];
    gl_shader_stage last_vertex;
    struct vk_graphics_pipeline_state graphics_state;
    VkGraphicsPipelineLibraryFlagsEXT stages;
    bool line_smooth;
    bool disable_multisample;
    bool line_rectangular;
    bool library;
    bool compiled;
    bool used;
};

bool rvgpu_find_inlinable_uniforms(struct rvgpu_shader *shader, nir_shader *nir);

static inline const struct rvgpu_descriptor_set_layout *
vk_to_rvgpu_descriptor_set_layout(const struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, const struct rvgpu_descriptor_set_layout, vk);
}

static inline const struct rvgpu_descriptor_set_layout *
get_set_layout(const struct rvgpu_pipeline_layout *layout, uint32_t set)
{
   return container_of(layout->vk.set_layouts[set],
                       const struct rvgpu_descriptor_set_layout, vk);
}

static inline const struct rvgpu_descriptor_set_binding_layout *
get_binding_layout(const struct rvgpu_pipeline_layout *layout,
                   uint32_t set, uint32_t binding)
{
    return &get_set_layout(layout, set)->binding[binding];
}

void rvgpu_shader_optimize(nir_shader *nir);
void rvgpu_inline_uniforms(nir_shader *nir, const struct rvgpu_shader *shader, const uint32_t *uniform_values, uint32_t ubo);

void rvgpu_lower_pipeline_layout(const struct rvgpu_device *device,
                                 struct rvgpu_pipeline_layout *layout,
                                 nir_shader *shader);

void rvgpu_shader_lower(struct rvgpu_device *pdevice, nir_shader *nir,
                        struct rvgpu_shader *shader,
                        struct rvgpu_pipeline_layout *layout);

VkResult vk_pipeline_shader_stage_to_nir(struct vk_device *device,
                                         const VkPipelineShaderStageCreateInfo *info,
                                         const struct spirv_to_nir_options *spirv_options,
                                         const struct nir_shader_compiler_options *nir_options,
                                         void *mem_ctx, nir_shader **nir_out);

void *rvgpu_shader_compile(struct rvgpu_device *device, struct rvgpu_shader *shader, nir_shader *nir);
void rvgpu_pipeline_shaders_compile(struct rvgpu_pipeline *pipeline);

void rvgpu_pipeline_init(struct rvgpu_device *device, struct rvgpu_pipeline *pipeline);
void rvgpu_pipeline_destroy(struct rvgpu_device *device, struct rvgpu_pipeline *pipeline,
                       const VkAllocationCallbacks *allocator);
#endif // RVGPU_PIPELINE_H__
