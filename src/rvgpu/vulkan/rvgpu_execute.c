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

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_util.h"

#include "rvgpu_private.h"

void rvgpu_add_enqueue_cmd_entrypoints(struct vk_device_dispatch_table *disp)
{
   struct vk_device_dispatch_table cmd_enqueue_dispatch;
   vk_device_dispatch_table_from_entrypoints(&cmd_enqueue_dispatch,
      &vk_cmd_enqueue_device_entrypoints, true);

#define ENQUEUE_CMD(CmdName) \
   assert(cmd_enqueue_dispatch.CmdName != NULL); \
   disp->CmdName = cmd_enqueue_dispatch.CmdName;

   /* This list needs to match what's in rvgpu_execute_cmd_buffer exactly */
   ENQUEUE_CMD(CmdBindPipeline)
   ENQUEUE_CMD(CmdSetViewport)
   ENQUEUE_CMD(CmdSetViewportWithCount)
   ENQUEUE_CMD(CmdSetScissor)
   ENQUEUE_CMD(CmdSetScissorWithCount)
   ENQUEUE_CMD(CmdSetLineWidth)
   ENQUEUE_CMD(CmdSetDepthBias)
   ENQUEUE_CMD(CmdSetBlendConstants)
   ENQUEUE_CMD(CmdSetDepthBounds)
   ENQUEUE_CMD(CmdSetStencilCompareMask)
   ENQUEUE_CMD(CmdSetStencilWriteMask)
   ENQUEUE_CMD(CmdSetStencilReference)
   ENQUEUE_CMD(CmdBindDescriptorSets)
   ENQUEUE_CMD(CmdBindIndexBuffer)
   ENQUEUE_CMD(CmdBindVertexBuffers2)
   ENQUEUE_CMD(CmdDraw)
   ENQUEUE_CMD(CmdDrawMultiEXT)
   ENQUEUE_CMD(CmdDrawIndexed)
   ENQUEUE_CMD(CmdDrawIndirect)
   ENQUEUE_CMD(CmdDrawIndexedIndirect)
   ENQUEUE_CMD(CmdDrawMultiIndexedEXT)
   ENQUEUE_CMD(CmdDispatch)
   ENQUEUE_CMD(CmdDispatchBase)
   ENQUEUE_CMD(CmdDispatchIndirect)
   ENQUEUE_CMD(CmdCopyBuffer2)
   ENQUEUE_CMD(CmdCopyImage2)
   ENQUEUE_CMD(CmdBlitImage2)
   ENQUEUE_CMD(CmdCopyBufferToImage2)
   ENQUEUE_CMD(CmdCopyImageToBuffer2)
   ENQUEUE_CMD(CmdUpdateBuffer)
   ENQUEUE_CMD(CmdFillBuffer)
   ENQUEUE_CMD(CmdClearColorImage)
   ENQUEUE_CMD(CmdClearDepthStencilImage)
   ENQUEUE_CMD(CmdClearAttachments)
   ENQUEUE_CMD(CmdResolveImage2)
   ENQUEUE_CMD(CmdBeginQueryIndexedEXT)
   ENQUEUE_CMD(CmdEndQueryIndexedEXT)
   ENQUEUE_CMD(CmdBeginQuery)
   ENQUEUE_CMD(CmdEndQuery)
   ENQUEUE_CMD(CmdResetQueryPool)
   ENQUEUE_CMD(CmdCopyQueryPoolResults)
   ENQUEUE_CMD(CmdPushConstants)
   ENQUEUE_CMD(CmdExecuteCommands)
   ENQUEUE_CMD(CmdDrawIndirectCount)
   ENQUEUE_CMD(CmdDrawIndexedIndirectCount)
   ENQUEUE_CMD(CmdPushDescriptorSetKHR)
//   ENQUEUE_CMD(CmdPushDescriptorSetWithTemplateKHR)
   ENQUEUE_CMD(CmdBindTransformFeedbackBuffersEXT)
   ENQUEUE_CMD(CmdBeginTransformFeedbackEXT)
   ENQUEUE_CMD(CmdEndTransformFeedbackEXT)
   ENQUEUE_CMD(CmdDrawIndirectByteCountEXT)
   ENQUEUE_CMD(CmdBeginConditionalRenderingEXT)
   ENQUEUE_CMD(CmdEndConditionalRenderingEXT)
   ENQUEUE_CMD(CmdSetVertexInputEXT)
   ENQUEUE_CMD(CmdSetCullMode)
   ENQUEUE_CMD(CmdSetFrontFace)
   ENQUEUE_CMD(CmdSetPrimitiveTopology)
   ENQUEUE_CMD(CmdSetDepthTestEnable)
   ENQUEUE_CMD(CmdSetDepthWriteEnable)
   ENQUEUE_CMD(CmdSetDepthCompareOp)
   ENQUEUE_CMD(CmdSetDepthBoundsTestEnable)
   ENQUEUE_CMD(CmdSetStencilTestEnable)
   ENQUEUE_CMD(CmdSetStencilOp)
   ENQUEUE_CMD(CmdSetLineStippleEXT)
   ENQUEUE_CMD(CmdSetDepthBiasEnable)
   ENQUEUE_CMD(CmdSetLogicOpEXT)
   ENQUEUE_CMD(CmdSetPatchControlPointsEXT)
   ENQUEUE_CMD(CmdSetPrimitiveRestartEnable)
   ENQUEUE_CMD(CmdSetRasterizerDiscardEnable)
   ENQUEUE_CMD(CmdSetColorWriteEnableEXT)
   ENQUEUE_CMD(CmdBeginRendering)
   ENQUEUE_CMD(CmdEndRendering)
   ENQUEUE_CMD(CmdSetDeviceMask)
   ENQUEUE_CMD(CmdPipelineBarrier2)
   ENQUEUE_CMD(CmdResetEvent2)
   ENQUEUE_CMD(CmdSetEvent2)
   ENQUEUE_CMD(CmdWaitEvents2)
   ENQUEUE_CMD(CmdWriteTimestamp2)

   ENQUEUE_CMD(CmdSetPolygonModeEXT)
   ENQUEUE_CMD(CmdSetTessellationDomainOriginEXT)
   ENQUEUE_CMD(CmdSetDepthClampEnableEXT)
   ENQUEUE_CMD(CmdSetDepthClipEnableEXT)
   ENQUEUE_CMD(CmdSetLogicOpEnableEXT)
   ENQUEUE_CMD(CmdSetSampleMaskEXT)
   ENQUEUE_CMD(CmdSetRasterizationSamplesEXT)
   ENQUEUE_CMD(CmdSetAlphaToCoverageEnableEXT)
   ENQUEUE_CMD(CmdSetAlphaToOneEnableEXT)
   ENQUEUE_CMD(CmdSetDepthClipNegativeOneToOneEXT)
   ENQUEUE_CMD(CmdSetLineRasterizationModeEXT)
   ENQUEUE_CMD(CmdSetLineStippleEnableEXT)
   ENQUEUE_CMD(CmdSetProvokingVertexModeEXT)
   ENQUEUE_CMD(CmdSetColorBlendEnableEXT)
   ENQUEUE_CMD(CmdSetColorBlendEquationEXT)
   ENQUEUE_CMD(CmdSetColorWriteMaskEXT)

   ENQUEUE_CMD(CmdBindShadersEXT)
   /* required for EXT_shader_object */
   ENQUEUE_CMD(CmdSetCoverageModulationModeNV)
   ENQUEUE_CMD(CmdSetCoverageModulationTableEnableNV)
   ENQUEUE_CMD(CmdSetCoverageModulationTableNV)
   ENQUEUE_CMD(CmdSetCoverageReductionModeNV)
   ENQUEUE_CMD(CmdSetCoverageToColorEnableNV)
   ENQUEUE_CMD(CmdSetCoverageToColorLocationNV)
   ENQUEUE_CMD(CmdSetRepresentativeFragmentTestEnableNV)
   ENQUEUE_CMD(CmdSetShadingRateImageEnableNV)
   ENQUEUE_CMD(CmdSetViewportSwizzleNV)
   ENQUEUE_CMD(CmdSetViewportWScalingEnableNV)

#undef ENQUEUE_CMD
}

static void rvgpu_execute_cmd_buffer(struct rvgpu_cmd_buffer *cmd_buffer,
                                     struct rendering_state *state, bool print_cmds)
{
   struct vk_cmd_queue_entry *cmd;
   bool first = true;
   bool did_flush = false;

   LIST_FOR_EACH_ENTRY(cmd, &cmd_buffer->vk.cmd_queue.cmds, cmd_link) {
      if (print_cmds)
         fprintf(stderr, "%s\n", vk_cmd_queue_type_names[cmd->type]);
      switch (cmd->type) {
      case VK_CMD_BIND_PIPELINE:
         // // handle_pipeline(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT:
         // // handle_set_viewport(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT_WITH_COUNT:
         // // handle_set_viewport_with_count(cmd, state);
         break;
      case VK_CMD_SET_SCISSOR:
         // // handle_set_scissor(cmd, state);
         break;
      case VK_CMD_SET_SCISSOR_WITH_COUNT:
         // // handle_set_scissor_with_count(cmd, state);
         break;
      case VK_CMD_SET_LINE_WIDTH:
         // // handle_set_line_width(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BIAS:
         // handle_set_depth_bias(cmd, state);
         break;
      case VK_CMD_SET_BLEND_CONSTANTS:
         // handle_set_blend_constants(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BOUNDS:
         // handle_set_depth_bounds(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_COMPARE_MASK:
         // handle_set_stencil_compare_mask(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_WRITE_MASK:
         // handle_set_stencil_write_mask(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_REFERENCE:
         // handle_set_stencil_reference(cmd, state);
         break;
      case VK_CMD_BIND_DESCRIPTOR_SETS:
         // handle_descriptor_sets(cmd, state);
         break;
      case VK_CMD_BIND_INDEX_BUFFER:
         // handle_index_buffer(cmd, state);
         break;
      case VK_CMD_BIND_VERTEX_BUFFERS2:
         // handle_vertex_buffers2(cmd, state);
         break;
      case VK_CMD_DRAW:
         // emit_state(state);
         // handle_draw(cmd, state);
         break;
      case VK_CMD_DRAW_MULTI_EXT:
         // emit_state(state);
         // handle_draw_multi(cmd, state);
         break;
      case VK_CMD_DRAW_INDEXED:
         // emit_state(state);
         // handle_draw_indexed(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT:
         // emit_state(state);
         // handle_draw_indirect(cmd, state, false);
         break;
      case VK_CMD_DRAW_INDEXED_INDIRECT:
         // emit_state(state);
         // handle_draw_indirect(cmd, state, true);
         break;
      case VK_CMD_DRAW_MULTI_INDEXED_EXT:
         // emit_state(state);
         // handle_draw_multi_indexed(cmd, state);
         break;
      case VK_CMD_DISPATCH:
         // emit_compute_state(state);
         // handle_dispatch(cmd, state);
         break;
      case VK_CMD_DISPATCH_BASE:
         // emit_compute_state(state);
         // handle_dispatch_base(cmd, state);
         break;
      case VK_CMD_DISPATCH_INDIRECT:
         // emit_compute_state(state);
         // handle_dispatch_indirect(cmd, state);
         break;
      case VK_CMD_COPY_BUFFER2:
         // handle_copy_buffer(cmd, state);
         break;
      case VK_CMD_COPY_IMAGE2:
         // handle_copy_image(cmd, state);
         break;
      case VK_CMD_BLIT_IMAGE2:
         // handle_blit_image(cmd, state);
         break;
      case VK_CMD_COPY_BUFFER_TO_IMAGE2:
         // handle_copy_buffer_to_image(cmd, state);
         break;
      case VK_CMD_COPY_IMAGE_TO_BUFFER2:
         // handle_copy_image_to_buffer2(cmd, state);
         break;
      case VK_CMD_UPDATE_BUFFER:
         // handle_update_buffer(cmd, state);
         break;
      case VK_CMD_FILL_BUFFER:
         // handle_fill_buffer(cmd, state);
         break;
      case VK_CMD_CLEAR_COLOR_IMAGE:
         // handle_clear_color_image(cmd, state);
         break;
      case VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE:
         // handle_clear_ds_image(cmd, state);
         break;
      case VK_CMD_CLEAR_ATTACHMENTS:
         // handle_clear_attachments(cmd, state);
         break;
      case VK_CMD_RESOLVE_IMAGE2:
         // handle_resolve_image(cmd, state);
         break;
      case VK_CMD_PIPELINE_BARRIER2:
         /* skip flushes since every cmdbuf does a flush
            after iterating its cmds and so this is redundant
          */
         if (first || did_flush || cmd->cmd_link.next == &cmd_buffer->vk.cmd_queue.cmds)
            continue;
         // handle_pipeline_barrier(cmd, state);
         did_flush = true;
         continue;
      case VK_CMD_BEGIN_QUERY_INDEXED_EXT:
         // handle_begin_query_indexed_ext(cmd, state);
         break;
      case VK_CMD_END_QUERY_INDEXED_EXT:
         // handle_end_query_indexed_ext(cmd, state);
         break;
      case VK_CMD_BEGIN_QUERY:
         // handle_begin_query(cmd, state);
         break;
      case VK_CMD_END_QUERY:
         // handle_end_query(cmd, state);
         break;
      case VK_CMD_RESET_QUERY_POOL:
         // handle_reset_query_pool(cmd, state);
         break;
      case VK_CMD_COPY_QUERY_POOL_RESULTS:
         // handle_copy_query_pool_results(cmd, state);
         break;
      case VK_CMD_PUSH_CONSTANTS:
         // handle_push_constants(cmd, state);
         break;
      case VK_CMD_EXECUTE_COMMANDS:
         // handle_execute_commands(cmd, state, print_cmds);
         break;
      case VK_CMD_DRAW_INDIRECT_COUNT:
         // emit_state(state);
         // handle_draw_indirect_count(cmd, state, false);
         break;
      case VK_CMD_DRAW_INDEXED_INDIRECT_COUNT:
         // emit_state(state);
         // handle_draw_indirect_count(cmd, state, true);
         break;
      case VK_CMD_PUSH_DESCRIPTOR_SET_KHR:
         // handle_push_descriptor_set(cmd, state);
         break;
      case VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_KHR:
         // handle_push_descriptor_set_with_template(cmd, state);
         break;
      case VK_CMD_BIND_TRANSFORM_FEEDBACK_BUFFERS_EXT:
         // handle_bind_transform_feedback_buffers(cmd, state);
         break;
      case VK_CMD_BEGIN_TRANSFORM_FEEDBACK_EXT:
         // handle_begin_transform_feedback(cmd, state);
         break;
      case VK_CMD_END_TRANSFORM_FEEDBACK_EXT:
         // handle_end_transform_feedback(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT_BYTE_COUNT_EXT:
         // emit_state(state);
         // handle_draw_indirect_byte_count(cmd, state);
         break;
      case VK_CMD_BEGIN_CONDITIONAL_RENDERING_EXT:
         // handle_begin_conditional_rendering(cmd, state);
         break;
      case VK_CMD_END_CONDITIONAL_RENDERING_EXT:
         // handle_end_conditional_rendering(state);
         break;
      case VK_CMD_SET_VERTEX_INPUT_EXT:
         // handle_set_vertex_input(cmd, state);
         break;
      case VK_CMD_SET_CULL_MODE:
         // handle_set_cull_mode(cmd, state);
         break;
      case VK_CMD_SET_FRONT_FACE:
         // handle_set_front_face(cmd, state);
         break;
      case VK_CMD_SET_PRIMITIVE_TOPOLOGY:
         // handle_set_primitive_topology(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_TEST_ENABLE:
         // handle_set_depth_test_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_WRITE_ENABLE:
         // handle_set_depth_write_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_COMPARE_OP:
         // handle_set_depth_compare_op(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE:
         // handle_set_depth_bounds_test_enable(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_TEST_ENABLE:
         // handle_set_stencil_test_enable(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_OP:
         // handle_set_stencil_op(cmd, state);
         break;
      case VK_CMD_SET_LINE_STIPPLE_EXT:
         // handle_set_line_stipple(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BIAS_ENABLE:
         // handle_set_depth_bias_enable(cmd, state);
         break;
      case VK_CMD_SET_LOGIC_OP_EXT:
         // handle_set_logic_op(cmd, state);
         break;
      case VK_CMD_SET_PATCH_CONTROL_POINTS_EXT:
         // handle_set_patch_control_points(cmd, state);
         break;
      case VK_CMD_SET_PRIMITIVE_RESTART_ENABLE:
         // handle_set_primitive_restart_enable(cmd, state);
         break;
      case VK_CMD_SET_RASTERIZER_DISCARD_ENABLE:
         // handle_set_rasterizer_discard_enable(cmd, state);
         break;
      case VK_CMD_SET_COLOR_WRITE_ENABLE_EXT:
         // handle_set_color_write_enable(cmd, state);
         break;
      case VK_CMD_BEGIN_RENDERING:
         // handle_begin_rendering(cmd, state);
         break;
      case VK_CMD_END_RENDERING:
         // handle_end_rendering(cmd, state);
         break;
      case VK_CMD_SET_DEVICE_MASK:
         /* no-op */
         break;
      case VK_CMD_RESET_EVENT2:
         // handle_event_reset2(cmd, state);
         break;
      case VK_CMD_SET_EVENT2:
         // handle_event_set2(cmd, state);
         break;
      case VK_CMD_WAIT_EVENTS2:
         // handle_wait_events2(cmd, state);
         break;
      case VK_CMD_WRITE_TIMESTAMP2:
         // handle_write_timestamp2(cmd, state);
         break;

      case VK_CMD_SET_POLYGON_MODE_EXT:
         // handle_set_polygon_mode(cmd, state);
         break;
      case VK_CMD_SET_TESSELLATION_DOMAIN_ORIGIN_EXT:
         // handle_set_tessellation_domain_origin(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_CLAMP_ENABLE_EXT:
         // handle_set_depth_clamp_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_CLIP_ENABLE_EXT:
         // handle_set_depth_clip_enable(cmd, state);
         break;
      case VK_CMD_SET_LOGIC_OP_ENABLE_EXT:
         // handle_set_logic_op_enable(cmd, state);
         break;
      case VK_CMD_SET_SAMPLE_MASK_EXT:
         // handle_set_sample_mask(cmd, state);
         break;
      case VK_CMD_SET_RASTERIZATION_SAMPLES_EXT:
         // handle_set_samples(cmd, state);
         break;
      case VK_CMD_SET_ALPHA_TO_COVERAGE_ENABLE_EXT:
         // handle_set_alpha_to_coverage(cmd, state);
         break;
      case VK_CMD_SET_ALPHA_TO_ONE_ENABLE_EXT:
         // handle_set_alpha_to_one(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT:
         // handle_set_halfz(cmd, state);
         break;
      case VK_CMD_SET_LINE_RASTERIZATION_MODE_EXT:
         // handle_set_line_rasterization_mode(cmd, state);
         break;
      case VK_CMD_SET_LINE_STIPPLE_ENABLE_EXT:
         // handle_set_line_stipple_enable(cmd, state);
         break;
      case VK_CMD_SET_PROVOKING_VERTEX_MODE_EXT:
         // handle_set_provoking_vertex_mode(cmd, state);
         break;
      case VK_CMD_SET_COLOR_BLEND_ENABLE_EXT:
         // handle_set_color_blend_enable(cmd, state);
         break;
      case VK_CMD_SET_COLOR_WRITE_MASK_EXT:
         // handle_set_color_write_mask(cmd, state);
         break;
      case VK_CMD_SET_COLOR_BLEND_EQUATION_EXT:
         // handle_set_color_blend_equation(cmd, state);
         break;
      case VK_CMD_BIND_SHADERS_EXT:
         // handle_shaders(cmd, state);
         break;

      default:
         fprintf(stderr, "Unsupported command %s\n", vk_cmd_queue_type_names[cmd->type]);
         unreachable("Unsupported command");
         break;
      }
      first = false;
      did_flush = false;
   }
}

