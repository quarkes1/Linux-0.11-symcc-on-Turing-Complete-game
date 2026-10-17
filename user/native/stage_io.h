#ifndef SP_STAGE_IO_H
#define SP_STAGE_IO_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *stage_read(const char *path) {
    FILE *f=fopen(path,"rb");long n;char *text;
    if(!f){fprintf(stderr,"cannot open %s\n",path);exit(1);}
    if(fseek(f,0,SEEK_END) || (n=ftell(f))<0 || n>262144 || fseek(f,0,SEEK_SET)) {
        fprintf(stderr,"invalid or oversized input: %s\n",path);exit(1);
    }
    text=malloc(n+1);if(!text){fprintf(stderr,"out of memory\n");exit(1);}
    if(fread(text,1,n,f)!=(unsigned)n || fclose(f)){fprintf(stderr,"read failed: %s\n",path);exit(1);}
    text[n]=0;return text;
}
static void stage_write(const char *path,const char *text) {
    FILE *f=fopen(path,"wb");unsigned n=strlen(text);
    if(!f){fprintf(stderr,"cannot write %s\n",path);exit(1);}
    int failed=fwrite(text,1,n,f)!=n;
    if(fclose(f))failed=1;
    if(failed){fprintf(stderr,"write failed: %s\n",path);exit(1);}
}
#endif
