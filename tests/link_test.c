/* tests/link_test.c — 对象格式与链接器单测（M2 Task 5 起） */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symcc/src/obj.h"
#include "symcc/src/symcc.h"
#include "symcc/src/link.h"
#include "emu/asm.h"
#include "emu/emu.h"

static int npass = 0, nfail = 0;
#define CHECK(cond) do { if (cond) { npass++; } else { nfail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_line_sizes(void) {
    CHECK(obj_line_size("    nop") == 4);
    CHECK(obj_line_size("    push r1") == 8);
    CHECK(obj_line_size("    call foo") == 20);
    CHECK(obj_line_size("    ret") == 12);
    CHECK(obj_line_size("    pop r2") == 8);
    CHECK(obj_line_size("    U32 0x10") == 4);
    CHECK(obj_line_size("    U16 0x20") == 2);
    CHECK(obj_line_size("    U8 0x30") == 1);
    CHECK(obj_line_size("    \"abc\"") == 3);
    CHECK(obj_line_size("main:") == 0);
    CHECK(obj_line_size("    ; comment") == 0);
    CHECK(obj_line_size("") == 0);
    CHECK(obj_label_name_len("main:") == 4);
    CHECK(obj_label_name_len("    mov r1, 2") == 0);
    CHECK(obj_label_name_len("L12:") == 3);
}

static void test_obj_roundtrip(void) {
    Obj *o = obj_new();
    obj_add_text(o, "main:");
    obj_add_text(o, "    mov r1, @hi:g");
    obj_add_data(o, "g:");
    obj_add_data(o, "    U32 0x1234");
    obj_add_bss(o, "buf", 100);
    obj_add_symbol(o, "main", false, 0, 0, 4);
    obj_add_symbol(o, "g", true, 1, 0, 4);
    FILE *f = tmpfile();
    CHECK(obj_write(o, f));
    rewind(f);
    Obj *o2 = obj_new();
    CHECK(obj_read(o2, f));
    fclose(f);
    CHECK(o2->n_text == 2 && o2->n_data == 2);
    CHECK(strstr(o2->text_lines[1], "@hi:g") != NULL);
    CHECK(strcmp(o2->data_lines[0], "g:") == 0);
    CHECK(o2->bss && strcmp(o2->bss->name, "buf") == 0 && o2->bss->size == 100);
    CHECK(o2->syms && strcmp(o2->syms->name, "main") == 0 && !o2->syms->is_data);
    obj_free(o); obj_free(o2);
}

/* codegen 输出形态：符号行 + 数据引用形态。
 * 引用形态按符号类别：bss（本文件、未初始化）→ D16 @name；data 变量 /
 * extern / 函数 → D32 拆装 @hi:/@lo:（链接布局 data/text > 64KB，
 * D16 imm16 溢出）。d32=true 强制全部 D32。 */
static void test_codegen_obj_form(void) {
    Token *tok = tokenize(
        "int g;\n"                        /* bss → D16 */
        "int h = 5;\n"                    /* data → D32 */
        "int f(int a) { return a + g + h; }\n"
        "int main(void) { return f(1); }\n");
    Program *prog = parse(tok);
    Obj *o = obj_new();
    CHECK(codegen(prog, o, false));
    ObjSymbol *s;
    for (s = o->syms; s; s = s->next)
        if (strcmp(s->name, "main") == 0) break;
    CHECK(s != NULL && !s->is_data);
    for (s = o->syms; s; s = s->next)
        if (strcmp(s->name, "g") == 0) break;
    CHECK(s != NULL && s->is_data);
    int ng16 = 0, nh32 = 0, i;
    for (i = 0; i < o->n_text; i++) {
        if (strstr(o->text_lines[i], "@g")) ng16++;
        if (strstr(o->text_lines[i], "@hi:h")) nh32++;
    }
    CHECK(ng16 > 0 && nh32 > 0);   /* bss→D16，data→D32 */
    Obj *o2 = obj_new();
    CHECK(codegen(prog, o2, true));
    int ng32 = 0;
    for (i = 0; i < o2->n_text; i++)
        if (strstr(o2->text_lines[i], "@hi:g")) ng32++;
    CHECK(ng32 > 0);               /* d32=true 强制全部 D32 */
    obj_free(o); obj_free(o2);
}

/* 两个对象：a 定义 f（引用全局 g），b 定义 main → 链接输出绝对 asm */
static void test_link_two_objects(void) {
    char *src_a =
        "int g;\n"                       /* bss → g */
        "int f(int x) { return x + g; }\n";
    char *src_b =
        "int main(void) { return f(40); }\n";
    Token *ta = tokenize(src_a);
    Token *tb = tokenize(src_b);
    Program *pa = parse(ta);
    Program *pb = parse(tb);
    Obj *oa = obj_new(), *ob = obj_new();
    CHECK(codegen(pa, oa, false));
    CHECK(codegen(pb, ob, false));
    Obj *objs[2] = { oa, ob };
    FILE *out = tmpfile();
    LinkError err;
    CHECK(symld_link(objs, 2, "runtime/crt0.asm", out, NULL, &err));
    /* 输出为绝对 asm：g 的数据引用已解析为数字（无 @g 残留） */
    long len = ftell(out);
    CHECK(len>0);
    if(len<=0){fclose(out);obj_free(oa);obj_free(ob);return;}
    char *buf=malloc((size_t)len+1);
    CHECK(buf!=NULL);
    if(!buf){fclose(out);obj_free(oa);obj_free(ob);return;}
    rewind(out);
    CHECK(len == (long)fread(buf, 1, (size_t)len, out));
    buf[len] = 0;
    fclose(out);
    CHECK(strstr(buf, "@g") == NULL);
    CHECK(strstr(buf, "call f") != NULL);      /* 全局函数保留 label */
    CHECK(strstr(buf, "main:") != NULL);
    CHECK(strstr(buf, "halt:") != NULL);
    CHECK(strstr(buf, "__bss_start") == NULL); /* 内置符号已解析 */
    free(buf);
    obj_free(oa); obj_free(ob);
}

/* 未定义符号 → 错误信息含符号名（main 已定义时 g 是首个未定义符号） */
static void test_link_errors(void) {
    Token *t = tokenize(
        "int f(int x) { return g(); }\n"
        "int main(void) { return f(0); }\n");
    Program *p = parse(t);
    Obj *o = obj_new();
    CHECK(codegen(p, o, false));
    Obj *objs[1] = { o };
    FILE *out = tmpfile();
    LinkError err;
    CHECK(!symld_link(objs, 1, "runtime/crt0.asm", out, NULL, &err));
    CHECK(strstr(err.msg, "g") != NULL);
    fclose(out);
    obj_free(o);
}

/* 缺 main：crt0 的 call main 解析失败 → 链接错误含 "main" */
static void test_link_no_main(void) {
    Token *t = tokenize("int f(void) { return 1; }\n");
    Program *p = parse(t);
    Obj *o = obj_new();
    CHECK(codegen(p, o, false));
    Obj *objs[1] = { o };
    FILE *out = tmpfile();
    LinkError err;
    CHECK(!symld_link(objs, 1, "runtime/crt0.asm", out, NULL, &err));
    CHECK(strstr(err.msg, "main") != NULL);
    fclose(out);
    obj_free(o);
}

/* bss 清零端到端：链接 → 汇编 → 模拟，未初始化全局读为 0 */
static void test_link_bss_cleared(void) {
    char *src = "int counter;\nint main(void) { return counter; }\n";
    Token *t = tokenize(src);
    Program *p = parse(t);
    Obj *o = obj_new();
    CHECK(codegen(p, o, false));
    Obj *objs[1] = { o };
    FILE *out = tmpfile();
    LinkError err;
    CHECK(symld_link(objs, 1, "runtime/crt0.asm", out, NULL, &err));
    long len = ftell(out);
    CHECK(len>0);
    if(len<=0){fclose(out);obj_free(o);return;}
    char *text=malloc((size_t)len+1);
    size_t capacity=(size_t)len*2+64;
    uint8_t *bin=malloc(capacity);
    CHECK(text && bin);
    if(!text || !bin){free(text);free(bin);fclose(out);obj_free(o);return;}
    rewind(out);
    len = fread(text, 1, (size_t)len, out);
    text[len] = 0;
    fclose(out);
    AsmError aerr;
    int n = asm_assemble(text, bin, capacity, &aerr);
    CHECK(n > 0);
    if(n<=0){free(text);free(bin);obj_free(o);return;}
    EmuResult res = emu_run(bin, (size_t)n, 8 << 20, 100000000);
    CHECK(res.error == 0 && res.exit_code == 0);
    free(res.mem);
    free(res.disk);
    free(text);free(bin);
    obj_free(o);
}

int main(void) {
    {
        Obj *o=obj_new();FILE *fp=tmpfile();
        fputs(".text\r\ncrlf_entry:\r\n    ret\r\n.sym crlf_entry T text 0 0\r\n",fp);rewind(fp);
        CHECK(obj_read(o,fp));CHECK(o->n_text==2);
        fclose(fp);obj_free(o);
    }
    test_line_sizes();
    test_obj_roundtrip();
    test_codegen_obj_form();
    test_link_two_objects();
    test_link_errors();
    test_link_no_main();
    test_link_bss_cleared();
    printf("link_test: %d passed, %d failed\n", npass, nfail);
    return nfail ? 1 : 0;
}
