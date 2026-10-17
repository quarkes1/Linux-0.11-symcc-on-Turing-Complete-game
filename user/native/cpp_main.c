#include "stage_io.h"
#include "symcc.h"
extern unsigned sp_heap_peak(void);
int main(int argc,char **argv) {
    const char *dirs[16];int n=1,i,stats=0;char *text,*expanded;
    if(argc<3){fprintf(stderr,"usage: scpp input.c output.i [-I dir]\n");return 2;}
    dirs[0]="/include";
    for(i=3;i<argc;i++) {
        if(!strcmp(argv[i],"--stats")){stats=1;continue;}
        if(strcmp(argv[i],"-I") || i+1==argc || n==16)return 2;
        dirs[n++]=argv[++i];
    }
    text=stage_read(argv[1]);expanded=preprocess_text(text,argv[1],dirs,n,0,0);
    stage_write(argv[2],expanded);
    if(stats)fprintf(stderr,"scpp heap_peak=%u\n",sp_heap_peak());return 0;
}
