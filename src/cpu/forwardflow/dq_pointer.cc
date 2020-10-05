//
// Created by zyy on 19-6-12.
//

#include "dq_pointer.hh"


DQPointer::DQPointer(const WKPointer &wk)
{
    valid = wk.valid;
    group = wk.group;
    bank = wk.bank;
    index = wk.index;
    op = wk.op;
    term = wk.term;

    reshapeOp = wk.reshapeOp;
    ssrDelay = wk.ssrDelay;
    fwLevel = wk.fwLevel;

    queueTime = wk.queueTime;
    pendingTime = wk.pendingTime;

    hasVal = wk.hasVal;
    val = wk.val;

    isLocal = wk.isLocal;
}

DQPointer::DQPointer(bool v, unsigned g, unsigned b, unsigned i, unsigned o)
{
    valid = v;
    group = g;
    bank = b;
    index = i;
    op = o;

    reshapeOp = -1;
    ssrDelay = 0;
    fwLevel = 0;

    queueTime = 0;
    pendingTime = 0;

    hasVal = false;
    val.i = ~0;
}



DQPointer::DQPointer(bool v, unsigned g, unsigned b, unsigned i, unsigned o, int t)
{
    valid = v;
    group = g;
    bank = b;
    index = i;
    op = o;

    reshapeOp = -1;
    ssrDelay = 0;
    fwLevel = 0;

    queueTime = 0;
    pendingTime = 0;
    term = t;

    hasVal = false;
    val.i = ~0;
}

WKPointer::WKPointer(const DQPointer &dqPointer)
{
    valid = dqPointer.valid;
    wkType = WKOp;
    group = dqPointer.group;
    bank = dqPointer.bank;
    index = dqPointer.index;
    op = dqPointer.op;
    term = dqPointer.term;

    reshapeOp = dqPointer.reshapeOp;
    ssrDelay = dqPointer.ssrDelay;
    fwLevel = dqPointer.fwLevel;

    queueTime = dqPointer.queueTime;
    pendingTime = dqPointer.pendingTime;

    hasVal = dqPointer.hasVal;
    val = dqPointer.val;

    isLocal = dqPointer.isLocal;
}
