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
#ifndef RVGPU_MESA_RC_BUILD_TYPE_H
#define RVGPU_MESA_RC_BUILD_TYPE_H

#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif
struct rc_llvm_context;

struct rc_type {
    unsigned floating:1;
    unsigned fixed:1;
    unsigned sign:1;
    unsigned norm:1;
    unsigned width:14;
    unsigned length:14;
};

struct rc_build_context {
    struct rc_llvm_context *rc;
    struct rc_type type;
    LLVMTypeRef elem_type;
    LLVMTypeRef vec_type;
    LLVMTypeRef int_elem_type;
    LLVMTypeRef int_vec_type;
    LLVMValueRef undef;
    LLVMValueRef zero;
    LLVMValueRef one;
};

LLVMTypeRef rc_build_int_vec_type(struct rc_llvm_context *rc, struct rc_type type);
LLVMTypeRef rc_build_vec_type(struct rc_llvm_context *rc, struct rc_type type);
LLVMTypeRef rc_build_elem_type(struct rc_llvm_context *ctx, struct rc_type type);

LLVMValueRef rc_build_const_int_vec(struct rc_llvm_context *rc, struct rc_type type, long long val);
#ifdef __cplusplus
}
#endif

#endif //RVGPU_MESA_RC_BUILD_TYPE_H
