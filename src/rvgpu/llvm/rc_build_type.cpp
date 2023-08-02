#include <llvm-c/Core.h>
#include <cassert>
#include "rc_build_type.h"
#include "rc_llvm_build.h"

LLVMTypeRef rc_build_int_vec_type(struct rc_llvm_context *rc, struct rc_type type) {
    LLVMTypeRef elem_type = LLVMIntTypeInContext(rc->context, type.width);
    if (type.length == 1)
        return elem_type;
    else
        return LLVMVectorType(elem_type, type.length);
}

LLVMTypeRef
rc_build_elem_type(struct rc_llvm_context *ctx, struct rc_type type) {
    if (type.floating) {
        switch (type.width) {
            case 16:
                return LLVMHalfTypeInContext(ctx->context);
            case 32:
                return LLVMFloatTypeInContext(ctx->context);
            case 64:
                return LLVMDoubleTypeInContext(ctx->context);
            default:
                assert(0);
                return LLVMFloatTypeInContext(ctx->context);
        }
    }
    else {
        return LLVMIntTypeInContext(ctx->context, type.width);
    }
}

LLVMValueRef rc_build_const_int_vec(struct rc_llvm_context *rc, struct rc_type type, long long val) {
    LLVMTypeRef elem_type = LLVMIntTypeInContext(rc->context, type.width);
    LLVMValueRef elems[MAX_VECTOR_LENGTH];

    for (unsigned i = 0; i < type.length; ++i)
        elems[i] = LLVMConstInt(elem_type, val, type.sign ? 1 : 0);

    if (type.length == 1)
        return elems[0];

    return LLVMConstVector(elems, type.length);
}

LLVMTypeRef rc_build_vec_type(struct rc_llvm_context *rc, struct rc_type type) {
    LLVMTypeRef elem_type = rc_build_elem_type(rc, type);
    if (type.length == 1)
        return elem_type;
    else
        return LLVMVectorType(elem_type, type.length);
}