# Copyright © 2023 Sietium Inc.

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

dep_libdrm_rvgpu = dependency(
    'libdrm_rvgpu', version : '>=' + '1.0.0',
)
rvgpu_entrypoints = custom_target(
  'rvgpu_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['rvgpu_entrypoints.h', 'rvgpu_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'rvgpu',
  ],
  depend_files : vk_entrypoints_gen_depend_files,
)

rvgpu_files = files(
    'drm_ioctl.c',
    'rvgpu_device.c',
    'rvgpu_instance.c',
    'rvgpu_physical_device.c',
    'rvgpu_image.c',
    'rvgpu_buffer.c',
    'rvgpu_device_memory.c',
    'rvgpu_formats.c',
    'rvgpu_queue.c',
    'rvgpu_winsys.c',
    'rvgpu_winsys_bo.c',
    'rvgpu_cmd_buffer.c',
    'rvgpu_descriptor_set.c',
    'rvgpu_pipeline.c',
    'rvgpu_pipeline_graphics.c',
    'rvgpu_lower.c',
    'rvgpu_lower_vulkan_resource.c',
    'rvgpu_lower_inline_uniforms.c',
    'rvgpu_wsi.c',
    'rvgpu_execute.c',
    'rvgpu_sync.c',
    'rvgpu_shader.c',
    'rvgpu_nir_to_llvm.c',
    'rvgpu_llvm_helper.cpp',
)

rvgpu_flags = [
  no_override_init_args,
]

libvulkan_rvgpu = shared_library(
  'vulkan_rvgpu',
  [rvgpu_files, rvgpu_entrypoints],
  include_directories : [
    inc_gallium_aux,
    inc_include,
    inc_src,
    inc_mesa,
    inc_gallium,
    inc_compiler,
    inc_rvgpu_common_llvm,
  ],
  link_with : [
    librvgpu_common_llvm,
  ],
  dependencies : [
    dep_libdrm,
    dep_libdrm_rvgpu,
    dep_llvm,
    idep_nir,
    idep_vulkan_util,
    idep_vulkan_runtime,
    idep_vulkan_wsi,
    idep_mesautil,
  ],
  c_args : rvgpu_flags,
  link_args : [
    ld_args_build_id,
    ld_args_bsymbolic,
    ld_args_gc_sections
  ],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

rvgpu_icd = custom_target(
  'rvgpu_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : 'rvgpu_icd.@0@.json'.format(host_machine.cpu()),
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.0', '--xml', '@INPUT1@',
    '--lib-path', join_paths(get_option('prefix'), get_option('libdir'),
                             'libvulkan_rvgpu.so'),
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
  install_dir : with_vulkan_icd_dir,
  install : true,
)

_dev_icdname = 'rvgpu_devenv_icd.@0@.json'.format(host_machine.cpu())
_dev_icd = custom_target(
  'rvgpu_devenv_icd',
  input : [vk_icd_gen, vk_api_xml],
  output : _dev_icdname,
  command : [
    prog_python, '@INPUT0@',
    '--api-version', '1.0', '--xml', '@INPUT1@',
    '--lib-path', meson.current_build_dir() / 'libvulkan_rvgpu.so',
    '--out', '@OUTPUT@',
  ],
  build_by_default : true,
)

devenv.append('VK_ICD_FILENAMES', _dev_icd.full_path())
