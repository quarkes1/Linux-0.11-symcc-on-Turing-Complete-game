#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"
static const char *script="ls /bin\ncat /etc/motd\necho hello\nforktest\nnosuchcommand\necho abx\bC\ncd /etc\ncat motd\nexit\necho finished\n";
static int input_at,can_type,shift_event,release_event,shift_code=16;
static unsigned char queued;
static int fork_seen,error_seen,upper_seen,proc_seen,orphan_seen,exec_seen,reg_seen,mode;
static int boot_seen,welcome_seen,clean_seen,cwd_seen,pwd_seen,long_seen,dots_seen,badflag_seen;
static int nested_seen;
static int file_seen;
static int row_is(const char *screen,const char *want);
static int last_row_is(const char *screen,const char *want) {
    for(int row=39;row>=0;row--) {
        int n=96;while(n>0 && screen[row*96+n-1]==' ')n--;
        if(n)return n==(int)strlen(want) && !memcmp(screen+row*96,want,n);
    }
    return 0;
}
static int prompt(const char *screen) {
    return last_row_is(screen,"symplus:/$") || last_row_is(screen,"symplus:/etc$");
}
static char prior[3840];static uint64_t stable_since;static int stress_lines[100];
static uint32_t keyboard(void *p) {
    unsigned char c;(void)p;
    if(shift_event==1){shift_event=2;return 0x100u|queued;}
    if(shift_event==2){shift_event=0;return shift_code;}
    if(release_event){release_event=0;return 13;}
    if(!can_type || !script[input_at])return 0;
    c=script[input_at++];if(c=='\n')can_type=0;
    /* Actual installed-game map: Backspace=0x0d, Enter=0x0a. */
    if(mode==5 && c=='\b'){release_event=1;return 0x10d;}
    if(mode==6 || mode==7) {
        const char *shifted="~!@#$%^&*()_+{}|:\"<>?";
        const char *base="`1234567890-=[]\\;',./";
        const char *at=strchr(shifted,c);
        if(at){queued=(unsigned char)base[at-shifted];shift_code=(input_at&1)?16:20;shift_event=1;return 0x100u|shift_code;}
    }
    if(c>='A' && c<='Z'){queued=c;shift_code=16;shift_event=1;return 0x110;}
    return 0x100u|c;
}
static int row_is(const char *screen,const char *want) {
    for(int row=0;row<40;row++) {
        int n=96;while(n>0 && screen[row*96+n-1]==' ')n--;
        if(n==(int)strlen(want) && !memcmp(screen+row*96,want,n))return 1;
    }
    return 0;
}
static int seen(const EmuResult *r,uint64_t n,void *p) {
    char screen[3841]; (void)n;(void)p;
    memcpy(screen,r->mem+0x2000,3840);screen[3840]=0;
    if(memcmp(prior,screen,3840)) {
        memcpy(prior,screen,3840);stable_since=n;
        if(mode==1)for(int row=0;row<40;row++) {
            const char *line=screen+row*96;
            if(!memcmp(line,"line",4) && line[4]>='0' && line[4]<='9' &&
                line[5]>='0' && line[5]<='9' && line[6]>='0' && line[6]<='9' && line[7]==' ') {
                int index=(line[4]-'0')*100+(line[5]-'0')*10+line[6]-'0';
                if(index<100)stress_lines[index]=1;
            }
        }
    }
    int stable=n-stable_since>=8192;
    if(row_is(screen,"[ OK ] MINIX root mounted on DiskA"))boot_seen=1;
    if(row_is(screen,"Welcome to SymphonyPlus Linux."))welcome_seen=1;
    if(stable && prompt(screen)) {
        if(mode==4 && script+input_at==strstr(script,"sh\npwd\n")+7)
            nested_seen=last_row_is(screen,"symplus:/etc$");
        if(!input_at && row_is(screen,"symplus:/$") &&
           !row_is(screen,"Welcome to SymphonyPlus Linux.") &&
           !row_is(screen,"SymphonyPlus Linux: interactive boot"))clean_seen=1;
        can_type=1;
    }
    if(mode==7 && stable && last_row_is(screen,">"))can_type=1;
    file_seen|=row_is(screen,"int main(void) { return 2 < 3; }");
    cwd_seen|=row_is(screen,"symplus:/etc$");pwd_seen|=row_is(screen,"/etc");
    long_seen|=row_is(screen,"-rw-r--r-- 1 0 0 31 motd");
    dots_seen|=row_is(screen,"drwxr-xr-x 2 0 0 48 .");
    badflag_seen|=row_is(screen,"ls: unknown option: z");
    fork_seen|=row_is(screen,"FORK PASS");error_seen|=row_is(screen,"nosuchcommand: not found");
    upper_seen|=row_is(screen,"abC");proc_seen|=row_is(screen,"PROC PASS");
    orphan_seen|=row_is(screen,"ORPHAN PASS");exec_seen|=row_is(screen,"EXEC PASS");
    reg_seen|=row_is(screen,"REG PASS");
    if(mode==1) {
        int count=0;for(int i=0;i<100;i++)count+=stress_lines[i];
        return count==100 && stable && row_is(screen,"stressdone") && prompt(screen);
    }
    if(mode==2)return reg_seen && proc_seen && orphan_seen && exec_seen && stable && row_is(screen,"finished") && prompt(screen);
    if(mode==3)return row_is(screen,"line too long") && row_is(screen,"aftertty") && stable && prompt(screen);
    if(mode==4)return stable && row_is(screen,"uidone") && prompt(screen);
    if(mode==5)return stable && row_is(screen,"keydone") && prompt(screen);
    if(mode==6)return stable && row_is(screen,"symdone") && prompt(screen);
    if(mode==7)return stable && row_is(screen,"filedone") && prompt(screen);
    return fork_seen && error_seen && upper_seen && stable && row_is(screen,"finished") && prompt(screen);
}
static unsigned char *readfile(const char *s,size_t *len) {
    FILE *f=fopen(s,"rb");unsigned char *p;long n;
    if(!f){fprintf(stderr,"missing %s\n",s);exit(1);}
    fseek(f,0,SEEK_END);n=ftell(f);rewind(f);p=malloc(n);*len=fread(p,1,n,f);fclose(f);return p;
}
int main(int argc,char **argv) {
    static char stress[8192];
    if(argc>1 && !strcmp(argv[1],"--stress")) {
        size_t at=0;mode=1;
        for(int i=0;i<100;i++)at+=(size_t)sprintf(stress+at,"echo line%03d\n",i);
        strcpy(stress+at,"echo stressdone\n");script=stress;
    } else if(argc>1 && !strcmp(argv[1],"--process")) {
        mode=2;script="regtest\nproctest\nexecprobe\necho finished\n";
    } else if(argc>1 && !strcmp(argv[1],"--tty")) {
        mode=3;strcpy(stress,"echo ");memset(stress+5,'x',1100);
        strcpy(stress+1105,"\necho aftertty\n");script=stress;
    } else if(argc>1 && !strcmp(argv[1],"--ui")) {
        mode=4;script="ls -la /etc\nls -z\ncd /etc\nsh\npwd\nexit\npwd\ncd ../etc/./\ncd /missing\npwd\necho uidone\n";
    } else if(argc>1 && !strcmp(argv[1],"--keys")) {
        mode=5;script="\becho keyx\bOK\necho keydone\n";
    } else if(argc>1 && !strcmp(argv[1],"--symbols")) {
        mode=6;script="echo '~!@#$%^&*()_+{}|:\"<>?'\necho symdone\n";
    } else if(argc>1 && !strcmp(argv[1],"--files")) {
        mode=7;script="mkdir /tmp/demo\ncat >/tmp/demo/note <<EOF\nint main(void) { return 2 < 3; }\nEOF\ncat /tmp/demo/note\ncat /tmp/copy <<END\nsecond\nEND\nrm /tmp/copy\nrm -r /tmp/demo\necho filedone\n";
    }
    size_t bn,dn;unsigned char *bin=readfile("build/kernel.bin",&bn),*disk=readfile("build/rootfs.bin",&dn);
    EmuOptions opt={0};EmuResult r;
    opt.disk=disk;opt.disk_len=dn;opt.keyboard=keyboard;opt.observe=seen;opt.ns_per_instruction=1000;
    bin[0x1fff]=0xa5;bin[0x2f00]=0x5a;
    r=emu_run_ex(bin,bn,8<<20,5000000000ULL,&opt);
    for(int i=0;i<40;i++) { fwrite(r.mem+0x2000+i*96,1,96,stdout);putchar('\n'); }
    fprintf(stderr,"stop=%d error=%d instructions=%llu\n",r.stop_reason,r.error,(unsigned long long)r.instructions);
    int ok=r.stop_reason==1 && !r.error && r.mem[0x1fff]==0xa5 && r.mem[0x2f00]==0x5a;
    if(mode==4) {
        fprintf(stderr,"boot=%d welcome=%d clear=%d cwd=%d pwd=%d long=%d dots=%d badflag=%d\n",boot_seen,welcome_seen,clean_seen,cwd_seen,pwd_seen,long_seen,dots_seen,badflag_seen);
        fprintf(stderr,"nested=%d final_cwd=%d\n",nested_seen,last_row_is((char *)r.mem+0x2000,"symplus:/etc$"));
        ok=ok && boot_seen && welcome_seen && clean_seen && cwd_seen && pwd_seen && long_seen && dots_seen && badflag_seen && nested_seen && last_row_is((char *)r.mem+0x2000,"symplus:/etc$");
    }
    if(mode==5) {
        fprintf(stderr,"edited=%d premature=%d\n",row_is((char *)r.mem+0x2000,"keyOK"),row_is((char *)r.mem+0x2000,"keyx"));
        ok=ok && row_is((char *)r.mem+0x2000,"keyOK") && !row_is((char *)r.mem+0x2000,"keyx");
    }
    if(mode==6)ok=ok && row_is((char *)r.mem+0x2000,"~!@#$%^&*()_+{}|:\"<>?");
    if(mode==7)ok=ok && file_seen;
    free(bin);free(disk);free(r.mem);free(r.disk);return ok?0:1;
}
