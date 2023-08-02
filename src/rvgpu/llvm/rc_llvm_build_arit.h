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
#ifndef RVGPU_MESA_RC_LLVM_BUILD_ARIT_H
#define RVGPU_MESA_RC_LLVM_BUILD_ARIT_H

#include <llvm-c/Types.h>
#include "rc_llvm_build.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Specifies floating point NaN behavior.
 */
enum nan_behavior {
    /* Results are undefined with NaN. Results in fastest code */
    UNDEFINED,
    /* If one of the inputs is NaN, the other operand is returned */
    RETURN_OTHER,
    /* If one of the inputs is NaN, the other operand is returned,
     * but we guarantee the second operand is not a NaN.
     * In min/max it will be as fast as undefined with sse opcodes,
     * and archs having native return_other can benefit too. */
    RETURN_OTHER_SECOND_NONNAN,
    /* If one of the inputs is NaN, NaN is returned,
     * but we guarantee the first operand is not a NaN.
     * In min/max it will be as fast as undefined with sse opcodes,
     * and archs having native return_nan can benefit too. */
    RETURN_NAN_FIRST_NONNAN,
};


enum rc_compare_func {
    NEVER,
    LESS,
    EQUAL,
    LEQUAL,
    GREATER,
    NOTEQUAL,
    GEQUAL,
    ALWAYS,
};

LLVMValueRef rc_build_add(struct rc_build_context *bld, LLVMValueRef a, LLVMValueRef b);
LLVMValueRef rc_build_mul(struct rc_build_context *bld, LLVMValueRef a, LLVMValueRef b);
LLVMValueRef rc_build_min(struct rc_build_context *bld, LLVMValueRef a, LLVMValueRef b);
LLVMValueRef rc_build_isnan(struct rc_build_context *bld, LLVMValueRef x);

LLVMValueRef rc_build_cmp(struct rc_build_context *bld,
                          enum rc_compare_func func,
                          LLVMValueRef a,
                          LLVMValueRef b);

LLVMValueRef rc_build_select(struct rc_build_context *bld,
                             LLVMValueRef mask,
                             LLVMValueRef a,
                             LLVMValueRef b);

LLVMValueRef rc_build_select_bitwise(struct rc_build_context *bld,
                                     LLVMValueRef mask,
                                     LLVMValueRef a,
                                     LLVMValueRef b);

LLVMValueRef rc_build_undef(struct rc_llvm_context *rc,  struct rc_type type);


#ifdef __cplusplus
}
#endif

#endif //RVGPU_MESA_RC_LLVM_BUILD_ARIT_H
