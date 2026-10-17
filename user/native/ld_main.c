#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link.h"
#include "runtime/platform.h"
extern unsigned sp_heap_peak(void);
int main(int argc,char **argv) {
    const char *paths[10]={"/lib/crt0.sym","/lib/syscall.sym","/lib/base.sym","/lib/heap.sym","/lib/stdio.sym","/lib/format.sym","/lib/string.sym","/lib/convert.sym","/lib/divsi3.sym",0};
    Obj *objects[10];int i,ok,failed;FILE *f;LinkOptions opt;LinkError error;
    if(argc!=3 && !(argc==4 && !strcmp(argv[3],"--stats"))){fprintf(stderr,"usage: sld input.sym output.spx\n");return 2;}
    paths[9]=argv[1];
    for(i=0;i<10;i++) {
        f=fopen(paths[i],"rb");if(!f){fprintf(stderr,"cannot open %s\n",paths[i]);return 1;}
        objects[i]=obj_new();ok=obj_read(objects[i],f);failed=fclose(f);
        if(!ok || failed){fprintf(stderr,"invalid object: %s\n",paths[i]);return 1;}
    }
    opt.base=SP_USER_BASE;opt.limit=SP_USER_BASE+SP_USER_SIZE-65536;
    opt.branch_gate=SP_BRANCH_GATE;opt.syscall_gate=SP_SYSCALL_GATE;opt.image_kind=SP_IMAGE_USER;
    f=fopen(argv[2],"wb");if(!f){fprintf(stderr,"cannot write %s\n",argv[2]);return 1;}
    ok=symld_link_ex(objects,10,0,&opt,0,f,&error);failed=fclose(f);
    if(!ok){fprintf(stderr,"sld: %s\n",error.msg);return 1;}
    if(failed){fprintf(stderr,"write failed: %s\n",argv[2]);return 1;}
    if(argc==4)fprintf(stderr,"sld heap_peak=%u\n",sp_heap_peak());return 0;
}
