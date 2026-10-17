/* SYMPLUS-PORT: synchronous DiskA transfers, no x86 ports or IRQ completion. */
#include <linux/sched.h>
#include <linux/symplus.h>
#include "blk.h"
static void disk_request(void) {
    struct request *r;unsigned address,bytes,i,value;int ok;
    while((r=blk_dev[3].current_request)!=NULL) {
        ok=MINOR(r->dev)==0 && r->sector<16384 && r->nr_sectors<=16384-r->sector &&
            (r->cmd==READ || r->cmd==WRITE);
        if(ok) {
            address=r->sector*512;bytes=r->nr_sectors*512;
            for(i=0;i<bytes;i+=4) {
                if(r->cmd==READ) {
                    value=sp_disk_read32(address+i);
                    r->buffer[i]=value>>24;r->buffer[i+1]=value>>16;
                    r->buffer[i+2]=value>>8;r->buffer[i+3]=value;
                } else {
                    value=((unsigned)(unsigned char)r->buffer[i]<<24)|((unsigned)(unsigned char)r->buffer[i+1]<<16)|
                        ((unsigned)(unsigned char)r->buffer[i+2]<<8)|(unsigned char)r->buffer[i+3];
                    sp_disk_write32(address+i,value);
                }
            }
        }
        if(r->bh) {r->bh->b_uptodate=ok;r->bh->b_lock=0;wake_up(&r->bh->b_wait);}
        wake_up(&r->waiting);wake_up(&wait_for_request);
        r->dev= -1;blk_dev[3].current_request=r->next;
    }
}
void symdisk_init(void) {blk_dev[3].request_fn=disk_request;}
