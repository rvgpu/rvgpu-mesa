#include <llvm-c/Core.h>
#include "rc_llvm_build.h"
#include "rc_bld_flow.h"

static LLVMBuilderRef create_builder_at_entry(struct rc_llvm_context *ctx) {
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef function = LLVMGetBasicBlockParent(current_block);
    LLVMBasicBlockRef first_block = LLVMGetEntryBasicBlock(function);
    LLVMValueRef first_instr = LLVMGetFirstInstruction(first_block);
    LLVMBuilderRef first_builder = LLVMCreateBuilderInContext(ctx->context);

    if (first_instr) {
        LLVMPositionBuilderBefore(first_builder, first_instr);
    } else {
        LLVMPositionBuilderAtEnd(first_builder, first_block);
    }

    return first_builder;
}

LLVMValueRef rc_build_alloca(struct rc_llvm_context *ctx, LLVMTypeRef type, const char *name) {
    LLVMBuilderRef first_builder = create_builder_at_entry(ctx);
    LLVMValueRef res = LLVMBuildAlloca(first_builder, type, name);
    LLVMBuildStore(ctx->builder, LLVMConstNull(type), res);
    LLVMDisposeBuilder(first_builder);

    return res;
}
