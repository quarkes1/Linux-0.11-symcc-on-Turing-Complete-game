/* Host front end: render guest RAM; never interpret guest commands. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <conio.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#include <sys/select.h>
#endif
#include "emu/emu.h"
typedef struct {
    char screen[3841];
    const char *until;
    int interactive,quit,terminal,shift_pending;
    unsigned char queued;
    FILE *input;
} Console;
static void draw(const char *screen,int live) {
    if(live)fputs("\033[H\033[2J",stdout);
    for(int y=0;y<40;y++) {
        int n=96;while(n>0 && screen[y*96+n-1]==' ')n--;
        fwrite(screen+y*96,1,(size_t)n,stdout);putchar('\n');
    }
    fflush(stdout);
}
static int observe(const EmuResult *r,uint64_t count,void *opaque) {
    Console *c=opaque;(void)count;
    if(r->settings[0]==0 && (uint64_t)r->settings[1]+3840<=r->mem_size) {
        const char *frame=(const char *)r->mem+r->settings[1];
        if(memcmp(c->screen,frame,3840)) {
            memcpy(c->screen,frame,3840);c->screen[3840]=0;
            if(c->terminal && c->interactive)draw(c->screen,1);
        }
    }
    return c->quit || (c->until && strstr(c->screen,c->until));
}
static uint32_t keyboard(void *opaque) {
    Console *c=opaque;int ch;
    if(c->shift_pending==1){c->shift_pending=2;return 0x100u|c->queued;}
    if(c->shift_pending==2){c->shift_pending=0;return 16;}
    if(c->input) {
        ch=fgetc(c->input);if(ch==EOF)return 0;
    } else {
#ifdef _WIN32
        if(!_kbhit())return 0;
        ch=_getch();
        if(ch==0 || ch==224){(void)_getch();return 0;}
#else
        fd_set fds;struct timeval tv={0,0};
        FD_ZERO(&fds);FD_SET(0,&fds);
        if(select(1,&fds,NULL,NULL,&tv)<=0)return 0;
        ch=getchar();if(ch==EOF){c->quit=1;return 0;}
#endif
    }
    if(ch==4){c->quit=1;return 0;}
    if(ch=='\r')ch='\n';
    if(ch>='A' && ch<='Z') {c->queued=(unsigned char)ch;c->shift_pending=1;return 0x110;}
    return 0x100u|(unsigned char)ch;
}
static unsigned char *load(const char *path,size_t *len,size_t max) {
    FILE *f=fopen(path,"rb");long n;unsigned char *p;
    if(!f){perror(path);return NULL;}
    if(fseek(f,0,SEEK_END) || (n=ftell(f))<0 || (uint64_t)n>max){fclose(f);fprintf(stderr,"invalid size: %s\n",path);return NULL;}
    rewind(f);p=malloc(n?n:1);
    if(!p || fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);*len=(size_t)n;return p;
}
static int usage(void) {
    fputs("usage: emu.exe image.bin [ram_size] [--disk rootfs.bin] [--interactive]\n"
          "       [--input keys.txt] [--until screen-text] [--write-disk output.bin]\n",stderr);
    return 2;
}
int main(int argc,char **argv) {
    const char *disk_path=NULL,*write_path=NULL,*input_path=NULL;
    unsigned char *bin,*disk=NULL;size_t len,disk_len=0,ram=8<<20;
    Console console={0};EmuOptions options={0};EmuResult r;int status;
    if(argc<2)return usage();
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--interactive"))console.interactive=1;
        else if(!strcmp(argv[i],"--disk") && i+1<argc)disk_path=argv[++i];
        else if(!strcmp(argv[i],"--write-disk") && i+1<argc)write_path=argv[++i];
        else if(!strcmp(argv[i],"--until") && i+1<argc)console.until=argv[++i];
        else if(!strcmp(argv[i],"--input") && i+1<argc)input_path=argv[++i];
        else if(i==2 && argv[i][0]!='-') {
            char *end;unsigned long v=strtoul(argv[i],&end,0);
            if(*end || !v || v>(8u<<20))return usage();
            ram=v;
        } else return usage();
    }
    if(write_path && !disk_path)return usage();
    if(input_path && !(console.input=fopen(input_path,"rb"))){perror(input_path);return 2;}
    console.terminal=isatty(fileno(stdout));
    bin=load(argv[1],&len,ram);if(!bin){if(console.input)fclose(console.input);return 2;}
    if(disk_path && !(disk=load(disk_path,&disk_len,8<<20))){free(bin);if(console.input)fclose(console.input);return 2;}
    options.disk=disk;options.disk_len=disk_len;options.opaque=&console;
    options.observe=observe;options.ns_per_instruction=1000;
    if(console.interactive || console.input)options.keyboard=keyboard;
    r=emu_run_ex(bin,len,ram,console.interactive?UINT64_MAX:2000000000ULL,&options);
    status=r.error?1:0;
    if(r.mem && ram>=0x2f00 && (!console.terminal || !console.interactive))draw((const char *)r.mem+0x2000,0);
    if(r.error)fprintf(stderr,"emulator error=%d after %llu instructions\n",r.error,(unsigned long long)r.instructions);
    else if(!console.interactive)printf("exit_code = %d\n",r.exit_code);
    if(write_path && !r.error) {
        FILE *f=fopen(write_path,"wb");
        if(!f){perror(write_path);status=2;}
        else {if(fwrite(r.disk,1,r.disk_size,f)!=r.disk_size)status=2;if(fclose(f))status=2;}
    }
    if(console.input)fclose(console.input);
    free(bin);free(disk);free(r.mem);free(r.disk);return status;
}
