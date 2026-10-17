#include "stage_io.h"
#include "symcc.h"
extern unsigned sp_heap_peak(void);
int main(int argc,char **argv) {
    char *source;Token *tokens;Program *program;Obj *obj;FILE *out;int failed;
    if(argc!=3 && !(argc==4 && !strcmp(argv[3],"--stats"))){fprintf(stderr,"usage: scc1 input.i output.sym\n");return 2;}
    source=stage_read(argv[1]);tokens=tokenize(source);program=parse(tokens);obj=obj_new();
    if(!codegen(program,obj,true))return 1;
    out=fopen(argv[2],"wb");if(!out){fprintf(stderr,"cannot write %s\n",argv[2]);return 1;}
    failed=!obj_write(obj,out);if(fclose(out))failed=1;
    if(argc==4)fprintf(stderr,"scc1 heap_peak=%u\n",sp_heap_peak());return failed;
}
