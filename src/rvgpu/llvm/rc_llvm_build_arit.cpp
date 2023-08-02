#include <llvm-c/Core.h>
#include <stdio.h>
#include <cassert>
#include "rc_llvm_build_arit.h"
#include "rc_build_type.h"


LLVMValueRef rc_build_add(struct rc_build_context *bld,
                              LLVMValueRef a, LLVMValueRef b) {
    LLVMValueRef res;

    if (a == bld->zero)
        return b;
    if (b == bld->zero)
        return a;
    if ((a == bld->undef) || (b == bld->undef))
        return bld->undef;

    if (bld->type.norm) {
        printf("TODO: alu iadd norm type \n");
    }

    if (bld->type.floating)
        res = LLVMBuildFAdd(bld->rc->builder, a, b, "");
    else
        res = LLVMBuildAdd(bld->rc->builder, a, b, "");

    return res;
}

LLVMValueRef rc_build_isnan(struct rc_build_context *bld, LLVMValueRef x) {
    LLVMValueRef mask;
    LLVMTypeRef int_vec_type = rc_build_int_vec_type(bld->rc, bld->type);

    mask = LLVMBuildFCmp(bld->rc->builder, LLVMRealOEQ, x, x, "isnotnan");
    mask = LLVMBuildNot(bld->rc->builder, mask, "");
    mask = LLVMBuildSExt(bld->rc->builder, mask, int_vec_type, "isnan");
    return mask;
}

static LLVMValueRef build_compare_ext(struct rc_llvm_context *rc,
                                      const struct rc_type type,
                                      enum rc_compare_func func,
                                      LLVMValueRef a,
                                      LLVMValueRef b,
                                      bool ordered) {
    LLVMTypeRef int_vec_type = rc_build_int_vec_type(rc, type);
    LLVMValueRef zeros = LLVMConstNull(int_vec_type);
    LLVMValueRef ones = LLVMConstAllOnes(int_vec_type);
    LLVMValueRef cond, res;

    if (func == rc_compare_func::NEVER)
        return zeros;
    if (func == rc_compare_func::ALWAYS)
        return ones;

    if (type.floating) {
        LLVMRealPredicate op;
        switch (func) {
            case rc_compare_func::EQUAL:
                op = ordered ? LLVMRealOEQ : LLVMRealUEQ;
                break;
            case rc_compare_func::NOTEQUAL:
                op = ordered ? LLVMRealONE : LLVMRealUNE;
                break;
            case rc_compare_func::LESS:
                op = ordered ? LLVMRealOLT : LLVMRealULT;
                break;
            case rc_compare_func::LEQUAL:
                op = ordered ? LLVMRealOGE : LLVMRealULE;
                break;
            case rc_compare_func::GREATER:
                op = ordered ? LLVMRealOGT : LLVMRealUGT;
                break;
            case rc_compare_func::GEQUAL:
                op = ordered ? LLVMRealOGE : LLVMRealUGE;
                break;
            default:
                assert(0);
                return rc_build_undef(rc, type);
        }
        cond = LLVMBuildFCmp(rc->builder, op, a, b, "");
        res = LLVMBuildSExt(rc->builder, cond, int_vec_type, "");
    } else {
        LLVMIntPredicate op;
        switch(func) {
            case rc_compare_func::EQUAL:
                op = LLVMIntEQ;
                break;
            case rc_compare_func::NOTEQUAL:
                op = LLVMIntNE;
                break;
            case rc_compare_func::LESS:
                op = type.sign ? LLVMIntSLT : LLVMIntULT;
                break;
            case rc_compare_func::LEQUAL:
                op = type.sign ? LLVMIntSLE : LLVMIntULE;
                break;
            case rc_compare_func::GREATER:
                op = type.sign ? LLVMIntSGT : LLVMIntUGT;
                break;
            case rc_compare_func::GEQUAL:
                op = type.sign ? LLVMIntSGE : LLVMIntUGE;
                break;
            default:
                assert(0);
                return rc_build_undef(rc, type);
        }
        cond = LLVMBuildICmp(rc->builder, op, a, b, "");
        res = LLVMBuildSExt(rc->builder, cond, int_vec_type, "");
    }
    return res;
}

static LLVMValueRef build_min_simple(struct rc_build_context *bld,
                                    LLVMValueRef a, LLVMValueRef b,
                                    enum nan_behavior nan) {
    LLVMValueRef cond;
    if (bld->type.floating) {
        switch (nan) {
            case nan_behavior::RETURN_OTHER: {
                LLVMValueRef isnan = rc_build_isnan(bld, a);
                cond = rc_build_cmp(bld, rc_compare_func::LESS, a, b);
                cond = LLVMBuildXor(bld->rc->builder, cond, isnan, "");
                return rc_build_select(bld, cond, a, b);
            }
            case nan_behavior::RETURN_OTHER_SECOND_NONNAN: {
                cond = build_compare_ext(bld->rc, bld->type, rc_compare_func::LESS, a, b, true);
                return rc_build_select(bld, cond, a, b);
            }
            case nan_behavior::RETURN_NAN_FIRST_NONNAN: {
                cond = rc_build_cmp(bld, rc_compare_func::LESS, b, a);
                return rc_build_select(bld, cond, b, a);
            }
            case nan_behavior::UNDEFINED: {
                cond = rc_build_cmp(bld, rc_compare_func::LESS, a, b);
                return rc_build_select(bld, cond, a, b);
            }
            default:
                assert(0);
        }
    } else {
        cond = rc_build_cmp(bld, rc_compare_func::LESS, a, b);
        return rc_build_select(bld, cond, a, b);
    }
}

LLVMValueRef rc_build_min(struct rc_build_context *bld, LLVMValueRef a, LLVMValueRef b) {
    if (a == bld->undef || b == bld->undef)
        return bld->undef;

    if (a == b)
        return a;

    if (bld->type.norm) {
        if (!bld->type.sign) {
            if ((a == bld->zero) || (b == bld->zero)) {
                return bld->zero;
            }
        }
        if (a == bld->one)
            return b;
        if (b == bld->one)
            return a;
    }
    return build_min_simple(bld, a, b, nan_behavior::UNDEFINED);
}



LLVMValueRef
rc_build_cmp(struct rc_build_context *bld,
             enum rc_compare_func func,
             LLVMValueRef a,
             LLVMValueRef b) {
    LLVMTypeRef int_vec_type = rc_build_int_vec_type(bld->rc, bld->type);
    LLVMValueRef zeros = LLVMConstNull(int_vec_type);
    LLVMValueRef ones = LLVMConstAllOnes(int_vec_type);

    if (func == rc_compare_func::NEVER)
        return zeros;
    if (func == rc_compare_func::ALWAYS)
        return ones;

    return build_compare_ext(bld->rc, bld->type, func, a, b, false);
}

LLVMValueRef rc_build_undef(struct rc_llvm_context *rc,  struct rc_type type) {
    LLVMTypeRef vec_type = rc_build_vec_type(rc, type);
    return LLVMGetUndef(vec_type);
}

LLVMValueRef rc_build_select(struct rc_build_context *bld,
                             LLVMValueRef mask,
                             LLVMValueRef a,
                             LLVMValueRef b) {
    LLVMValueRef res;
    if (a == b)
        return a;

    if (bld->type.length == 1) {
        mask = LLVMBuildTrunc(bld->rc->builder, mask, LLVMInt1TypeInContext(bld->rc->context), "");
        res = LLVMBuildSelect(bld->rc->builder, mask, a, b, "");
    } else if (LLVMIsConstant(mask) || LLVMGetInstructionOpcode(mask) == LLVMSExt) {
        LLVMTypeRef bool_vec_type = LLVMVectorType(LLVMInt1TypeInContext(bld->rc->context), bld->type.length);
        mask = LLVMBuildTrunc(bld->rc->builder, mask, bool_vec_type, "");
        res = LLVMBuildSelect(bld->rc->builder, mask, a, b, "");
    } else {
        res = rc_build_select_bitwise(bld, mask, a, b);
    }

    return res;
}

LLVMValueRef rc_build_select_bitwise(struct rc_build_context *bld,
                                     LLVMValueRef mask,
                                     LLVMValueRef a,
                                     LLVMValueRef b) {
    LLVMValueRef res;
    LLVMTypeRef int_vec_type = rc_build_int_vec_type(bld->rc, bld->type);
    if (a == b)
        return a;

    if (bld->type.floating) {
        a = LLVMBuildBitCast(bld->rc->builder, a, int_vec_type, "");
        b = LLVMBuildBitCast(bld->rc->builder, b, int_vec_type, "");
    }
    if (bld->type.width > 32) {
        mask = LLVMBuildSExt(bld->rc->builder, mask, int_vec_type, "");
    }
    a = LLVMBuildAnd(bld->rc->builder, a, mask, "");
    b = LLVMBuildAnd(bld->rc->builder, b, LLVMBuildNot(bld->rc->builder, mask, ""), "");
    res = LLVMBuildOr(bld->rc->builder, a, b, "");

    if (bld->type.floating) {
        LLVMTypeRef vec_type = rc_build_vec_type(bld->rc, bld->type);
        res = LLVMBuildBitCast(bld->rc->builder, res, vec_type, "");
    }
    return res;
}

LLVMValueRef rc_build_mul(struct rc_build_context *bld, LLVMValueRef a, LLVMValueRef b) {

    if (a == bld->zero)
        return bld->zero;
    if (a == bld->one)
        return b;
    if (b == bld->zero)
        return bld->zero;
    if (b == bld->one)
        return a;
    if (a == bld->undef || b == bld->undef)
        return bld->undef;

    if (!bld->type.floating && !bld->type.fixed && bld->type.norm) { printf("TODO : rc_build mul process norm type \n");}

    LLVMValueRef shift = bld->type.fixed ? rc_build_const_int_vec(bld->rc, bld->type, bld->type.width/2) : NULL;
    LLVMValueRef res;
    if (bld->type.floating)
        res = LLVMBuildFMul(bld->rc->builder, a, b, "");
    else
        res = LLVMBuildMul(bld->rc->builder, a, b, "");
    if (shift) {
        if (bld->type.sign)
            res = LLVMBuildAShr(bld->rc->builder, res, shift, "");
        else
            res = LLVMBuildLShr(bld->rc->builder, res, shift, "");
    }
    return res;
}