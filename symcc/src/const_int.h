#ifndef SP_CONST_INT_H
#define SP_CONST_INT_H
/* Each recursive evaluator returns its type alongside the value. Keep this
 * state local to its translation unit, and save it before evaluating RHS. */
static int cc_eval_unsigned;
static CcInt cc_binary(CcInt a,int ua,CcInt b,int ub,Token *op) {
    uint32_t x=(uint32_t)a,y=(uint32_t)b;
    int uns=ua||ub;
    cc_eval_unsigned=uns;
    if(tok_is(op,"&&") || tok_is(op,"||")) {
        cc_eval_unsigned=0;
        return tok_is(op,"&&")?(a!=0 && b!=0):(a!=0 || b!=0);
    }
    if(tok_is(op,"==") || tok_is(op,"!=") || tok_is(op,"<") ||
       tok_is(op,"<=") || tok_is(op,">") || tok_is(op,">=")) {
        cc_eval_unsigned=0;
        if(tok_is(op,"=="))return uns?x==y:a==b;
        if(tok_is(op,"!="))return uns?x!=y:a!=b;
        if(tok_is(op,"<"))return uns?x<y:a<b;
        if(tok_is(op,"<="))return uns?x<=y:a<=b;
        if(tok_is(op,">"))return uns?x>y:a>b;
        return uns?x>=y:a>=b;
    }
    if(tok_is(op,"<<") || tok_is(op,">>")) {
        cc_eval_unsigned=ua;
        if(b<0 || b>=32){fprintf(stderr,"constant shift out of range\n");exit(1);}
        if(tok_is(op,"<<"))return ua?(CcInt)(uint32_t)(x<<y):a<<b;
        return ua?(CcInt)(x>>y):a>>b;
    }
    if(tok_is(op,"+"))return uns?(CcInt)(uint32_t)(x+y):a+b;
    if(tok_is(op,"-"))return uns?(CcInt)(uint32_t)(x-y):a-b;
    if(tok_is(op,"*"))return uns?(CcInt)(uint32_t)(x*y):a*b;
    if(tok_is(op,"/"))return b?(uns?(CcInt)(x/y):a/b):0;
    if(tok_is(op,"%"))return b?(uns?(CcInt)(x%y):a%b):0;
    if(tok_is(op,"&"))return uns?(CcInt)(x&y):a&b;
    if(tok_is(op,"^"))return uns?(CcInt)(x^y):a^b;
    return uns?(CcInt)(x|y):a|b;
}
#endif
