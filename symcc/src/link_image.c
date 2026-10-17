/* Flat images with a shared low conditional gateway. Legacy linker stays intact. */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link.h"
#include "emu/asm.h"
#include "emu/isa.h"
#include "runtime/platform.h"

typedef struct { char *name; unsigned addr; int owner, exported; } ImageSym;
typedef struct {
    ImageSym *syms; int ns, cap;
    char *out; size_t used, size;
    LinkError *err; int failed;
} Image;
static unsigned a4(unsigned x) { return (x+3u)&~3u; }
static void fail(Image *im,const char *fmt,...) {
    va_list ap; va_start(ap,fmt);
    vsnprintf(im->err->msg,sizeof im->err->msg,fmt,ap);
    va_end(ap); im->failed=1;
}
static void emit(Image *im,const char *fmt,...) {
    va_list ap; char text[4096]; int n;
    va_start(ap,fmt); n=vsnprintf(text,sizeof text,fmt,ap); va_end(ap);
    if(n<0 || n>=(int)sizeof text) {fail(im,"image line too long");return;}
    if(im->used+(size_t)n+1>im->size) {
        im->size=(im->used+n+1)*2;
        im->out=realloc(im->out,im->size);
        if(!im->out) { fprintf(stderr,"out of memory\n");exit(1); }
    }
    memcpy(im->out+im->used,text,n+1); im->used+=n;
}
static void add(Image *im,const char *name,unsigned addr,int owner,int exp) {
    for(int i=0;i<im->ns;i++)
        if(!strcmp(im->syms[i].name,name) &&
           (im->syms[i].owner==owner || (exp && im->syms[i].exported))) {
            fail(im,"duplicate image symbol: %s",name);return;
        }
    if(im->ns==im->cap) {
        im->cap=im->cap?im->cap*2:256;
        im->syms=realloc(im->syms,im->cap*sizeof *im->syms);
        if(!im->syms) exit(1);
    }
    ImageSym *s=&im->syms[im->ns++];
    s->name=malloc(strlen(name)+1); strcpy(s->name,name);
    s->addr=addr;s->owner=owner;s->exported=exp;
}
static unsigned resolve(Image *im,const char *name,int owner) {
    char *end; unsigned long v=strtoul(name,&end,0);
    if(*name && !*end) return (unsigned)v;
    for(int i=0;i<im->ns;i++)
        if(im->syms[i].owner==owner && !strcmp(im->syms[i].name,name)) return im->syms[i].addr;
    for(int i=0;i<im->ns;i++)
        if(im->syms[i].exported && !strcmp(im->syms[i].name,name)) return im->syms[i].addr;
    fail(im,"undefined image symbol: %s",name); return 0;
}
static int cond(const char *op) {
    const char *names[]={"je","jne","jb","jae","jbe","ja","jl","jge","jle","jg"};
    for(int i=0;i<10;i++) if(!strcmp(op,names[i])) return 1;
    return 0;
}
static int reg(const char *s) { return isa_reg_index(s)>=0; }
static void parts(const char *s,char op[32],char arg[512]) {
    op[0]=arg[0]=0; sscanf(s,"%31s %511[^ ;\t\r\n]",op,arg);
}
static unsigned line_size(const char *s) {
    char op[32],arg[512];parts(s,op,arg);
    if(cond(op))return 16;
    if(!strcmp(op,"call"))return 32;
    if(!strcmp(op,"jmp") && !reg(arg))return 16;
    return (unsigned)obj_line_size(s);
}
static void pad(Image *im,unsigned from,unsigned to) {
    if(to<from) {fail(im,"invalid image overlap");return;}
    while(from+4<=to) {emit(im,"    U32 0\n");from+=4;}
    while(from++<to)emit(im,"    U8 0\n");
}
static int exported(Obj *o,const char *name) {
    for(ObjSymbol *s=o->syms;s;s=s->next)if(!strcmp(s->name,name))return 1;
    return 0;
}
static void labels(Image *im,Obj *o,int owner,char **lines,int count,unsigned base,int text) {
    for(int i=0;i<count;i++) {
        size_t n=obj_label_name_len(lines[i]);
        if(n) {
            char name[512];
            if(n>=sizeof name){fail(im,"label too long");return;}
            memcpy(name,lines[i],n);name[n]=0;
            add(im,name,base,owner,exported(o,name));
        } else base+=text?line_size(lines[i]):(unsigned)obj_line_size(lines[i]);
    }
}
static void target(Image *im,unsigned addr) {
    emit(im,"    mov r13, 0x%x\n    lsl r13, r13, 16\n    or r13, r13, 0x%x\n",addr>>16,addr&65535);
}
static void line(Image *im,const char *s,int owner,const LinkOptions *opt) {
    char op[32],arg[512]; parts(s,op,arg);
    if(obj_label_name_len(s)) {
        if(!strcmp(op,"main:"))emit(im,"main:\n");
        return;
    }
    if(cond(op)) {
        target(im,resolve(im,arg,owner));
        emit(im,"    %s 0x%x\n",op,opt->branch_gate);return;
    }
    if(!strcmp(op,"call")) {
        emit(im,"    counter flags\n    add flags, flags, 32\n    sub sp, sp, 4\n    store_32 [sp], flags\n");
        target(im,resolve(im,arg,owner));emit(im,"    jmp r13\n");return;
    }
    if(!strcmp(op,"jmp") && !reg(arg)) {
        target(im,resolve(im,arg,owner));emit(im,"    jmp r13\n");return;
    }
    /* Data relocations and immediate references; labels themselves never reach asm. */
    if(*s==';' || *s=='"' || !*s) {emit(im,"%s\n",s);return;}
    char out[4096];size_t at=0;
    while(*s && at+64<sizeof out) {
        if(*s==';'){break;}
        if(*s!='@') {out[at++]=*s++;continue;}
        s++; int mode=0;
        if(!strncmp(s,"hi:",3)){mode=1;s+=3;}
        else if(!strncmp(s,"lo:",3)){mode=2;s+=3;}
        else if(!strncmp(s,"32:",3)){mode=3;s+=3;}
        char name[512];size_t n=0;
        while((isalnum((unsigned char)*s)||*s=='_')&&n+1<sizeof name)name[n++]=*s++;
        name[n]=0;unsigned value=resolve(im,name,owner);
        const char *p=s;while(*p==' ')p++;
        if(*p=='+') {
            char *end;unsigned long v=strtoul(p+1,&end,0);
            if(end!=p+1){value+=(unsigned)v;s=end;}
        }
        if(mode==1)value>>=16;
        if(mode==2)value&=65535;
        if(mode==0 && value>65535){fail(im,"D16 image reference %s needs --d32",name);return;}
        at+=(size_t)snprintf(out+at,sizeof out-at,"0x%x",value);
    }
    out[at]=0;emit(im,"%s\n",out);
}
static void be32(FILE *f,unsigned v) {
    unsigned char b[4]={(unsigned char)(v>>24),(unsigned char)(v>>16),(unsigned char)(v>>8),(unsigned char)v};
    fwrite(b,1,4,f);
}
bool symld_link_ex(Obj **objs,int n,const char *crt0,const LinkOptions *opt,FILE *out,FILE *bin,LinkError *err) {
    if(!opt || opt->image_kind==SP_IMAGE_LEGACY)return symld_link(objs,n,crt0,out,bin,err);
    Image im={0};im.err=err;
    Obj *start=obj_new(); Obj **all=calloc(n+1,sizeof *all);
    unsigned *ts=calloc(n+1,sizeof *ts),*ds=calloc(n+1,sizeof *ds);
    unsigned base=opt->image_kind==SP_IMAGE_USER?opt->base:0x3000u;
    unsigned pos=base,bss,bssend,loaded;
    int ok=0;
    if(!all||!ts||!ds){fail(&im,"out of memory");goto done;}
    if(crt0) {
        FILE *f=fopen(crt0,"rb");char s[1024];
        if(!f){fail(&im,"cannot open crt0: %s",crt0);goto done;}
        while(fgets(s,sizeof s,f)) {
            s[strcspn(s,"\r\n")]=0;
            if(s[0]=='.')continue;
            char *ph=strstr(s,"FRAMEBUF_BASE");
            if(ph) {char temp[1024];snprintf(temp,sizeof temp,"%.*s0x2000%s",(int)(ph-s),s,ph+13);strcpy(s,temp);}
            obj_add_text(start,s);
        } fclose(f);
    }
    all[0]=start;for(int i=0;i<n;i++)all[i+1]=objs[i];n++;
    for(int i=0;i<n;i++) {
        ts[i]=pos;
        labels(&im,all[i],i,all[i]->text_lines,all[i]->n_text,pos,1);
        for(int k=0;k<all[i]->n_text;k++)pos+=line_size(all[i]->text_lines[k]);
    }
    /* A real self-jump after crt0 is not needed: the crt0 source owns its stop label. */
    pos=a4(pos);
    for(int i=0;i<n;i++) {
        ds[i]=pos;
        labels(&im,all[i],i,all[i]->data_lines,all[i]->n_data,pos,0);
        for(int k=0;k<all[i]->n_data;k++)pos+=(unsigned)obj_line_size(all[i]->data_lines[k]);
    }
    loaded=pos;bss=a4(pos);pos=bss;
    for(int i=0;i<n;i++)for(ObjBss *b=all[i]->bss;b;b=b->next) {
        add(&im,b->name,pos,i,exported(all[i],b->name));pos+=a4((unsigned)b->size);
    }
    bssend=pos;
    add(&im,"__bss_start",bss,-1,1);add(&im,"__bss_end",bssend,-1,1);
    add(&im,"end",bssend,-1,1);
    add(&im,"__syscall_gate",opt->syscall_gate,-1,1);
    if(bssend>opt->limit || base>=opt->limit) {fail(&im,"image exceeds address window");goto done;}
    if(opt->image_kind==SP_IMAGE_KERNEL) {
        emit(&im,"    jmp 0x3000\n");
        pad(&im,4,opt->branch_gate);
        emit(&im,"    jmp r13\n");
        unsigned entry=0x140;
        for(int k=0;k<im.ns;k++)if(im.syms[k].exported && !strcmp(im.syms[k].name,"sp_syscall_entry"))entry=im.syms[k].addr;
        target(&im,entry);emit(&im,"    jmp r13\n");
        pad(&im,opt->syscall_gate+16,0x140);
        emit(&im,"halt:\n    jmp 0x140\n");
        pad(&im,0x144,base);
    }
    pos=base;
    for(int i=0;i<n;i++)for(int k=0;k<all[i]->n_text;k++) {
        line(&im,all[i]->text_lines[k],i,opt);pos+=line_size(all[i]->text_lines[k]);
    }
    pad(&im,pos,a4(pos));pos=a4(pos);
    for(int i=0;i<n;i++)for(int k=0;k<all[i]->n_data;k++) {
        line(&im,all[i]->data_lines[k],i,opt);pos+=(unsigned)obj_line_size(all[i]->data_lines[k]);
    }
    /* Kernel BSS occupies real bytes; user BSS is cleared by exec. */
    if(opt->image_kind==SP_IMAGE_KERNEL)pad(&im,pos,bssend);
    if(im.failed)goto done;
    if(out)fwrite(im.out,1,im.used,out);
    if(bin) {
        AsmError ae;size_t cap=im.used+65536;uint8_t *bytes=malloc(cap);
        int len=asm_assemble(im.out,bytes,cap,&ae);
        if(len<0){fail(&im,"image asm line %d: %s",ae.line,ae.msg);free(bytes);goto done;}
        if(opt->image_kind==SP_IMAGE_USER) {
            be32(bin,SP_EXEC_MAGIC);be32(bin,1);
            be32(bin,resolve(&im,"_start",0)-base);
            be32(bin,loaded-base);be32(bin,bssend-loaded);be32(bin,65536);be32(bin,0);
        }
        fwrite(bytes,1,(size_t)len,bin);free(bytes);
    }
    ok=!im.failed;
done:
    for(int i=0;i<im.ns;i++)free(im.syms[i].name);
    free(im.syms);free(im.out);free(all);free(ts);free(ds);obj_free(start);
    return ok;
}
