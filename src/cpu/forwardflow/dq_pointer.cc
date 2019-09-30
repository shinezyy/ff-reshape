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
    fwLevel = wk.fwLevel;

    queueTime = wk.queueTime;
}

DQPointer::DQPointer(bool v, unsigned g, unsigned b, unsigned i, unsigned o)
{
    valid = v;
    group = g;
    bank = b;
    index = i;
    op = o;

    reshapeOp = -1;
    fwLevel = 0;

    queueTime = 0;
}



DQPointer::DQPointer(bool v, unsigned g, unsigned b, unsigned i, unsigned o, int t)
{
    valid = v;
    group = g;
    bank = b;
    index = i;
    op = o;

    reshapeOp = -1;
    fwLevel = 0;

    queueTime = 0;
    term = t;
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
    fwLevel = dqPointer.fwLevel;

    queueTime = dqPointer.queueTime;
}
