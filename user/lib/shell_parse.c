#include "shell.h"
static void empty(ShCommand *c,int when) {
    c->argc=0;c->argv[0]=0;c->when=when;c->append=0;c->here_fd= -1;
    c->input=c->output=c->delimiter=0;
}
static void finish(ShCommand *c) {
    char *p=c->argv[0];
    c->argv[c->argc]=0;
    if(c->delimiter && !c->output && c->argc==2 &&
       p[0]=='c' && p[1]=='a' && p[2]=='t' && !p[3]) {
        c->output=c->argv[1];c->argc=1;c->argv[1]=0;
    }
}
int sh_parse(char *line,char *storage,int capacity,ShList *out) {
    char *word[64],*p=line,quote,ch;int kind[64],n=0,used=0,op,i,here=0;
    ShCommand *c;
    out->count=0;
    while(*p) {
        while(*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')p++;
        if(!*p)break;
        if(n==64 || used>=capacity)goto fail;
        word[n]=storage+used;kind[n]=0;
        if(*p=='<' || *p=='>') {
            ch=*p++;kind[n]=ch=='<'?1:3;if(*p==ch){p++;kind[n]++;}
        } else if(*p=='&' || *p=='|') {
            ch=*p++;if(*p++!=ch)goto fail;kind[n]=ch=='&'?5:6;
        } else if(*p==';'){p++;kind[n]=7;}
        else while(*p && *p!=' ' && *p!='\t' && *p!='\n' && *p!='\r' &&
                   *p!='<' && *p!='>' && *p!='&' && *p!='|' && *p!=';') {
            if(*p=='\'' || *p=='"') {
                quote=*p++;while(*p && *p!=quote) {
                    if(used+1>=capacity)goto fail;
                    storage[used++]=*p++;
                }
                if(!*p)goto fail;
                p++;
            } else {
                if(*p=='\\'){p++;if(!*p || *p=='\n')goto fail;}
                if(used+1>=capacity)goto fail;
                storage[used++]=*p++;
            }
        }
        storage[used++]=0;n++;
    }
    if(!n)return 0;
    out->count=1;c=&out->command[0];empty(c,SH_ALWAYS);
    for(i=0;i<n;i++) {
        op=kind[i];
        if(!op){if(c->argc==32)goto fail;c->argv[c->argc++]=word[i];}
        else if(op<=4) {
            if(i+1==n || kind[i+1])goto fail;
            if(op<=2) {
                if(c->input || c->delimiter)goto fail;
                if(op==1)c->input=word[++i];
                else {if(++here>8)goto fail;c->delimiter=word[++i];}
            } else {if(c->output)goto fail;c->output=word[++i];c->append=op==4;}
        } else {
            if(!c->argc)goto fail;
            finish(c);
            if(i+1==n){if(op!=7)goto fail;return 0;}
            if(out->count==SH_COMMANDS)goto fail;
            c=&out->command[out->count++];empty(c,op==5?SH_AND:(op==6?SH_OR:SH_ALWAYS));
        }
    }
    if(!c->argc)goto fail;
    finish(c);return 0;
fail:out->count=0;return -1;
}
