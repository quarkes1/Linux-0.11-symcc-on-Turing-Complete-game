struct inode_disk { unsigned short mode,uid; unsigned size,time; unsigned char gid,nlinks; unsigned short zones[9]; };
struct directory { unsigned short ino; char name[14]; };
int main(void) {
    struct inode_disk d;
    struct directory e;
    return sizeof(d)==32 && sizeof(e)==16 &&
        (char *)&d.zones-(char *)&d==14 &&
        (char *)&e.name-(char *)&e==2 ? 0 : 1;
}
