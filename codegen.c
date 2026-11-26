/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Kod:   codegen.c - generátor cílového kódu IFJcode26
 */

#include "codegen.h"
#include "error.h"
#include "parser.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dynamický textový buffer pro instrukce IFJcode26
typedef struct {
    char* data;
    size_t len;
    size_t cap;
} tStrBuf;

static void sb_init(tStrBuf* sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(tStrBuf* sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_append_n(tStrBuf* sb, const char* s, size_t n)
{
    if (n == 0) {
        return;
    }
    if (sb->len + n + 1 > sb->cap) {
        size_t new_cap = (sb->cap == 0) ? 256 : sb->cap * 2;
        while (new_cap < sb->len + n + 1) {
            new_cap *= 2;
        }
        char* tmp = realloc(sb->data, new_cap);
        if (tmp == NULL) {
            error_exit(ERR_INTERNAL, "nedostatek paměti v codegen");
        }
        sb->data = tmp;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void sb_printf(tStrBuf* sb, const char* fmt, ...)
{
    char stack_buf[512];
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);
    if (needed < 0) {
        error_exit(ERR_INTERNAL, "chyba formátování v codegen");
    }
    if ((size_t)needed < sizeof(stack_buf)) {
        sb_append_n(sb, stack_buf, (size_t)needed);
        return;
    }
    char* heap_buf = malloc((size_t)needed + 1);
    if (heap_buf == NULL) {
        error_exit(ERR_INTERNAL, "nedostatek paměti v codegen");
    }
    va_start(args, fmt);
    vsnprintf(heap_buf, (size_t)needed + 1, fmt, args);
    va_end(args);
    sb_append_n(sb, heap_buf, (size_t)needed);
    free(heap_buf);
}

static char* cg_strdup(const char* s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if (copy == NULL) {
        error_exit(ERR_INTERNAL, "nedostatek paměti v codegen");
    }
    memcpy(copy, s, len + 1);
    return copy;
}

// Formátování řetězcové konstanty pro IFJcode26 (escape \xyz pro 000..032, 035, 092)
static void sb_append_escaped_string(tStrBuf* sb, const char* s)
{
    sb_append_n(sb, "string@", 7);
    if (s == NULL) {
        return;
    }
    for (const unsigned char* p = (const unsigned char*)s; *p != '\0'; p++) {
        unsigned char c = *p;
        if (c <= 32 || c == 35 || c == 92 || c >= 127) {
            sb_printf(sb, "\\%03u", (unsigned int)c);
        } else {
            char ch = (char)c;
            sb_append_n(sb, &ch, 1);
        }
    }
}

// Lokální tabulka proměnných pro generování kódu (mapování jméno -> LF@jmeno$id)
typedef struct {
    char* src_name;
    char* ir_name;
    tTypeInfo type;
    int scope_depth;
} tCgVarEntry;

static struct {
    tStrBuf funcs_buf; // vygenerovaný kód všech uživatelských funkcí
    tStrBuf cur_defvars; // DEFVAR LF@... pro aktuální funkci
    tStrBuf cur_body; // tělo aktuální funkce
    char* cur_func_name;
    char* main_func_name;

    tCgVarEntry* vars;
    int var_count;
    int var_cap;
    int scope_depth;
    int var_uid;
    int temp_uid;
    int label_uid;
} cg;

static void emit_defvar(const char* ir_name)
{
    sb_printf(&cg.cur_defvars, "DEFVAR %s\n", ir_name);
}

static void emit(const char* fmt, ...)
{
    char stack_buf[512];
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);
    if (needed < 0) {
        error_exit(ERR_INTERNAL, "chyba formátování instrukce");
    }
    if ((size_t)needed < sizeof(stack_buf)) {
        sb_append_n(&cg.cur_body, stack_buf, (size_t)needed);
    } else {
        char* heap_buf = malloc((size_t)needed + 1);
        if (heap_buf == NULL) {
            error_exit(ERR_INTERNAL, "nedostatek paměti v codegen");
        }
        va_start(args, fmt);
        vsnprintf(heap_buf, (size_t)needed + 1, fmt, args);
        va_end(args);
        sb_append_n(&cg.cur_body, heap_buf, (size_t)needed);
        free(heap_buf);
    }
    sb_append_n(&cg.cur_body, "\n", 1);
}

int cg_init(void)
{
    memset(&cg, 0, sizeof(cg));
    sb_init(&cg.funcs_buf);
    sb_init(&cg.cur_defvars);
    sb_init(&cg.cur_body);
    return ERR_OK;
}

static void clear_vars(void)
{
    for (int i = 0; i < cg.var_count; i++) {
        free(cg.vars[i].src_name);
        free(cg.vars[i].ir_name);
    }
    free(cg.vars);
    cg.vars = NULL;
    cg.var_count = 0;
    cg.var_cap = 0;
    cg.scope_depth = 0;
}

void cg_cleanup(void)
{
    clear_vars();
    sb_free(&cg.funcs_buf);
    sb_free(&cg.cur_defvars);
    sb_free(&cg.cur_body);
    free(cg.cur_func_name);
    cg.cur_func_name = NULL;
    free(cg.main_func_name);
    cg.main_func_name = NULL;
}

void cg_set_main(const char* main_func_name)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    free(cg.main_func_name);
    cg.main_func_name = cg_strdup(main_func_name);
}

int cg_new_label_id(void)
{
    return cg.label_uid++;
}

const char* cg_new_temp(void)
{
    static char ring[16][48];
    static int ring_idx = 0;
    char* slot = ring[ring_idx];
    ring_idx = (ring_idx + 1) % 16;
    snprintf(slot, 48, "LF@%%t_%d", cg.temp_uid++);
    emit_defvar(slot);
    return slot;
}

void cg_scope_push(void)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    cg.scope_depth++;
}

void cg_scope_pop(void)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    while (cg.var_count > 0 && cg.vars[cg.var_count - 1].scope_depth == cg.scope_depth) {
        cg.var_count--;
        free(cg.vars[cg.var_count].src_name);
        free(cg.vars[cg.var_count].ir_name);
    }
    if (cg.scope_depth > 0) {
        cg.scope_depth--;
    }
}

const char* cg_define_var(const char* name, tTypeInfo type)
{
    if (cg.var_count >= cg.var_cap) {
        int new_cap = (cg.var_cap == 0) ? 32 : cg.var_cap * 2;
        tCgVarEntry* tmp = realloc(cg.vars, (size_t)new_cap * sizeof(tCgVarEntry));
        if (tmp == NULL) {
            error_exit(ERR_INTERNAL, "nedostatek paměti v codegen");
        }
        cg.vars = tmp;
        cg.var_cap = new_cap;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "LF@%s$%d", name, cg.var_uid++);
    cg.vars[cg.var_count].src_name = cg_strdup(name);
    cg.vars[cg.var_count].ir_name = cg_strdup(buf);
    cg.vars[cg.var_count].type = type;
    cg.vars[cg.var_count].scope_depth = cg.scope_depth;
    cg.var_count++;

    emit_defvar(buf);
    return cg.vars[cg.var_count - 1].ir_name;
}

const char* cg_find_var(const char* name, tTypeInfo* out_type)
{
    for (int i = cg.var_count - 1; i >= 0; i--) {
        if (strcmp(cg.vars[i].src_name, name) == 0) {
            if (out_type != NULL) {
                *out_type = cg.vars[i].type;
            }
            return cg.vars[i].ir_name;
        }
    }
    return NULL;
}

// --- AST výrazů ---

static tExpr* alloc_expr(tExprKind kind, tTypeInfo type)
{
    tExpr* e = calloc(1, sizeof(tExpr));
    if (e == NULL) {
        error_exit(ERR_INTERNAL, "nedostatek paměti pro výraz");
    }
    e->kind = kind;
    e->type = type;
    return e;
}

tExpr* cg_expr_int(int32_t val)
{
    tTypeInfo t = { TYPE_INT, TYPE_UNIT };
    tExpr* e = alloc_expr(EXPR_INT, t);
    e->data.int_val = val;
    return e;
}

tExpr* cg_expr_double(double val)
{
    tTypeInfo t = { TYPE_DOUBLE, TYPE_UNIT };
    tExpr* e = alloc_expr(EXPR_DOUBLE, t);
    e->data.double_val = val;
    return e;
}

tExpr* cg_expr_string(const char* val)
{
    tTypeInfo t = { TYPE_STRING, TYPE_UNIT };
    tExpr* e = alloc_expr(EXPR_STRING, t);
    e->data.str_val = cg_strdup(val != NULL ? val : "");
    return e;
}

tExpr* cg_expr_bool(bool val)
{
    tTypeInfo t = { TYPE_BOOLEAN, TYPE_UNIT };
    tExpr* e = alloc_expr(EXPR_BOOL, t);
    e->data.bool_val = val;
    return e;
}

tExpr* cg_expr_unit(void)
{
    tTypeInfo t = { TYPE_UNIT, TYPE_UNIT };
    return alloc_expr(EXPR_UNIT, t);
}

tExpr* cg_expr_var(const char* name, tTypeInfo type)
{
    tExpr* e = alloc_expr(EXPR_VAR, type);
    e->data.str_val = cg_strdup(name);
    return e;
}

tExpr* cg_expr_temp_var(const char* lf_name, tTypeInfo type)
{
    tExpr* e = alloc_expr(EXPR_TEMP_VAR, type);
    e->data.str_val = cg_strdup(lf_name);
    return e;
}

tExpr* cg_expr_unary(tTokenType op, tExpr* operand, tTypeInfo result_type)
{
    tExpr* e = alloc_expr(EXPR_UNARY, result_type);
    e->data.unary.op = op;
    e->data.unary.operand = operand;
    return e;
}

tExpr* cg_expr_binary(tTokenType op, tExpr* left, tExpr* right, tTypeInfo result_type)
{
    tExpr* e = alloc_expr(EXPR_BINARY, result_type);
    e->data.binary.op = op;
    e->data.binary.left = left;
    e->data.binary.right = right;
    return e;
}

tExpr* cg_expr_call(const char* name, tExpr** args, int arg_count, tTypeInfo ret_type)
{
    tExpr* e = alloc_expr(EXPR_CALL, ret_type);
    e->data.call.name = cg_strdup(name);
    e->data.call.args = args;
    e->data.call.arg_count = arg_count;
    return e;
}

tExpr* cg_expr_member(tExpr* receiver, const char* member, tExpr** args, int arg_count, tTypeInfo res_type)
{
    tExpr* e = alloc_expr(EXPR_MEMBER, res_type);
    e->data.member.receiver = receiver;
    e->data.member.member = cg_strdup(member);
    e->data.member.args = args;
    e->data.member.arg_count = arg_count;
    return e;
}

tExpr* cg_expr_list_lit(tExpr** elems, int count, tTypeInfo list_type)
{
    tExpr* e = alloc_expr(EXPR_LIST_LIT, list_type);
    e->data.list_lit.elems = elems;
    e->data.list_lit.count = count;
    return e;
}

tExpr* cg_expr_list_index(tExpr* list_expr, tExpr* index_expr, tTypeInfo elem_type)
{
    tExpr* e = alloc_expr(EXPR_LIST_INDEX, elem_type);
    e->data.list_index.list_expr = list_expr;
    e->data.list_index.index_expr = index_expr;
    return e;
}

tExpr* cg_expr_range(tExpr* start, tExpr* end, tExpr* step, bool inclusive)
{
    tTypeInfo t = { TYPE_LIST, TYPE_INT };
    tExpr* e = alloc_expr(EXPR_RANGE, t);
    e->data.range.start = start;
    e->data.range.end = end;
    e->data.range.step = step;
    e->data.range.inclusive = inclusive;
    return e;
}

void cg_expr_free(tExpr* expr)
{
    if (expr == NULL) {
        return;
    }
    switch (expr->kind) {
    case EXPR_STRING:
    case EXPR_VAR:
    case EXPR_TEMP_VAR:
        free(expr->data.str_val);
        break;
    case EXPR_UNARY:
        cg_expr_free(expr->data.unary.operand);
        break;
    case EXPR_BINARY:
        cg_expr_free(expr->data.binary.left);
        cg_expr_free(expr->data.binary.right);
        break;
    case EXPR_CALL:
        free(expr->data.call.name);
        for (int i = 0; i < expr->data.call.arg_count; i++) {
            cg_expr_free(expr->data.call.args[i]);
        }
        free(expr->data.call.args);
        break;
    case EXPR_MEMBER:
        cg_expr_free(expr->data.member.receiver);
        free(expr->data.member.member);
        for (int i = 0; i < expr->data.member.arg_count; i++) {
            cg_expr_free(expr->data.member.args[i]);
        }
        free(expr->data.member.args);
        break;
    case EXPR_LIST_LIT:
        for (int i = 0; i < expr->data.list_lit.count; i++) {
            cg_expr_free(expr->data.list_lit.elems[i]);
        }
        free(expr->data.list_lit.elems);
        break;
    case EXPR_LIST_INDEX:
        cg_expr_free(expr->data.list_index.list_expr);
        cg_expr_free(expr->data.list_index.index_expr);
        break;
    case EXPR_RANGE:
        cg_expr_free(expr->data.range.start);
        cg_expr_free(expr->data.range.end);
        cg_expr_free(expr->data.range.step);
        break;
    default:
        break;
    }
    free(expr);
}

// --- Pomocné rutiny pro generování hodnot a vestavěných operací ---

static const char* type_tag_str(tDataType t)
{
    switch (t) {
    case TYPE_INT:
        return "int";
    case TYPE_DOUBLE:
        return "float";
    case TYPE_STRING:
        return "string";
    case TYPE_BOOLEAN:
        return "bool";
    case TYPE_UNIT:
        return "nil";
    case TYPE_LIST:
        return "list";
    default:
        return "nil";
    }
}

static void emit_to_str_of_var(const char* src_var, tTypeInfo src_type, const char* dest_var)
{
    switch (src_type.type) {
    case TYPE_INT:
        emit("INT2STR %s %s", dest_var, src_var);
        break;
    case TYPE_DOUBLE:
        emit("FLOAT2STR %s %s", dest_var, src_var);
        break;
    case TYPE_STRING:
        emit("MOVE %s %s", dest_var, src_var);
        break;
    case TYPE_BOOLEAN: {
        int id = cg_new_label_id();
        emit("JUMPIFEQ $bstr_t_%d %s bool@true", id, src_var);
        emit("MOVE %s string@false", dest_var);
        emit("JUMP $bstr_e_%d", id);
        emit("LABEL $bstr_t_%d", id);
        emit("MOVE %s string@true", dest_var);
        emit("LABEL $bstr_e_%d", id);
        break;
    }
    case TYPE_UNIT:
        emit("MOVE %s string@()", dest_var);
        break;
    case TYPE_LIST:
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", src_var);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 string@%s", type_tag_str(src_type.innerType));
        emit("CALL $__list_to_str");
        emit("MOVE %s TF@%%ret", dest_var);
        break;
    }
}

void cg_emit_widen_if_needed(const char* var_name, tTypeInfo expected, tTypeInfo actual)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    if (expected.type == TYPE_DOUBLE && actual.type == TYPE_INT) {
        emit("INT2FLOAT %s %s", var_name, var_name);
    }
}

static void emit_builtin_call(tExpr* expr, const char* dest_var)
{
    const char* fn = expr->data.call.name;

    if (strcmp(fn, "print") == 0) {
        tExpr* arg = expr->data.call.args[0];
        char* v = cg_strdup(cg_new_temp());
        cg_emit_expr_into(arg, v);
        if (arg->type.type == TYPE_UNIT) {
            emit("WRITE string@()");
        } else if (arg->type.type == TYPE_LIST) {
            char* s = cg_strdup(cg_new_temp());
            emit_to_str_of_var(v, arg->type, s);
            emit("WRITE %s", s);
            free(s);
        } else {
            emit("WRITE %s", v);
        }
        emit("MOVE %s nil@nil", dest_var);
        free(v);
        return;
    }

    if (strcmp(fn, "toStr") == 0) {
        tExpr* arg = expr->data.call.args[0];
        char* v = cg_strdup(cg_new_temp());
        cg_emit_expr_into(arg, v);
        emit_to_str_of_var(v, arg->type, dest_var);
        free(v);
        return;
    }

    if (strcmp(fn, "readLine") == 0) {
        int id = cg_new_label_id();
        char* tp = cg_strdup(cg_new_temp());
        emit("READ %s string", dest_var);
        emit("TYPE %s %s", tp, dest_var);
        emit("JUMPIFNEQ $rl_ok_%d %s string@nil", id, tp);
        emit("MOVE %s string@", dest_var);
        emit("LABEL $rl_ok_%d", id);
        free(tp);
        return;
    }

    if (strcmp(fn, "readInt") == 0) {
        int id = cg_new_label_id();
        char* tp = cg_strdup(cg_new_temp());
        emit("READ %s int", dest_var);
        emit("TYPE %s %s", tp, dest_var);
        emit("JUMPIFNEQ $ri_ok_%d %s string@nil", id, tp);
        emit("MOVE %s int@0", dest_var);
        emit("LABEL $ri_ok_%d", id);
        free(tp);
        return;
    }

    if (strcmp(fn, "readDouble") == 0) {
        int id = cg_new_label_id();
        char* tp = cg_strdup(cg_new_temp());
        emit("READ %s float", dest_var);
        emit("TYPE %s %s", tp, dest_var);
        emit("JUMPIFNEQ $rd_ok_%d %s string@nil", id, tp);
        emit("MOVE %s float@0x0p+0", dest_var);
        emit("LABEL $rd_ok_%d", id);
        free(tp);
        return;
    }

    if (strcmp(fn, "readBoolean") == 0) {
        int id = cg_new_label_id();
        char* tp = cg_strdup(cg_new_temp());
        emit("READ %s bool", dest_var);
        emit("TYPE %s %s", tp, dest_var);
        emit("JUMPIFNEQ $rb_ok_%d %s string@nil", id, tp);
        emit("MOVE %s bool@false", dest_var);
        emit("LABEL $rb_ok_%d", id);
        free(tp);
        return;
    }

    if (strcmp(fn, "toInt") == 0) {
        char* v = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.call.args[0], v);
        emit("FLOAT2INT %s %s", dest_var, v);
        free(v);
        return;
    }

    if (strcmp(fn, "strLength") == 0) {
        char* v = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.call.args[0], v);
        emit("STRLEN %s %s", dest_var, v);
        free(v);
        return;
    }

    if (strcmp(fn, "subStr") == 0) {
        char* vs = cg_strdup(cg_new_temp());
        char* vstart = cg_strdup(cg_new_temp());
        char* vend = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.call.args[0], vs);
        cg_emit_expr_into(expr->data.call.args[1], vstart);
        cg_emit_expr_into(expr->data.call.args[2], vend);
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", vs);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 %s", vstart);
        emit("DEFVAR TF@%%p2");
        emit("MOVE TF@%%p2 %s", vend);
        emit("CALL $__substr");
        emit("MOVE %s TF@%%ret", dest_var);
        free(vs);
        free(vstart);
        free(vend);
        return;
    }

    if (strcmp(fn, "ord") == 0) {
        int id = cg_new_label_id();
        char* vs = cg_strdup(cg_new_temp());
        char* vi = cg_strdup(cg_new_temp());
        char* vlen = cg_strdup(cg_new_temp());
        char* vcmp = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.call.args[0], vs);
        cg_emit_expr_into(expr->data.call.args[1], vi);
        emit("STRLEN %s %s", vlen, vs);
        emit("LT %s %s int@0", vcmp, vi);
        emit("JUMPIFEQ $ord_bad_%d %s bool@true", id, vcmp);
        emit("LT %s %s %s", vcmp, vi, vlen);
        emit("JUMPIFNEQ $ord_bad_%d %s bool@true", id, vcmp);
        emit("STRI2INT %s %s %s", dest_var, vs, vi);
        emit("JUMP $ord_end_%d", id);
        emit("LABEL $ord_bad_%d", id);
        emit("MOVE %s int@-1", dest_var);
        emit("LABEL $ord_end_%d", id);
        free(vs);
        free(vi);
        free(vlen);
        free(vcmp);
        return;
    }

    if (strcmp(fn, "chr") == 0) {
        char* vc = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.call.args[0], vc);
        emit("INT2CHAR %s %s", dest_var, vc);
        free(vc);
        return;
    }
}

static bool is_builtin_name(const char* name)
{
    return strcmp(name, "print") == 0 || strcmp(name, "readLine") == 0 || strcmp(name, "readInt") == 0 || strcmp(name, "readDouble") == 0 || strcmp(name, "readBoolean") == 0 || strcmp(name, "toStr") == 0 || strcmp(name, "toInt") == 0 || strcmp(name, "strLength") == 0 || strcmp(name, "subStr") == 0 || strcmp(name, "ord") == 0 || strcmp(name, "chr") == 0;
}

static void emit_member_expr(tExpr* expr, const char* dest_var)
{
    tExpr* recv = expr->data.member.receiver;
    const char* mem = expr->data.member.member;

    char* v_recv = cg_strdup(cg_new_temp());
    cg_emit_expr_into(recv, v_recv);

    if (strcmp(mem, "toString") == 0) {
        emit_to_str_of_var(v_recv, recv->type, dest_var);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "toInt") == 0) {
        emit("FLOAT2INT %s %s", dest_var, v_recv);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "floor") == 0) {
        int id = cg_new_label_id();
        char* visint = cg_strdup(cg_new_temp());
        char* vti = cg_strdup(cg_new_temp());
        char* vneg = cg_strdup(cg_new_temp());
        emit("ISINT %s %s", visint, v_recv);
        emit("JUMPIFEQ $fl_exact_%d %s bool@true", id, visint);
        emit("FLOAT2INT %s %s", vti, v_recv);
        emit("LT %s %s float@0x0p+0", vneg, v_recv);
        emit("JUMPIFNEQ $fl_conv_%d %s bool@true", id, vneg);
        emit("SUB %s %s int@1", vti, vti);
        emit("LABEL $fl_conv_%d", id);
        emit("INT2FLOAT %s %s", dest_var, vti);
        emit("JUMP $fl_end_%d", id);
        emit("LABEL $fl_exact_%d", id);
        emit("MOVE %s %s", dest_var, v_recv);
        emit("LABEL $fl_end_%d", id);
        free(visint);
        free(vti);
        free(vneg);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "length") == 0) {
        emit("STRLEN %s %s", dest_var, v_recv);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "head") == 0) {
        char* vlen = cg_strdup(cg_new_temp());
        char* vdata = cg_strdup(cg_new_temp());
        emit("ARRAYGET %s %s int@0", vlen, v_recv);
        emit("JUMPIFEQ $__err_40 %s int@0", vlen);
        emit("ARRAYGET %s %s int@1", vdata, v_recv);
        emit("ARRAYGET %s %s int@0", dest_var, vdata);
        free(vlen);
        free(vdata);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "tail") == 0) {
        char* vlen = cg_strdup(cg_new_temp());
        emit("ARRAYGET %s %s int@0", vlen, v_recv);
        emit("JUMPIFEQ $__err_40 %s int@0", vlen);
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", v_recv);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 int@1");
        emit("DEFVAR TF@%%p2");
        emit("MOVE TF@%%p2 %s", vlen);
        emit("CALL $__list_slice");
        emit("MOVE %s TF@%%ret", dest_var);
        free(vlen);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "drop") == 0) {
        char* vn = cg_strdup(cg_new_temp());
        char* vlen = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.member.args[0], vn);
        emit("ARRAYGET %s %s int@0", vlen, v_recv);
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", v_recv);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 %s", vn);
        emit("DEFVAR TF@%%p2");
        emit("MOVE TF@%%p2 %s", vlen);
        emit("CALL $__list_slice");
        emit("MOVE %s TF@%%ret", dest_var);
        free(vn);
        free(vlen);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "take") == 0) {
        char* vn = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.member.args[0], vn);
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", v_recv);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 int@0");
        emit("DEFVAR TF@%%p2");
        emit("MOVE TF@%%p2 %s", vn);
        emit("CALL $__list_slice");
        emit("MOVE %s TF@%%ret", dest_var);
        free(vn);
        free(v_recv);
        return;
    }
    if (strcmp(mem, "slice") == 0) {
        char* vfrom = cg_strdup(cg_new_temp());
        char* vuntil = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.member.args[0], vfrom);
        cg_emit_expr_into(expr->data.member.args[1], vuntil);
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", v_recv);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 %s", vfrom);
        emit("DEFVAR TF@%%p2");
        emit("MOVE TF@%%p2 %s", vuntil);
        emit("CALL $__list_slice");
        emit("MOVE %s TF@%%ret", dest_var);
        free(vfrom);
        free(vuntil);
        free(v_recv);
        return;
    }
    free(v_recv);
}

void cg_emit_expr_into(tExpr* expr, const char* dest_var)
{
    if (!parser_codegen_enabled() || expr == NULL) {
        return;
    }

    switch (expr->kind) {
    case EXPR_INT:
        emit("MOVE %s int@%d", dest_var, (int)expr->data.int_val);
        break;

    case EXPR_DOUBLE:
        emit("MOVE %s float@%a", dest_var, expr->data.double_val);
        break;

    case EXPR_STRING: {
        tStrBuf tmp;
        sb_init(&tmp);
        sb_append_escaped_string(&tmp, expr->data.str_val);
        emit("MOVE %s %s", dest_var, tmp.data);
        sb_free(&tmp);
        break;
    }

    case EXPR_BOOL:
        emit("MOVE %s bool@%s", dest_var, expr->data.bool_val ? "true" : "false");
        break;

    case EXPR_UNIT:
        emit("MOVE %s nil@nil", dest_var);
        break;

    case EXPR_VAR: {
        const char* ir = cg_find_var(expr->data.str_val, NULL);
        if (ir == NULL) {
            error_exit(ERR_INTERNAL, "neznámá proměnná v codegen: %s", expr->data.str_val);
        }
        emit("MOVE %s %s", dest_var, ir);
        break;
    }

    case EXPR_TEMP_VAR:
        emit("MOVE %s %s", dest_var, expr->data.str_val);
        break;

    case EXPR_UNARY: {
        char* v = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.unary.operand, v);
        if (expr->data.unary.op == TOK_MINUS) {
            if (expr->type.type == TYPE_DOUBLE) {
                emit("SUB %s float@0x0p+0 %s", dest_var, v);
            } else {
                emit("SUB %s int@0 %s", dest_var, v);
            }
        } else if (expr->data.unary.op == TOK_PLUS) {
            emit("MOVE %s %s", dest_var, v);
        } else if (expr->data.unary.op == TOK_NOT) {
            emit("NOT %s %s", dest_var, v);
        }
        free(v);
        break;
    }

    case EXPR_BINARY: {
        tTokenType op = expr->data.binary.op;
        tExpr* left = expr->data.binary.left;
        tExpr* right = expr->data.binary.right;

        // Zkratové vyhodnocení && a ||
        if (op == TOK_AND) {
            int id = cg_new_label_id();
            char* vl = cg_strdup(cg_new_temp());
            cg_emit_expr_into(left, vl);
            emit("JUMPIFNEQ $and_f_%d %s bool@true", id, vl);
            cg_emit_expr_into(right, dest_var);
            emit("JUMP $and_e_%d", id);
            emit("LABEL $and_f_%d", id);
            emit("MOVE %s bool@false", dest_var);
            emit("LABEL $and_e_%d", id);
            free(vl);
            break;
        }
        if (op == TOK_OR) {
            int id = cg_new_label_id();
            char* vl = cg_strdup(cg_new_temp());
            cg_emit_expr_into(left, vl);
            emit("JUMPIFEQ $or_t_%d %s bool@true", id, vl);
            cg_emit_expr_into(right, dest_var);
            emit("JUMP $or_e_%d", id);
            emit("LABEL $or_t_%d", id);
            emit("MOVE %s bool@true", dest_var);
            emit("LABEL $or_e_%d", id);
            free(vl);
            break;
        }

        char* vl = cg_strdup(cg_new_temp());
        char* vr = cg_strdup(cg_new_temp());
        cg_emit_expr_into(left, vl);
        cg_emit_expr_into(right, vr);

        // Konkatenace seznamů ++
        if (op == TOK_CONCAT) {
            emit("CREATEFRAME");
            emit("DEFVAR TF@%%p0");
            emit("MOVE TF@%%p0 %s", vl);
            emit("DEFVAR TF@%%p1");
            emit("MOVE TF@%%p1 %s", vr);
            emit("CALL $__list_concat");
            emit("MOVE %s TF@%%ret", dest_var);
            free(vl);
            free(vr);
            break;
        }

        // Konkatenace řetězců +
        if (op == TOK_PLUS && left->type.type == TYPE_STRING && right->type.type == TYPE_STRING) {
            emit("CONCAT %s %s %s", dest_var, vl, vr);
            free(vl);
            free(vr);
            break;
        }

        // Rovnost seznamů == / != (strukturální porovnání)
        if ((op == TOK_EQ || op == TOK_NEQ) && (left->type.type == TYPE_LIST || right->type.type == TYPE_LIST)) {
            emit("CREATEFRAME");
            emit("DEFVAR TF@%%p0");
            emit("MOVE TF@%%p0 %s", vl);
            emit("DEFVAR TF@%%p1");
            emit("MOVE TF@%%p1 %s", vr);
            emit("CALL $__list_eq");
            emit("MOVE %s TF@%%ret", dest_var);
            if (op == TOK_NEQ) {
                emit("NOT %s %s", dest_var, dest_var);
            }
            free(vl);
            free(vr);
            break;
        }

        // Implicitní konverze Int -> Double při smíšených číselných operandech
        bool is_num_mix = (left->type.type == TYPE_INT && right->type.type == TYPE_DOUBLE) || (left->type.type == TYPE_DOUBLE && right->type.type == TYPE_INT);
        if (is_num_mix) {
            if (left->type.type == TYPE_INT) {
                emit("INT2FLOAT %s %s", vl, vl);
            }
            if (right->type.type == TYPE_INT) {
                emit("INT2FLOAT %s %s", vr, vr);
            }
        }

        switch (op) {
        case TOK_PLUS:
            emit("ADD %s %s %s", dest_var, vl, vr);
            break;
        case TOK_MINUS:
            emit("SUB %s %s %s", dest_var, vl, vr);
            break;
        case TOK_STAR:
            emit("MUL %s %s %s", dest_var, vl, vr);
            break;
        case TOK_SLASH:
            if (expr->type.type == TYPE_DOUBLE) {
                emit("DIV %s %s %s", dest_var, vl, vr);
            } else {
                emit("IDIV %s %s %s", dest_var, vl, vr);
            }
            break;
        case TOK_LT:
            emit("LT %s %s %s", dest_var, vl, vr);
            break;
        case TOK_GT:
            emit("GT %s %s %s", dest_var, vl, vr);
            break;
        case TOK_LE:
            emit("GT %s %s %s", dest_var, vl, vr);
            emit("NOT %s %s", dest_var, dest_var);
            break;
        case TOK_GE:
            emit("LT %s %s %s", dest_var, vl, vr);
            emit("NOT %s %s", dest_var, dest_var);
            break;
        case TOK_EQ:
            emit("EQ %s %s %s", dest_var, vl, vr);
            break;
        case TOK_NEQ:
            emit("EQ %s %s %s", dest_var, vl, vr);
            emit("NOT %s %s", dest_var, dest_var);
            break;
        default:
            break;
        }

        free(vl);
        free(vr);
        break;
    }

    case EXPR_CALL: {
        if (is_builtin_name(expr->data.call.name)) {
            emit_builtin_call(expr, dest_var);
            break;
        }
        int n = expr->data.call.arg_count;
        char** arg_vars = NULL;
        if (n > 0) {
            arg_vars = malloc((size_t)n * sizeof(char*));
            if (arg_vars == NULL) {
                error_exit(ERR_INTERNAL, "nedostatek paměti v codegen");
            }
            // Vyhodnocení argumentů zleva doprava před CREATEFRAME (kvůli vnořeným voláním)
            for (int i = 0; i < n; i++) {
                arg_vars[i] = cg_strdup(cg_new_temp());
                cg_emit_expr_into(expr->data.call.args[i], arg_vars[i]);
            }
        }
        emit("CREATEFRAME");
        for (int i = 0; i < n; i++) {
            emit("DEFVAR TF@%%p%d", i);
            emit("MOVE TF@%%p%d %s", i, arg_vars[i]);
            free(arg_vars[i]);
        }
        free(arg_vars);
        emit("CALL $%s", expr->data.call.name);
        emit("MOVE %s TF@%%ret", dest_var);
        break;
    }

    case EXPR_MEMBER:
        emit_member_expr(expr, dest_var);
        break;

    case EXPR_LIST_LIT: {
        int n = expr->data.list_lit.count;
        if (n == 0) {
            emit("CREATEFRAME");
            emit("CALL $__list_empty");
            emit("MOVE %s TF@%%ret", dest_var);
            break;
        }
        char* vdata = cg_strdup(cg_new_temp());
        char* velem = cg_strdup(cg_new_temp());
        emit("NEWARRAY %s int@%d", vdata, n);
        for (int i = 0; i < n; i++) {
            tExpr* el = expr->data.list_lit.elems[i];
            cg_emit_expr_into(el, velem);
            if (expr->type.innerType == TYPE_DOUBLE && el->type.type == TYPE_INT) {
                emit("INT2FLOAT %s %s", velem, velem);
            }
            emit("ARRAYSET %s int@%d %s", vdata, i, velem);
        }
        emit("NEWARRAY %s int@2", dest_var);
        emit("ARRAYSET %s int@0 int@%d", dest_var, n);
        emit("ARRAYSET %s int@1 %s", dest_var, vdata);
        free(vdata);
        free(velem);
        break;
    }

    case EXPR_LIST_INDEX: {
        char* vlist = cg_strdup(cg_new_temp());
        char* vidx = cg_strdup(cg_new_temp());
        char* vlen = cg_strdup(cg_new_temp());
        char* vdata = cg_strdup(cg_new_temp());
        char* vcmp = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.list_index.list_expr, vlist);
        cg_emit_expr_into(expr->data.list_index.index_expr, vidx);
        emit("ARRAYGET %s %s int@0", vlen, vlist);
        emit("LT %s %s int@0", vcmp, vidx);
        emit("JUMPIFEQ $__err_40 %s bool@true", vcmp);
        emit("LT %s %s %s", vcmp, vidx, vlen);
        emit("JUMPIFNEQ $__err_40 %s bool@true", vcmp);
        emit("ARRAYGET %s %s int@1", vdata, vlist);
        emit("ARRAYGET %s %s %s", dest_var, vdata, vidx);
        free(vlist);
        free(vidx);
        free(vlen);
        free(vdata);
        free(vcmp);
        break;
    }

    case EXPR_RANGE: {
        char* vs = cg_strdup(cg_new_temp());
        char* ve = cg_strdup(cg_new_temp());
        char* vst = cg_strdup(cg_new_temp());
        cg_emit_expr_into(expr->data.range.start, vs);
        cg_emit_expr_into(expr->data.range.end, ve);
        if (expr->data.range.step != NULL) {
            cg_emit_expr_into(expr->data.range.step, vst);
        } else {
            emit("MOVE %s int@1", vst);
        }
        emit("CREATEFRAME");
        emit("DEFVAR TF@%%p0");
        emit("MOVE TF@%%p0 %s", vs);
        emit("DEFVAR TF@%%p1");
        emit("MOVE TF@%%p1 %s", ve);
        emit("DEFVAR TF@%%p2");
        emit("MOVE TF@%%p2 %s", vst);
        emit("DEFVAR TF@%%p3");
        emit("MOVE TF@%%p3 bool@%s", expr->data.range.inclusive ? "true" : "false");
        emit("CALL $__list_range");
        emit("MOVE %s TF@%%ret", dest_var);
        free(vs);
        free(ve);
        free(vst);
        break;
    }
    }
}

// --- Funkce, proměnné a řídicí struktury ---

void cg_func_begin(const char* name, char** param_names, int param_count)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    clear_vars();
    sb_free(&cg.cur_defvars);
    sb_free(&cg.cur_body);
    sb_init(&cg.cur_defvars);
    sb_init(&cg.cur_body);

    free(cg.cur_func_name);
    cg.cur_func_name = cg_strdup(name);
    cg.temp_uid = 0;

    emit_defvar("LF@%ret");
    emit("MOVE LF@%%ret nil@nil");

    tTypeInfo dummy = { TYPE_UNIT, TYPE_UNIT };
    for (int i = 0; i < param_count; i++) {
        const char* ir = cg_define_var(param_names[i], dummy);
        emit("MOVE %s LF@%%p%d", ir, i);
    }
}

void cg_func_end(tTypeInfo ret_type, tTypeInfo last_body_type, bool ended_with_return)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    if (!ended_with_return) {
        if (ret_type.type == TYPE_UNIT) {
            emit("MOVE LF@%%ret nil@nil");
        } else if (ret_type.type == TYPE_DOUBLE && last_body_type.type == TYPE_INT) {
            emit("INT2FLOAT LF@%%ret LF@%%ret");
        }
    }
    emit("LABEL $%s$end", cg.cur_func_name);
    emit("POPFRAME");
    emit("RETURN");

    sb_printf(&cg.funcs_buf, "\nLABEL $%s\n", cg.cur_func_name);
    sb_printf(&cg.funcs_buf, "PUSHFRAME\n");
    if (cg.cur_defvars.data != NULL) {
        sb_append_n(&cg.funcs_buf, cg.cur_defvars.data, cg.cur_defvars.len);
    }
    if (cg.cur_body.data != NULL) {
        sb_append_n(&cg.funcs_buf, cg.cur_body.data, cg.cur_body.len);
    }

    clear_vars();
}

void cg_emit_var_init(const char* name, tTypeInfo var_type, tExpr* init_expr)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    const char* ir = cg_define_var(name, var_type);
    cg_emit_expr_into(init_expr, ir);
    cg_emit_widen_if_needed(ir, var_type, init_expr->type);
}

void cg_emit_assign(const char* name, tExpr* rhs_expr)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    tTypeInfo var_type = { TYPE_UNIT, TYPE_UNIT };
    const char* ir = cg_find_var(name, &var_type);
    if (ir == NULL) {
        error_exit(ERR_INTERNAL, "neznámá proměnná při přiřazení: %s", name);
    }
    cg_emit_expr_into(rhs_expr, ir);
    cg_emit_widen_if_needed(ir, var_type, rhs_expr->type);
}

void cg_emit_return(bool has_expr, tExpr* expr, tTypeInfo expected_ret_type)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    if (!has_expr) {
        emit("MOVE LF@%%ret nil@nil");
    } else {
        cg_emit_expr_into(expr, "LF@%ret");
        if (expected_ret_type.type == TYPE_UNIT) {
            emit("MOVE LF@%%ret nil@nil");
        } else {
            cg_emit_widen_if_needed("LF@%ret", expected_ret_type, expr->type);
        }
    }
    emit("JUMP $%s$end", cg.cur_func_name);
}

tCgIfState cg_if_begin(tExpr* cond, bool need_result)
{
    tCgIfState st;
    memset(&st, 0, sizeof(st));
    if (!parser_codegen_enabled()) {
        return st;
    }
    st.id = cg_new_label_id();
    if (need_result) {
        st.res_var = cg_strdup(cg_new_temp());
        emit("MOVE %s nil@nil", st.res_var);
    }
    char* vcond = cg_strdup(cg_new_temp());
    cg_emit_expr_into(cond, vcond);
    emit("JUMPIFNEQ $if_else_%d %s bool@true", st.id, vcond);
    free(vcond);
    return st;
}

void cg_if_else(tCgIfState* state)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    state->has_else = true;
    emit("JUMP $if_end_%d", state->id);
    emit("LABEL $if_else_%d", state->id);
}

void cg_if_end(tCgIfState* state, bool need_result, tTypeInfo res_type, tTypeInfo then_type, tTypeInfo else_type)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    if (!state->has_else) {
        emit("LABEL $if_else_%d", state->id);
    }
    emit("LABEL $if_end_%d", state->id);

    if (need_result && state->res_var != NULL && res_type.type == TYPE_DOUBLE && (then_type.type == TYPE_INT || else_type.type == TYPE_INT)) {
        int id = cg_new_label_id();
        char* tp = cg_strdup(cg_new_temp());
        emit("TYPE %s %s", tp, state->res_var);
        emit("JUMPIFNEQ $if_widen_skip_%d %s string@int", id, tp);
        emit("INT2FLOAT %s %s", state->res_var, state->res_var);
        emit("LABEL $if_widen_skip_%d", id);
        free(tp);
    }
    free(state->res_var);
    state->res_var = NULL;
}

tCgWhileState cg_while_begin(void)
{
    tCgWhileState st;
    st.id = 0;
    if (!parser_codegen_enabled()) {
        return st;
    }
    st.id = cg_new_label_id();
    emit("LABEL $while_cond_%d", st.id);
    return st;
}

void cg_while_cond(tCgWhileState* state, tExpr* cond)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    char* vcond = cg_strdup(cg_new_temp());
    cg_emit_expr_into(cond, vcond);
    emit("JUMPIFNEQ $while_end_%d %s bool@true", state->id, vcond);
    free(vcond);
}

void cg_while_end(tCgWhileState* state)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    emit("JUMP $while_cond_%d", state->id);
    emit("LABEL $while_end_%d", state->id);
}

tCgForState cg_for_range_begin(const char* var_name, tExpr* start, tExpr* end, tExpr* step, bool inclusive)
{
    tCgForState st;
    memset(&st, 0, sizeof(st));
    if (!parser_codegen_enabled()) {
        return st;
    }
    st.id = cg_new_label_id();
    st.over_list = false;
    st.inclusive = inclusive;

    char* vstart = cg_strdup(cg_new_temp());
    st.v_end = cg_strdup(cg_new_temp());
    st.v_step = cg_strdup(cg_new_temp());

    cg_emit_expr_into(start, vstart);
    cg_emit_expr_into(end, st.v_end);
    if (step != NULL) {
        cg_emit_expr_into(step, st.v_step);
    } else {
        emit("MOVE %s int@1", st.v_step);
    }

    // Krok 0 je běhová chyba 40
    emit("JUMPIFEQ $__err_40 %s int@0", st.v_step);

    cg_scope_push();
    tTypeInfo int_t = { TYPE_INT, TYPE_UNIT };
    st.v_iter = cg_strdup(cg_define_var(var_name, int_t));
    emit("MOVE %s %s", st.v_iter, vstart);
    free(vstart);

    char* vpos = cg_strdup(cg_new_temp());
    char* vcmp = cg_strdup(cg_new_temp());

    emit("LABEL $for_cond_%d", st.id);
    emit("GT %s %s int@0", vpos, st.v_step);
    emit("JUMPIFNEQ $for_neg_%d %s bool@true", st.id, vpos);

    // Kladný krok
    if (inclusive) {
        emit("GT %s %s %s", vcmp, st.v_iter, st.v_end);
        emit("JUMPIFEQ $for_end_%d %s bool@true", st.id, vcmp);
    } else {
        emit("LT %s %s %s", vcmp, st.v_iter, st.v_end);
        emit("JUMPIFNEQ $for_end_%d %s bool@true", st.id, vcmp);
    }
    emit("JUMP $for_guards_%d", st.id);

    // Záporný krok
    emit("LABEL $for_neg_%d", st.id);
    if (inclusive) {
        emit("LT %s %s %s", vcmp, st.v_iter, st.v_end);
        emit("JUMPIFEQ $for_end_%d %s bool@true", st.id, vcmp);
    } else {
        emit("GT %s %s %s", vcmp, st.v_iter, st.v_end);
        emit("JUMPIFNEQ $for_end_%d %s bool@true", st.id, vcmp);
    }
    emit("LABEL $for_guards_%d", st.id);

    free(vpos);
    free(vcmp);
    return st;
}

tCgForState cg_for_list_begin(const char* var_name, tTypeInfo elem_type, tExpr* list_expr)
{
    tCgForState st;
    memset(&st, 0, sizeof(st));
    if (!parser_codegen_enabled()) {
        return st;
    }
    st.id = cg_new_label_id();
    st.over_list = true;

    st.v_list = cg_strdup(cg_new_temp());
    st.v_idx = cg_strdup(cg_new_temp());
    st.v_len = cg_strdup(cg_new_temp());

    cg_emit_expr_into(list_expr, st.v_list);
    emit("ARRAYGET %s %s int@0", st.v_len, st.v_list);
    emit("MOVE %s int@0", st.v_idx);

    cg_scope_push();
    st.v_iter = cg_strdup(cg_define_var(var_name, elem_type));

    char* vcmp = cg_strdup(cg_new_temp());
    char* vdata = cg_strdup(cg_new_temp());
    emit("LABEL $for_cond_%d", st.id);
    emit("LT %s %s %s", vcmp, st.v_idx, st.v_len);
    emit("JUMPIFNEQ $for_end_%d %s bool@true", st.id, vcmp);
    emit("ARRAYGET %s %s int@1", vdata, st.v_list);
    emit("ARRAYGET %s %s %s", st.v_iter, vdata, st.v_idx);
    free(vcmp);
    free(vdata);
    return st;
}

void cg_for_guard(tCgForState* state, tExpr* guard_expr)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    char* vg = cg_strdup(cg_new_temp());
    cg_emit_expr_into(guard_expr, vg);
    emit("JUMPIFNEQ $for_next_%d %s bool@true", state->id, vg);
    free(vg);
}

void cg_for_body_begin(tCgForState* state)
{
    (void)state;
}

void cg_for_end(tCgForState* state)
{
    if (!parser_codegen_enabled()) {
        return;
    }
    emit("LABEL $for_next_%d", state->id);
    if (state->over_list) {
        emit("ADD %s %s int@1", state->v_idx, state->v_idx);
    } else {
        emit("ADD %s %s %s", state->v_iter, state->v_iter, state->v_step);
    }
    emit("JUMP $for_cond_%d", state->id);
    emit("LABEL $for_end_%d", state->id);

    cg_scope_pop();
    free(state->v_iter);
    free(state->v_end);
    free(state->v_step);
    free(state->v_list);
    free(state->v_idx);
    free(state->v_len);
}

// --- Vestavěné pomocné podprogramy v IFJcode26 ---

static void emit_runtime_helpers(FILE* out)
{
    // Běhová chyba 40
    fprintf(out,
        "\nLABEL $__err_40\n"
        "EXIT int@40\n");

    // Prázdný seznam: [0, nil]
    fprintf(out,
        "\nLABEL $__list_empty\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 int@0\n"
        "ARRAYSET LF@%%ret int@1 nil@nil\n"
        "POPFRAME\n"
        "RETURN\n");

    // Slice seznamu: TF@%p0 = list, TF@%p1 = from, TF@%p2 = until
    fprintf(out,
        "\nLABEL $__list_slice\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "DEFVAR LF@len\n"
        "DEFVAR LF@lo\n"
        "DEFVAR LF@hi\n"
        "DEFVAR LF@nlen\n"
        "DEFVAR LF@cmp\n"
        "DEFVAR LF@src\n"
        "DEFVAR LF@dst\n"
        "DEFVAR LF@i\n"
        "DEFVAR LF@sidx\n"
        "DEFVAR LF@val\n"
        "ARRAYGET LF@len LF@%%p0 int@0\n"
        "MOVE LF@lo LF@%%p1\n"
        "LT LF@cmp LF@lo int@0\n"
        "JUMPIFNEQ $__sl_lo_ok LF@cmp bool@true\n"
        "MOVE LF@lo int@0\n"
        "LABEL $__sl_lo_ok\n"
        "MOVE LF@hi LF@%%p2\n"
        "GT LF@cmp LF@hi LF@len\n"
        "JUMPIFNEQ $__sl_hi_ok LF@cmp bool@true\n"
        "MOVE LF@hi LF@len\n"
        "LABEL $__sl_hi_ok\n"
        "SUB LF@nlen LF@hi LF@lo\n"
        "GT LF@cmp LF@nlen int@0\n"
        "JUMPIFEQ $__sl_nonempty LF@cmp bool@true\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 int@0\n"
        "ARRAYSET LF@%%ret int@1 nil@nil\n"
        "POPFRAME\n"
        "RETURN\n"
        "LABEL $__sl_nonempty\n"
        "ARRAYGET LF@src LF@%%p0 int@1\n"
        "NEWARRAY LF@dst LF@nlen\n"
        "MOVE LF@i int@0\n"
        "LABEL $__sl_loop\n"
        "LT LF@cmp LF@i LF@nlen\n"
        "JUMPIFNEQ $__sl_done LF@cmp bool@true\n"
        "ADD LF@sidx LF@lo LF@i\n"
        "ARRAYGET LF@val LF@src LF@sidx\n"
        "ARRAYSET LF@dst LF@i LF@val\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__sl_loop\n"
        "LABEL $__sl_done\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 LF@nlen\n"
        "ARRAYSET LF@%%ret int@1 LF@dst\n"
        "POPFRAME\n"
        "RETURN\n");

    // Konkatenace seznamů: TF@%p0 = list1, TF@%p1 = list2
    fprintf(out,
        "\nLABEL $__list_concat\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "DEFVAR LF@l1\n"
        "DEFVAR LF@l2\n"
        "DEFVAR LF@tot\n"
        "DEFVAR LF@cmp\n"
        "DEFVAR LF@d1\n"
        "DEFVAR LF@d2\n"
        "DEFVAR LF@dst\n"
        "DEFVAR LF@i\n"
        "DEFVAR LF@j\n"
        "DEFVAR LF@val\n"
        "ARRAYGET LF@l1 LF@%%p0 int@0\n"
        "ARRAYGET LF@l2 LF@%%p1 int@0\n"
        "ADD LF@tot LF@l1 LF@l2\n"
        "GT LF@cmp LF@tot int@0\n"
        "JUMPIFEQ $__cc_nonempty LF@cmp bool@true\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 int@0\n"
        "ARRAYSET LF@%%ret int@1 nil@nil\n"
        "POPFRAME\n"
        "RETURN\n"
        "LABEL $__cc_nonempty\n"
        "NEWARRAY LF@dst LF@tot\n"
        "JUMPIFEQ $__cc_part2 LF@l1 int@0\n"
        "ARRAYGET LF@d1 LF@%%p0 int@1\n"
        "MOVE LF@i int@0\n"
        "LABEL $__cc_loop1\n"
        "LT LF@cmp LF@i LF@l1\n"
        "JUMPIFNEQ $__cc_part2 LF@cmp bool@true\n"
        "ARRAYGET LF@val LF@d1 LF@i\n"
        "ARRAYSET LF@dst LF@i LF@val\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__cc_loop1\n"
        "LABEL $__cc_part2\n"
        "JUMPIFEQ $__cc_done LF@l2 int@0\n"
        "ARRAYGET LF@d2 LF@%%p1 int@1\n"
        "MOVE LF@i int@0\n"
        "LABEL $__cc_loop2\n"
        "LT LF@cmp LF@i LF@l2\n"
        "JUMPIFNEQ $__cc_done LF@cmp bool@true\n"
        "ARRAYGET LF@val LF@d2 LF@i\n"
        "ADD LF@j LF@l1 LF@i\n"
        "ARRAYSET LF@dst LF@j LF@val\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__cc_loop2\n"
        "LABEL $__cc_done\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 LF@tot\n"
        "ARRAYSET LF@%%ret int@1 LF@dst\n"
        "POPFRAME\n"
        "RETURN\n");

    // Strukturální rovnost seznamů: TF@%p0 = list1, TF@%p1 = list2
    fprintf(out,
        "\nLABEL $__list_eq\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "DEFVAR LF@l1\n"
        "DEFVAR LF@l2\n"
        "DEFVAR LF@d1\n"
        "DEFVAR LF@d2\n"
        "DEFVAR LF@i\n"
        "DEFVAR LF@cmp\n"
        "DEFVAR LF@v1\n"
        "DEFVAR LF@v2\n"
        "JUMPIFEQ $__leq_true LF@%%p0 LF@%%p1\n"
        "ARRAYGET LF@l1 LF@%%p0 int@0\n"
        "ARRAYGET LF@l2 LF@%%p1 int@0\n"
        "JUMPIFNEQ $__leq_false LF@l1 LF@l2\n"
        "JUMPIFEQ $__leq_true LF@l1 int@0\n"
        "ARRAYGET LF@d1 LF@%%p0 int@1\n"
        "ARRAYGET LF@d2 LF@%%p1 int@1\n"
        "MOVE LF@i int@0\n"
        "LABEL $__leq_loop\n"
        "LT LF@cmp LF@i LF@l1\n"
        "JUMPIFNEQ $__leq_true LF@cmp bool@true\n"
        "ARRAYGET LF@v1 LF@d1 LF@i\n"
        "ARRAYGET LF@v2 LF@d2 LF@i\n"
        "JUMPIFNEQ $__leq_false LF@v1 LF@v2\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__leq_loop\n"
        "LABEL $__leq_true\n"
        "MOVE LF@%%ret bool@true\n"
        "POPFRAME\n"
        "RETURN\n"
        "LABEL $__leq_false\n"
        "MOVE LF@%%ret bool@false\n"
        "POPFRAME\n"
        "RETURN\n");

    // Rozsah / List.range: TF@%p0 = start, TF@%p1 = end, TF@%p2 = step, TF@%p3 = inclusive
    fprintf(out,
        "\nLABEL $__list_range\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "DEFVAR LF@cur\n"
        "DEFVAR LF@cnt\n"
        "DEFVAR LF@pos\n"
        "DEFVAR LF@cmp\n"
        "DEFVAR LF@dst\n"
        "DEFVAR LF@i\n"
        "JUMPIFEQ $__err_40 LF@%%p2 int@0\n"
        "MOVE LF@cur LF@%%p0\n"
        "MOVE LF@cnt int@0\n"
        "GT LF@pos LF@%%p2 int@0\n"
        "LABEL $__rng_cnt_loop\n"
        "JUMPIFNEQ $__rng_cnt_neg LF@pos bool@true\n"
        "JUMPIFEQ $__rng_cnt_pos_inc LF@%%p3 bool@true\n"
        "LT LF@cmp LF@cur LF@%%p1\n"
        "JUMPIFNEQ $__rng_alloc LF@cmp bool@true\n"
        "JUMP $__rng_cnt_step\n"
        "LABEL $__rng_cnt_pos_inc\n"
        "GT LF@cmp LF@cur LF@%%p1\n"
        "JUMPIFEQ $__rng_alloc LF@cmp bool@true\n"
        "JUMP $__rng_cnt_step\n"
        "LABEL $__rng_cnt_neg\n"
        "JUMPIFEQ $__rng_cnt_neg_inc LF@%%p3 bool@true\n"
        "GT LF@cmp LF@cur LF@%%p1\n"
        "JUMPIFNEQ $__rng_alloc LF@cmp bool@true\n"
        "JUMP $__rng_cnt_step\n"
        "LABEL $__rng_cnt_neg_inc\n"
        "LT LF@cmp LF@cur LF@%%p1\n"
        "JUMPIFEQ $__rng_alloc LF@cmp bool@true\n"
        "LABEL $__rng_cnt_step\n"
        "ADD LF@cnt LF@cnt int@1\n"
        "ADD LF@cur LF@cur LF@%%p2\n"
        "JUMP $__rng_cnt_loop\n"
        "LABEL $__rng_alloc\n"
        "GT LF@cmp LF@cnt int@0\n"
        "JUMPIFEQ $__rng_fill LF@cmp bool@true\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 int@0\n"
        "ARRAYSET LF@%%ret int@1 nil@nil\n"
        "POPFRAME\n"
        "RETURN\n"
        "LABEL $__rng_fill\n"
        "NEWARRAY LF@dst LF@cnt\n"
        "MOVE LF@cur LF@%%p0\n"
        "MOVE LF@i int@0\n"
        "LABEL $__rng_fill_loop\n"
        "LT LF@cmp LF@i LF@cnt\n"
        "JUMPIFNEQ $__rng_done LF@cmp bool@true\n"
        "ARRAYSET LF@dst LF@i LF@cur\n"
        "ADD LF@cur LF@cur LF@%%p2\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__rng_fill_loop\n"
        "LABEL $__rng_done\n"
        "NEWARRAY LF@%%ret int@2\n"
        "ARRAYSET LF@%%ret int@0 LF@cnt\n"
        "ARRAYSET LF@%%ret int@1 LF@dst\n"
        "POPFRAME\n"
        "RETURN\n");

    // Převod seznamu na řetězec: TF@%p0 = list, TF@%p1 = elem_type_tag
    fprintf(out,
        "\nLABEL $__list_to_str\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "DEFVAR LF@len\n"
        "DEFVAR LF@data\n"
        "DEFVAR LF@i\n"
        "DEFVAR LF@cmp\n"
        "DEFVAR LF@el\n"
        "DEFVAR LF@s\n"
        "ARRAYGET LF@len LF@%%p0 int@0\n"
        "MOVE LF@%%ret string@List(\n"
        "JUMPIFEQ $__lts_done LF@len int@0\n"
        "ARRAYGET LF@data LF@%%p0 int@1\n"
        "MOVE LF@i int@0\n"
        "LABEL $__lts_loop\n"
        "LT LF@cmp LF@i LF@len\n"
        "JUMPIFNEQ $__lts_done LF@cmp bool@true\n"
        "JUMPIFEQ $__lts_first LF@i int@0\n"
        "CONCAT LF@%%ret LF@%%ret string@,\\032\n"
        "LABEL $__lts_first\n"
        "ARRAYGET LF@el LF@data LF@i\n"
        "JUMPIFEQ $__lts_int LF@%%p1 string@int\n"
        "JUMPIFEQ $__lts_flt LF@%%p1 string@float\n"
        "JUMPIFEQ $__lts_str LF@%%p1 string@string\n"
        "JUMPIFEQ $__lts_bool LF@%%p1 string@bool\n"
        "MOVE LF@s string@()\n"
        "JUMP $__lts_append\n"
        "LABEL $__lts_int\n"
        "INT2STR LF@s LF@el\n"
        "JUMP $__lts_append\n"
        "LABEL $__lts_flt\n"
        "FLOAT2STR LF@s LF@el\n"
        "JUMP $__lts_append\n"
        "LABEL $__lts_str\n"
        "MOVE LF@s LF@el\n"
        "JUMP $__lts_append\n"
        "LABEL $__lts_bool\n"
        "JUMPIFEQ $__lts_bt LF@el bool@true\n"
        "MOVE LF@s string@false\n"
        "JUMP $__lts_append\n"
        "LABEL $__lts_bt\n"
        "MOVE LF@s string@true\n"
        "LABEL $__lts_append\n"
        "CONCAT LF@%%ret LF@%%ret LF@s\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__lts_loop\n"
        "LABEL $__lts_done\n"
        "CONCAT LF@%%ret LF@%%ret string@)\n"
        "POPFRAME\n"
        "RETURN\n");

    // subStr(s, start, end): TF@%p0 = s, TF@%p1 = start, TF@%p2 = end
    fprintf(out,
        "\nLABEL $__substr\n"
        "PUSHFRAME\n"
        "DEFVAR LF@%%ret\n"
        "DEFVAR LF@len\n"
        "DEFVAR LF@cmp\n"
        "DEFVAR LF@i\n"
        "DEFVAR LF@ch\n"
        "STRLEN LF@len LF@%%p0\n"
        "LT LF@cmp LF@%%p1 int@0\n"
        "JUMPIFEQ $__err_40 LF@cmp bool@true\n"
        "LT LF@cmp LF@%%p2 int@0\n"
        "JUMPIFEQ $__err_40 LF@cmp bool@true\n"
        "GT LF@cmp LF@%%p1 LF@%%p2\n"
        "JUMPIFEQ $__err_40 LF@cmp bool@true\n"
        "GT LF@cmp LF@%%p1 LF@len\n"
        "JUMPIFEQ $__err_40 LF@cmp bool@true\n"
        "GT LF@cmp LF@%%p2 LF@len\n"
        "JUMPIFEQ $__err_40 LF@cmp bool@true\n"
        "MOVE LF@%%ret string@\n"
        "MOVE LF@i LF@%%p1\n"
        "LABEL $__sub_loop\n"
        "LT LF@cmp LF@i LF@%%p2\n"
        "JUMPIFNEQ $__sub_done LF@cmp bool@true\n"
        "GETCHAR LF@ch LF@%%p0 LF@i\n"
        "CONCAT LF@%%ret LF@%%ret LF@ch\n"
        "ADD LF@i LF@i int@1\n"
        "JUMP $__sub_loop\n"
        "LABEL $__sub_done\n"
        "POPFRAME\n"
        "RETURN\n");
}

void cg_emit_program(FILE* out)
{
    if (out == NULL) {
        return;
    }
    fprintf(out, ".IFJcode26\n");
    fprintf(out, "JUMP $__main_entry\n");

    emit_runtime_helpers(out);

    if (cg.funcs_buf.data != NULL) {
        fwrite(cg.funcs_buf.data, 1, cg.funcs_buf.len, out);
    }

    fprintf(out, "\nLABEL $__main_entry\n");
    if (cg.main_func_name != NULL) {
        fprintf(out, "CREATEFRAME\n");
        fprintf(out, "CALL $%s\n", cg.main_func_name);
    }
    fprintf(out, "EXIT int@0\n");
}