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

#include <fcntl.h>
#include <sys/sysmacros.h>

#include "vk_util.h"
#include "vk_log.h"

#include "rvgpu_private.h"
#include "rvgpu_constants.h"

static void
rvgpu_physical_device_get_supported_extensions(const struct rvgpu_physical_device *device,
                                               struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table) {
      .KHR_swapchain = true,
   };
}

static void
rvgpu_physical_device_init_mem_types(struct rvgpu_physical_device *device)
{
   /* Setup available memory heaps and types */
   device->memory_properties.memoryHeapCount = 1;
   device->memory_properties.memoryHeaps[0].size = 100 * 1024 * 1023; // 100M
   device->memory_properties.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   device->memory_properties.memoryTypeCount = 1;
   device->memory_properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |  
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   device->memory_properties.memoryTypes[0].heapIndex = 0;
}

static VkResult
rvgpu_physical_device_try_create(struct rvgpu_instance *instance, drmDevicePtr drm_device,
                                struct rvgpu_physical_device **device_out)
{
   VkResult result;
   int fd = -1;

   if (drm_device) {
      const char *path = drm_device->nodes[DRM_NODE_RENDER];
      drmVersionPtr version;

      fd = drmOpen(path, NULL);
      if (fd < 0) {
         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER, "Could not open device %s: %m",
                          path);
      }

      version = drmGetVersion(fd);
      if (!version) {
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Could not get the kernel driver version for device %s: %m", path);
      }

      if (strcmp(version->name, "rvgpu cmodel")) {
         drmFreeVersion(version);
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Device '%s' is not using the AMDGPU kernel driver: %m", path);
      }
      drmFreeVersion(version);
   }

   struct rvgpu_physical_device *device = vk_zalloc2(&instance->vk.alloc, NULL, sizeof(*device), 8,
                                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_fd;
   }

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &rvgpu_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk, NULL, &dispatch_table);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   device->instance = instance;

   if (drm_device) {
      device->ws = rvgpu_winsys_create(fd, instance->debug_flags, instance->perftest_flags);
   } else {
      printf("null drm device\n");
   }

   if (!device->ws) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to initialize winsys");
      goto fail_base;
   }

   device->vk.supported_sync_types = device->ws->ops.get_sync_types(device->ws);

   rvgpu_physical_device_init_mem_types(device);

   rvgpu_physical_device_get_supported_extensions(device, &device->vk.supported_extensions);

   result = rvgpu_wsi_init(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_base;
   }

   *device_out = device;
   return VK_SUCCESS;

fail_base:
   vk_physical_device_finish(&device->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, device);
fail_fd:
   if (fd != -1)
      close(fd);
   return result;
}


static void
rvgpu_get_physical_device_properties_1_1(struct rvgpu_physical_device *pdevice,
                                         VkPhysicalDeviceVulkan11Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);

   // memcpy(p->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   // memcpy(p->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   /* The LUID is for Windows. */
   p->deviceLUIDValid = false;
   p->deviceNodeMask = 0;

   p->subgroupSize = RVGPU_SUBGROUP_SIZE;
   p->subgroupSupportedStages = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;

   p->subgroupSupportedOperations =
      VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
      VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
      VK_SUBGROUP_FEATURE_CLUSTERED_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT |
      VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
   p->subgroupQuadOperationsInAllStages = true;

   p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
   p->maxMultiviewViewCount = MAX_VIEWS;
   p->maxMultiviewInstanceIndex = INT_MAX;
   p->protectedNoFault = false;
   p->maxPerSetDescriptors = RVGPU_MAX_PER_SET_DESCRIPTORS;
   p->maxMemoryAllocationSize = RVGPU_MAX_MEMORY_ALLOCATION_SIZE;
}

VkResult
create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device,
                           struct vk_physical_device **out)
{
   if (!(device->available_nodes & (1 << DRM_NODE_RENDER)) || device->bustype != DRM_BUS_PCI ||
       device->deviceinfo.pci->vendor_id != SIETIUM_VENDOR_ID)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   return rvgpu_physical_device_try_create((struct rvgpu_instance *)vk_instance, device,
                                          (struct rvgpu_physical_device **)out);
}

void
rvgpu_physical_device_destroy(struct vk_physical_device *vk_device)
{
   struct rvgpu_physical_device *device = container_of(vk_device, struct rvgpu_physical_device, vk);

   rvgpu_finish_wsi(device);
   rvgpu_winsys_destroy(device->ws);
   vk_physical_device_finish(&device->vk);
   vk_free(&device->instance->vk.alloc, device);
}

static void
rvgpu_get_physical_device_queue_family_properties(struct rvgpu_physical_device *pdevice,
                                                  uint32_t *pCount,
                                                  VkQueueFamilyProperties **pQueueFamilyProperties)
{
   int num_queue_families = 1;
   int idx;

   if (pQueueFamilyProperties == NULL) {
      *pCount = num_queue_families;
      return;
   }

   if (!*pCount) {
      return;
   }

   idx = 0;
   if (*pCount >= 1) {
      *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
         .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT |
                       VK_QUEUE_SPARSE_BINDING_BIT,
         .queueCount = 1,
         .timestampValidBits = 64,
         .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
      };
      idx++;
   }

   *pCount = idx;

   RVGPU_UNUSED_VARIABLE(pdevice);
}

static size_t
rvgpu_max_descriptor_set_size()
{
   /* make sure that the entire descriptor set is addressable with a signed
    * 32-bit int. So the sum of all limits scaled by descriptor size has to
    * be at most 2 GiB. the combined image & samples object count as one of
    * both. This limit is for the pipeline layout, not for the set layout, but
    * there is no set limit, so we just set a pipeline limit. I don't think
    * any app is going to hit this soon. */
   return ((1ull << 31) - 16 * MAX_DYNAMIC_BUFFERS -
           MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_INLINE_UNIFORM_BLOCK_COUNT) /
          (32 /* uniform buffer, 32 due to potential space wasted on alignment */ +
           32 /* storage buffer, 32 due to potential space wasted on alignment */ +
           32 /* sampler, largest when combined with image */ + 64 /* sampled image */ +
           64 /* storage image */);
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                              VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);
   if (!pQueueFamilyProperties) {
      rvgpu_get_physical_device_queue_family_properties(pdevice, pCount, NULL);  
      return ;
   }

   VkQueueFamilyProperties *properties[] = {
      &pQueueFamilyProperties[0].queueFamilyProperties,
      &pQueueFamilyProperties[1].queueFamilyProperties,
      &pQueueFamilyProperties[2].queueFamilyProperties,
   };

   rvgpu_get_physical_device_queue_family_properties(pdevice, pCount, properties);
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                   VkPhysicalDeviceProperties2 *pProperties)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);
   rvgpu_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   VkPhysicalDeviceVulkan11Properties core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
   };
   rvgpu_get_physical_device_properties_1_1(pdevice, &core_1_1);

   vk_foreach_struct(ext, pProperties->pNext)
   {
      if (vk_get_physical_device_core_1_1_property_ext(ext, &core_1_1))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *)ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT: {
         VkPhysicalDeviceDiscardRectanglePropertiesEXT *properties =
            (VkPhysicalDeviceDiscardRectanglePropertiesEXT *)ext;
         properties->maxDiscardRectangles = MAX_DISCARD_RECTANGLES;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
         VkPhysicalDeviceExternalMemoryHostPropertiesEXT *properties =
            (VkPhysicalDeviceExternalMemoryHostPropertiesEXT *)ext;
         properties->minImportedHostPointerAlignment = 4096;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD: {
         VkPhysicalDeviceShaderCorePropertiesAMD *properties =
            (VkPhysicalDeviceShaderCorePropertiesAMD *)ext;

         /* Shader engines. */
         properties->shaderEngineCount = 1;
         properties->shaderArraysPerEngineCount = 1;
         properties->computeUnitsPerShaderArray = 1;
         properties->simdPerComputeUnit = 1;
         properties->wavefrontsPerSimd = 1;
         properties->wavefrontSize = 64;

         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD: {
         VkPhysicalDeviceShaderCoreProperties2AMD *properties =
            (VkPhysicalDeviceShaderCoreProperties2AMD *)ext;

         properties->shaderCoreFeatures = 0;
         properties->activeComputeUnitCount = 1;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *properties =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         properties->maxVertexAttribDivisor = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceConservativeRasterizationPropertiesEXT *properties =
            (VkPhysicalDeviceConservativeRasterizationPropertiesEXT *)ext;
         properties->primitiveOverestimationSize = 0;
         properties->maxExtraPrimitiveOverestimationSize = 0;
         properties->extraPrimitiveOverestimationSizeGranularity = 0;
         properties->primitiveUnderestimation = true;
         properties->conservativePointAndLineRasterization = false;
         properties->degenerateTrianglesRasterized = true;
         properties->degenerateLinesRasterized = false;
         properties->fullyCoveredFragmentShaderInputVariable = true;
         properties->conservativeRasterizationPostDepthCoverage = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: {
         // VkPhysicalDevicePCIBusInfoPropertiesEXT *properties =
         //    (VkPhysicalDevicePCIBusInfoPropertiesEXT *)ext;
         // properties->pciDomain = pdevice->bus_info.domain;
         // properties->pciBus = pdevice->bus_info.bus;
         // properties->pciDevice = pdevice->bus_info.dev;
         // properties->pciFunction = pdevice->bus_info.func;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
         VkPhysicalDeviceTransformFeedbackPropertiesEXT *properties =
            (VkPhysicalDeviceTransformFeedbackPropertiesEXT *)ext;
         properties->maxTransformFeedbackStreams = MAX_SO_STREAMS;
         properties->maxTransformFeedbackBuffers = MAX_SO_BUFFERS;
         properties->maxTransformFeedbackBufferSize = UINT32_MAX;
         properties->maxTransformFeedbackStreamDataSize = 512;
         properties->maxTransformFeedbackBufferDataSize = 512;
         properties->maxTransformFeedbackBufferDataStride = 512;
         properties->transformFeedbackQueries = true;
         properties->transformFeedbackStreamsLinesTriangles = true;
         properties->transformFeedbackRasterizationStreamSelect = false;
         properties->transformFeedbackDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT: {
         VkPhysicalDeviceSampleLocationsPropertiesEXT *properties =
            (VkPhysicalDeviceSampleLocationsPropertiesEXT *)ext;
         properties->sampleLocationSampleCounts =
            VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
         properties->maxSampleLocationGridSize = (VkExtent2D){2, 2};
         properties->sampleLocationCoordinateRange[0] = 0.0f;
         properties->sampleLocationCoordinateRange[1] = 0.9375f;
         properties->sampleLocationSubPixelBits = 4;
         properties->variableSampleLocations = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceLineRasterizationPropertiesEXT *props =
            (VkPhysicalDeviceLineRasterizationPropertiesEXT *)ext;
         props->lineSubPixelPrecisionBits = 4;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
         VkPhysicalDeviceRobustness2PropertiesEXT *properties =
            (VkPhysicalDeviceRobustness2PropertiesEXT *)ext;
         properties->robustStorageBufferAccessSizeAlignment = 4;
         properties->robustUniformBufferAccessSizeAlignment = 4;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: {
         VkPhysicalDeviceCustomBorderColorPropertiesEXT *props =
            (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)ext;
         props->maxCustomBorderColorSamplers = 4096;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR: {
         VkPhysicalDeviceFragmentShadingRatePropertiesKHR *props =
            (VkPhysicalDeviceFragmentShadingRatePropertiesKHR *)ext;
         props->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D){0, 0};
         props->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D){0, 0};
         props->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 1;
         props->primitiveFragmentShadingRateWithMultipleViewports = true;
         props->layeredShadingRateAttachments = false; /* TODO */
         props->fragmentShadingRateNonTrivialCombinerOps = true;
         props->maxFragmentSize = (VkExtent2D){2, 2};
         props->maxFragmentSizeAspectRatio = 2;
         props->maxFragmentShadingRateCoverageSamples = 32;
         props->maxFragmentShadingRateRasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
         props->fragmentShadingRateWithShaderDepthStencilWrites = false;
         props->fragmentShadingRateWithSampleMask = true;
         props->fragmentShadingRateWithShaderSampleMask = false;
         props->fragmentShadingRateWithConservativeRasterization = true;
         props->fragmentShadingRateWithFragmentShaderInterlock = false;
         props->fragmentShadingRateWithCustomSampleLocations = false;
         props->fragmentShadingRateStrictMultiplyCombiner = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: {
         VkPhysicalDeviceProvokingVertexPropertiesEXT *props =
            (VkPhysicalDeviceProvokingVertexPropertiesEXT *)ext;
         props->provokingVertexModePerPipeline = true;
         props->transformFeedbackPreservesTriangleFanProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR: {
         VkPhysicalDeviceAccelerationStructurePropertiesKHR *props =
            (VkPhysicalDeviceAccelerationStructurePropertiesKHR *)ext;
         props->maxGeometryCount = (1 << 24) - 1;
         props->maxInstanceCount = (1 << 24) - 1;
         props->maxPrimitiveCount = (1 << 29) - 1;
         props->maxPerStageDescriptorAccelerationStructures =
            pProperties->properties.limits.maxPerStageDescriptorStorageBuffers;
         props->maxPerStageDescriptorUpdateAfterBindAccelerationStructures =
            pProperties->properties.limits.maxPerStageDescriptorStorageBuffers;
         props->maxDescriptorSetAccelerationStructures =
            pProperties->properties.limits.maxDescriptorSetStorageBuffers;
         props->maxDescriptorSetUpdateAfterBindAccelerationStructures =
            pProperties->properties.limits.maxDescriptorSetStorageBuffers;
         props->minAccelerationStructureScratchOffsetAlignment = 128;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT: {
         VkPhysicalDeviceDrmPropertiesEXT *props = (VkPhysicalDeviceDrmPropertiesEXT *)ext;
         if (pdevice->available_nodes & (1 << DRM_NODE_PRIMARY)) {
            props->hasPrimary = true;
            props->primaryMajor = (int64_t)major(pdevice->primary_devid);
            props->primaryMinor = (int64_t)minor(pdevice->primary_devid);
         } else {
            props->hasPrimary = false;
         }
         if (pdevice->available_nodes & (1 << DRM_NODE_RENDER)) {
            props->hasRender = true;
            props->renderMajor = (int64_t)major(pdevice->render_devid);
            props->renderMinor = (int64_t)minor(pdevice->render_devid);
         } else {
            props->hasRender = false;
         }
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT: {
         VkPhysicalDeviceMultiDrawPropertiesEXT *props =
            (VkPhysicalDeviceMultiDrawPropertiesEXT *)ext;
         props->maxMultiDrawCount = 2048;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR: {
         VkPhysicalDeviceRayTracingPipelinePropertiesKHR *props =
            (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)ext;
         props->shaderGroupHandleSize = RVGPU_RT_HANDLE_SIZE;
         props->maxRayRecursionDepth = 31;    /* Minimum allowed for DXR. */
         props->maxShaderGroupStride = 16384; /* dummy */
         /* This isn't strictly necessary, but Doom Eternal breaks if the
          * alignment is any lower. */
         props->shaderGroupBaseAlignment = RVGPU_RT_HANDLE_SIZE;
         props->shaderGroupHandleCaptureReplaySize = RVGPU_RT_HANDLE_SIZE;
         props->maxRayDispatchInvocationCount = 1024 * 1024 * 64;
         props->shaderGroupHandleAlignment = 16;
         props->maxRayHitAttributeSize = RVGPU_MAX_HIT_ATTRIB_SIZE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: {
         VkPhysicalDeviceMaintenance4Properties *properties =
            (VkPhysicalDeviceMaintenance4Properties *)ext;
         properties->maxBufferSize = RVGPU_MAX_MEMORY_ALLOCATION_SIZE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT: {
         // VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *properties =
         //   (VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *)ext;
         // STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
         //              sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
         //memcpy(properties->shaderModuleIdentifierAlgorithmUUID,
         //       vk_shaderModuleIdentifierAlgorithmUUID,
         //       sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR: {
         VkPhysicalDevicePerformanceQueryPropertiesKHR *properties =
            (VkPhysicalDevicePerformanceQueryPropertiesKHR *)ext;
         properties->allowCommandBufferQueryCopies = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV: {
         VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *properties =
            (VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *)ext;
         properties->maxIndirectCommandsStreamCount = 1;
         properties->maxIndirectCommandsStreamStride = UINT32_MAX;
         properties->maxIndirectCommandsTokenCount = UINT32_MAX;
         properties->maxIndirectCommandsTokenOffset = UINT16_MAX;
         properties->minIndirectCommandsBufferOffsetAlignment = 4;
         properties->minSequencesCountBufferOffsetAlignment = 4;
         properties->minSequencesIndexBufferOffsetAlignment = 4;

         /* Don't support even a shader group count = 1 until we support shader
          * overrides during pipeline creation. */
         properties->maxGraphicsShaderGroupCount = 0;

         properties->maxIndirectSequenceCount = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT: {
         VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *props =
            (VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *)ext;
         props->graphicsPipelineLibraryFastLinking = true;
         props->graphicsPipelineLibraryIndependentInterpolationDecoration = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
         VkPhysicalDeviceMeshShaderPropertiesEXT *properties =
            (VkPhysicalDeviceMeshShaderPropertiesEXT *)ext;

         properties->maxTaskWorkGroupTotalCount = 4194304; /* 2^22 min required */
         properties->maxTaskWorkGroupCount[0] = 65535;
         properties->maxTaskWorkGroupCount[1] = 65535;
         properties->maxTaskWorkGroupCount[2] = 65535;
         properties->maxTaskWorkGroupInvocations = 1024;
         properties->maxTaskWorkGroupSize[0] = 1024;
         properties->maxTaskWorkGroupSize[1] = 1024;
         properties->maxTaskWorkGroupSize[2] = 1024;
         properties->maxTaskPayloadSize = 16384; /* 16K min required */
         properties->maxTaskSharedMemorySize = 65536;
         properties->maxTaskPayloadAndSharedMemorySize = 65536;

         properties->maxMeshWorkGroupTotalCount = 4194304; /* 2^22 min required */
         properties->maxMeshWorkGroupCount[0] = 65535;
         properties->maxMeshWorkGroupCount[1] = 65535;
         properties->maxMeshWorkGroupCount[2] = 65535;
         properties->maxMeshWorkGroupInvocations = 256; /* Max NGG HW limit */
         properties->maxMeshWorkGroupSize[0] = 256;
         properties->maxMeshWorkGroupSize[1] = 256;
         properties->maxMeshWorkGroupSize[2] = 256;
         properties->maxMeshOutputMemorySize = 32 * 1024; /* 32K min required */
         properties->maxMeshSharedMemorySize = 28672;     /* 28K min required */
         properties->maxMeshPayloadAndSharedMemorySize =
            properties->maxTaskPayloadSize +
            properties->maxMeshSharedMemorySize; /* 28K min required */
         properties->maxMeshPayloadAndOutputMemorySize =
            properties->maxTaskPayloadSize +
            properties->maxMeshOutputMemorySize;    /* 47K min required */
         properties->maxMeshOutputComponents = 128; /* 32x vec4 min required */
         properties->maxMeshOutputVertices = 256;
         properties->maxMeshOutputPrimitives = 256;
         properties->maxMeshOutputLayers = 8;
         properties->maxMeshMultiviewViewCount = MAX_VIEWS;
         properties->meshOutputPerVertexGranularity = 1;
         properties->meshOutputPerPrimitiveGranularity = 1;

         properties->maxPreferredTaskWorkGroupInvocations = 64;
         properties->maxPreferredMeshWorkGroupInvocations = 128;
         properties->prefersLocalInvocationVertexOutput = true;
         properties->prefersLocalInvocationPrimitiveOutput = true;
         properties->prefersCompactVertexOutput = true;
         properties->prefersCompactPrimitiveOutput = false;

         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT: {
         VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *properties =
            (VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *)ext;
         properties->dynamicPrimitiveTopologyUnrestricted = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT: {
         VkPhysicalDeviceDescriptorBufferPropertiesEXT *properties =
            (VkPhysicalDeviceDescriptorBufferPropertiesEXT *)ext;
         properties->combinedImageSamplerDescriptorSingleArray = true;
         properties->bufferlessPushDescriptors = true;
         properties->allowSamplerImageViewPostSubmitCreation = false;
         properties->descriptorBufferOffsetAlignment = 4;
         properties->maxDescriptorBufferBindings = MAX_SETS;
         properties->maxResourceDescriptorBufferBindings = MAX_SETS;
         properties->maxSamplerDescriptorBufferBindings = MAX_SETS;
         properties->maxEmbeddedImmutableSamplerBindings = MAX_SETS;
         properties->maxEmbeddedImmutableSamplers = rvgpu_max_descriptor_set_size();
         properties->bufferCaptureReplayDescriptorDataSize = 0;
         properties->imageCaptureReplayDescriptorDataSize = 0;
         properties->imageViewCaptureReplayDescriptorDataSize = 0;
         properties->samplerCaptureReplayDescriptorDataSize = 0;
         properties->accelerationStructureCaptureReplayDescriptorDataSize = 0;
         properties->samplerDescriptorSize = 16;
         properties->combinedImageSamplerDescriptorSize = 96;
         properties->sampledImageDescriptorSize = 64;
         properties->storageImageDescriptorSize = 32;
         properties->uniformTexelBufferDescriptorSize = 16;
         properties->robustUniformTexelBufferDescriptorSize = 16;
         properties->storageTexelBufferDescriptorSize = 16;
         properties->robustStorageTexelBufferDescriptorSize = 16;
         properties->uniformBufferDescriptorSize = 16;
         properties->robustUniformBufferDescriptorSize = 16;
         properties->storageBufferDescriptorSize = 16;
         properties->robustStorageBufferDescriptorSize = 16;
         properties->inputAttachmentDescriptorSize = 64;
         properties->accelerationStructureDescriptorSize = 16;
         properties->maxSamplerDescriptorBufferRange = UINT32_MAX;
         properties->maxResourceDescriptorBufferRange = UINT32_MAX;
         properties->samplerDescriptorBufferAddressSpaceSize = RVGPU_MAX_MEMORY_ALLOCATION_SIZE;
         properties->resourceDescriptorBufferAddressSpaceSize = RVGPU_MAX_MEMORY_ALLOCATION_SIZE;
         properties->descriptorBufferAddressSpaceSize = RVGPU_MAX_MEMORY_ALLOCATION_SIZE;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties *pProperties)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);
   VkSampleCountFlags sample_counts = 0xf;
   
   size_t max_descriptor_set_size = rvgpu_max_descriptor_set_size();
   
   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D = (1 << 14),
      .maxImageDimension2D = (1 << 14),
      .maxImageDimension3D = (1 << 11),
      .maxImageDimensionCube = (1 << 14),
      .maxImageArrayLayers = (1 << 11),
      .maxTexelBufferElements = UINT32_MAX,
      .maxUniformBufferRange = UINT32_MAX,
      .maxStorageBufferRange = UINT32_MAX,
      .maxPushConstantsSize = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount = UINT32_MAX,
      .maxSamplerAllocationCount = 64 * 1024,
      .bufferImageGranularity = 1,
      .sparseAddressSpaceSize = RVGPU_MAX_MEMORY_ALLOCATION_SIZE, /* buffer max size */
      .maxBoundDescriptorSets = MAX_SETS,
      .maxPerStageDescriptorSamplers = max_descriptor_set_size,
      .maxPerStageDescriptorUniformBuffers = max_descriptor_set_size,
      .maxPerStageDescriptorStorageBuffers = max_descriptor_set_size,
      .maxPerStageDescriptorSampledImages = max_descriptor_set_size,
      .maxPerStageDescriptorStorageImages = max_descriptor_set_size,
      .maxPerStageDescriptorInputAttachments = max_descriptor_set_size,
      .maxPerStageResources = max_descriptor_set_size,
      .maxDescriptorSetSamplers = max_descriptor_set_size,
      .maxDescriptorSetUniformBuffers = max_descriptor_set_size,
      .maxDescriptorSetUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetStorageBuffers = max_descriptor_set_size,
      .maxDescriptorSetStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetSampledImages = max_descriptor_set_size,
      .maxDescriptorSetStorageImages = max_descriptor_set_size,
      .maxDescriptorSetInputAttachments = max_descriptor_set_size,
      .maxVertexInputAttributes = MAX_VERTEX_ATTRIBS,
      .maxVertexInputBindings = MAX_VBS,
      .maxVertexInputAttributeOffset = UINT32_MAX,
      .maxVertexInputBindingStride = 2048,
      .maxVertexOutputComponents = 128,
      .maxTessellationGenerationLevel = 64,
      .maxTessellationPatchSize = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 120,
      .maxTessellationControlTotalOutputComponents = 4096,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations = 127,
      .maxGeometryInputComponents = 64,
      .maxGeometryOutputComponents = 128,
      .maxGeometryOutputVertices = 256,
      .maxGeometryTotalOutputComponents = 1024,
      .maxFragmentInputComponents = 128,
      .maxFragmentOutputAttachments = 8,
      .maxFragmentDualSrcAttachments = 1,
      .maxFragmentCombinedOutputResources = max_descriptor_set_size,
      .maxComputeSharedMemorySize = 32768, 
      .maxComputeWorkGroupCount = {65535, 65535, 65535},
      .maxComputeWorkGroupInvocations = 1024,
      .maxComputeWorkGroupSize = {1024, 1024, 1024},
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = UINT32_MAX,
      .maxSamplerLodBias = 16,
      .maxSamplerAnisotropy = 16,
      .maxViewports = MAX_VIEWPORTS,
      .maxViewportDimensions = {(1 << 14), (1 << 14)},
      .viewportBoundsRange = {INT16_MIN, INT16_MAX},
      .viewportSubPixelBits = 8,
      .minMemoryMapAlignment = 4096, /* A page */
      .minTexelBufferOffsetAlignment = 4,
      .minUniformBufferOffsetAlignment = 4,
      .minStorageBufferOffsetAlignment = 4,
      .minTexelOffset = -32,
      .maxTexelOffset = 31,
      .minTexelGatherOffset = -32,
      .maxTexelGatherOffset = 31,
      .minInterpolationOffset = -2,
      .maxInterpolationOffset = 2,
      .subPixelInterpolationOffsetBits = 8,
      .maxFramebufferWidth = MAX_FRAMEBUFFER_WIDTH,
      .maxFramebufferHeight = MAX_FRAMEBUFFER_HEIGHT,
      .maxFramebufferLayers = (1 << 10),
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .maxColorAttachments = MAX_RTS,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = sample_counts,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = true,
      .timestampPeriod = 1.0,
      .maxClipDistances = 8,
      .maxCullDistances = 8,
      .maxCombinedClipAndCullDistances = 8,
      .discreteQueuePriorities = 2,
      .pointSizeRange = {0.0, 8191.875},
      .lineWidthRange = {0.0, 8.0},
      .pointSizeGranularity = (1.0 / 8.0),
      .lineWidthGranularity = (1.0 / 8.0),
      .strictLines = false, /* FINISHME */
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 1,
      .optimalBufferCopyRowPitchAlignment = 1,
      .nonCoherentAtomSize = 64,
   };
   
   VkPhysicalDeviceType device_type;
   
   device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
   
   *pProperties = (VkPhysicalDeviceProperties){
      .apiVersion = RVGPU_API_VERSION,
      .driverVersion = vk_get_driver_version(),
      .vendorID = SIETIUM_VENDOR_ID,
      .deviceID = RVGPU_DEVICE_ID,
      .deviceType = device_type,
      .limits = limits,
      .sparseProperties =
         {
            .residencyNonResidentStrict = true,
            .residencyStandard2DBlockShape = true,
            .residencyStandard3DBlockShape = true,
         },
   };
   
   strcpy(pProperties->deviceName, pdevice->marketing_name);
   memcpy(pProperties->pipelineCacheUUID, pdevice->cache_uuid, VK_UUID_SIZE);
}


VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures)
{
   // RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);
   memset(pFeatures, 0, sizeof(*pFeatures));

   *pFeatures = (VkPhysicalDeviceFeatures){
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = true,
      .tessellationShader = true,
      .sampleRateShading = true,
      .dualSrcBlend = true,
      .logicOp = true,
      .multiDrawIndirect = true,
      .drawIndirectFirstInstance = true,
      .depthClamp = true,
      .depthBiasClamp = true,
      .fillModeNonSolid = true,
      .depthBounds = true,
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = false,
      .multiViewport = true,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = false,
      .textureCompressionASTC_LDR = false,
      .textureCompressionBC = true,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = true,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = true,
      .shaderImageGatherExtended = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = true,
      .shaderInt64 = true,
      .shaderInt16 = true,
      .sparseBinding = true,
      .sparseResidencyBuffer = false,
      .sparseResidencyImage2D = false,
      .sparseResidencyImage3D = false,
      .sparseResidencyAliased = false,
      .variableMultisampleRate = true,
      .shaderResourceMinLod = true,
      .shaderResourceResidency = true,
      .inheritedQueries = true,
   };
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceFeatures2 *pFeatures)
{
   // RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);
   rvgpu_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);
}

VKAPI_ATTR void VKAPI_CALL
rvgpu_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, 
                                         VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   RVGPU_FROM_HANDLE(rvgpu_physical_device, pdevice, physicalDevice);

   pMemoryProperties->memoryProperties = pdevice->memory_properties;

   vk_foreach_struct (ext, pMemoryProperties->pNext) {
      rvgpu_debug_ignored_stype(ext->sType);
   }
}

VkResult VKAPI_CALL
rvgpu_physical_device_init(struct rvgpu_physical_device *device, 
                           struct rvgpu_instance *instance)
{
    VkResult result;
    struct vk_physical_device_dispatch_table dispatch_table;
    vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table, &rvgpu_physical_device_entrypoints, true);
    vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table, &wsi_physical_device_entrypoints, false);

    result = vk_physical_device_init(&device->vk, &instance->vk,NULL, &dispatch_table);

    if (result != VK_SUCCESS) {
        vk_error(instance, result);
        goto fail_alloc;
    }

    device->instance = instance;
#if 0    
    device->ws = rvgpu_winsys_create(1, instance->debug_flags, instance->perftest_flags);    
    if (!device->ws) {
        vk_error(instance, result);
        goto fail_base;
    }

    device->vk.supported_sync_types = device->ws->ops.get_sync_types(device->ws);
#endif

    device->sync_timeline_type = vk_sync_timeline_get_type(&rvgpu_sync_type);
    device->sync_types[0] = &rvgpu_sync_type;
    device->sync_types[1] = &device->sync_timeline_type.sync;
    device->sync_types[2] = NULL;
    device->vk.supported_sync_types = device->sync_types;

    rvgpu_physical_device_init_mem_types(device);
    rvgpu_physical_device_get_supported_extensions(device, &device->vk.supported_extensions);
    
    result = rvgpu_wsi_init(device);

    if (result != VK_SUCCESS) { 
        vk_error(instance, result);
        goto fail_base;
    }

fail_base:
    vk_physical_device_finish(&device->vk);
fail_alloc:
    vk_free(&instance->vk.alloc, device);

    return VK_SUCCESS;
}

VkResult
rvgpu_enumerate_physical_devices(struct vk_instance *vk_instance) 
{
    struct rvgpu_instance *instance = container_of(vk_instance, struct rvgpu_instance, vk);
    
    struct rvgpu_physical_device *device = vk_zalloc2(&instance->vk.alloc, NULL, sizeof(*device), 8,
                                                      VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

    if (!device)
        return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
    
    VkResult result = rvgpu_physical_device_init(device, instance);

    if (result == VK_SUCCESS) 
        list_addtail(&device->vk.link, &instance->vk.physical_devices.list);
    else
        vk_free(&vk_instance->alloc, device);
   
    return result;
}

