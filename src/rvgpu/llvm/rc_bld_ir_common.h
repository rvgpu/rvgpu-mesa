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
#ifndef RVGPU_MESA_RC_BLD_IR_COMMON_H
#define RVGPU_MESA_RC_BLD_IR_COMMON_H
#include <llvm-c/Core.h>
#ifdef __cplusplus
extern "C" {
#endif

struct rc_exec_mask {
    struct rc_build_context *bld;
    LLVMValueRef exec_mask;
};

void rc_exec_mask_store(struct rc_exec_mask *mask,
                        struct rc_build_context *bld_store,
                        LLVMValueRef val,
                        LLVMValueRef dst_ptr);

void rc_exec_mask_init(struct rc_exec_mask *mask, struct rc_build_context *bld);

#ifdef __cplusplus
}
#endif
#endif //RVGPU_MESA_RC_BLD_IR_COMMON_H