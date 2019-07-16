//
// Created by zyy on 19-6-12.
//

#include "dq_pointer.hh"


DQPointer::DQPointer(WKPointer &wk)
{
    valid = wk.valid;
    group = wk.group;
    bank = wk.bank;
    index = wk.index;
    op = wk.op;
}

DQPointer::DQPointer(WKPointer wk)
{
    valid = wk.valid;
    group = wk.group;
    bank = wk.bank;
    index = wk.index;
    op = wk.op;
}

DQPointer::DQPointer(bool v, unsigned g, unsigned b, unsigned i, unsigned o)
{
    valid = v;
    group = g;
    bank = b;
    index = i;
    op = o;
}

WKPointer::WKPointer(DQPointer &dqPointer)
{
    valid = dqPointer.valid;
    wkType = WKOp;
    group = dqPointer.group;
    bank = dqPointer.bank;
    index = dqPointer.index;
    op = dqPointer.op;
}

WKPointer::WKPointer(DQPointer &&dqPointer)
{
    valid = dqPointer.valid;
    wkType = WKOp;
    group = dqPointer.group;
    bank = dqPointer.bank;
    index = dqPointer.index;
    op = dqPointer.op;
}
