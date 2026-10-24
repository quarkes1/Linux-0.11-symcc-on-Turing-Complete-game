#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"
#include "runtime/platform.h"
static unsigned char signature[3][32];
static unsigned long long counts[3][SP_USER_SIZE/4],totals[4],kernel;
static FILE *keys;
void profile_pc(unsigned pc,const EmuResult *r,unsigned long long n) {
    (void)n;
    if(pc<SP_USER_BASE){kernel++;return;}
    if(pc>=SP_USER_BASE+SP_USER_SIZE)return;
    int id=3;
    for(int i=0;i<3;i++)if(!memcmp(r->mem+SP_USER_BASE,signature[i],32)){id=i;break;}
    totals[id]++;if(id<3)counts[id][(pc-SP_USER_BASE)/4]++;
}
static unsigned keyboard(void *p){(void)p;int c=fgetc(keys);return c==EOF?0:0x100u|(unsigned char)c;}
static int observe(const EmuResult *r,uint64_t n,void *p){(void)n;(void)p;char text[3841];memcpy(text,r->mem+0x2000,3840);text[3840]=0;if(strstr(text,"symcc: compilation failed"))return 1;for(int row=0;row<40;row++)if(!memcmp(r->mem+0x2000+row*96,"test ",5))return 1;return 0;}
static unsigned char *load(char *path,size_t *n){FILE*f=fopen(path,"rb");if(!f)exit(2);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);unsigned char*b=malloc(*n);fread(b,1,*n,f);fclose(f);return b;}
typedef struct {char name[100];unsigned long long n;} Row;
static Row rows[5000];static int nr;
static int compare(const void*a,const void*b){const Row*x=a,*y=b;return x->n<y->n?1:x->n>y->n?-1:0;}
int main(void) {
    char *stages[]={"scpp","scc1","sld"},path[128];size_t bn,dn;
    for(int i=0;i<3;i++){sprintf(path,"build/user/%s.spx",stages[i]);unsigned char*b=load(path,&bn);memcpy(signature[i],b+28,32);free(b);}
    unsigned char*b=load("build/kernel.bin",&bn),*disk=load("build/rootfs.bin",&dn);
    keys=fopen("build/profile.keys","rb");EmuOptions o={0};o.disk=disk;o.disk_len=dn;o.keyboard=keyboard;o.observe=observe;
    EmuResult r=emu_run_ex(b,bn,SP_RAM_BYTES,20000000000ULL,&o);
    printf("stop=%d error=%d instructions=%llu\n",r.stop_reason,r.error,(unsigned long long)r.instructions);
    for(int row=0;row<24;row++){fwrite(r.mem+0x2000+row*96,1,96,stdout);putchar('\n');}
    for(int i=0;i<4;i++)printf("%s: %llu\n",i<3?stages[i]:"other",totals[i]*1024);
    printf("kernel: %llu\n",kernel*1024);
    FILE*f=fopen("build/profile.map","r");int id;unsigned begin,end;char name[100];
    while(fscanf(f,"%d %x %x %99s",&id,&begin,&end,name)==4){unsigned long long n=0;for(unsigned a=begin;a<end && a<SP_USER_BASE+SP_USER_SIZE;a+=4)n+=counts[id][(a-SP_USER_BASE)/4];if(n){snprintf(rows[nr].name,100,"%s/%s",stages[id],name);rows[nr++].n=n*1024;}}
    qsort(rows,nr,sizeof(Row),compare);for(int i=0;i<25 && i<nr;i++)printf("%12llu %s\n",rows[i].n,rows[i].name);
    fclose(f);fclose(keys);free(b);free(disk);free(r.mem);free(r.disk);return r.stop_reason==1 && !r.error?0:1;
}
