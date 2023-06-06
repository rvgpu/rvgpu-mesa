#include "rvgpu_private.h"


static VkResult rvgpu_sync_init(struct vk_device *device,
                                  struct vk_sync *sync,
                                  uint64_t initial_value)
{
    return VK_SUCCESS;
}

static void rvgpu_sync_finish(struct vk_device *device, struct vk_sync *sync)
{
  
}


static VkResult rvgpu_sync_signal(struct vk_device *device,
                                    struct vk_sync *sync,
                                    UNUSED uint64_t value)
{
    return VK_SUCCESS;
}

static VkResult rvgpu_sync_reset(struct vk_device *device,
                                   struct vk_sync *sync)
{
    return VK_SUCCESS;
}

static VkResult rvgpu_sync_move(struct vk_device *device,
                                  struct vk_sync *dst,
                                  struct vk_sync *src)

{
    return VK_SUCCESS;
}

static VkResult
rvgpu_sync_wait(struct vk_device *vk_device,
                struct vk_sync *vk_sync,
                uint64_t wait_value,
                enum vk_sync_wait_flags wait_flags,
                uint64_t abs_timeout_ns)
{
    return VK_SUCCESS;
}

const struct vk_sync_type rvgpu_sync_type = {
   .size = sizeof(struct rvgpu_sync),
   .features = VK_SYNC_FEATURE_BINARY |
               VK_SYNC_FEATURE_GPU_WAIT |
               VK_SYNC_FEATURE_GPU_MULTI_WAIT |
               VK_SYNC_FEATURE_CPU_WAIT |
               VK_SYNC_FEATURE_CPU_RESET |
               VK_SYNC_FEATURE_CPU_SIGNAL |
               VK_SYNC_FEATURE_WAIT_PENDING,
   .init = rvgpu_sync_init,
   .finish = rvgpu_sync_finish,
   .signal = rvgpu_sync_signal,
   .reset = rvgpu_sync_reset,
   .move = rvgpu_sync_move,
   .wait = rvgpu_sync_wait,
};

