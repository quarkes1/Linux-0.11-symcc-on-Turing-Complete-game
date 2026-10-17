#include "sp.h"
int sp_reg_probe(void);
int main(void) {if(sp_reg_probe()){puts("REG FAIL\n");return 1;}puts("REG PASS\n");return 0;}
