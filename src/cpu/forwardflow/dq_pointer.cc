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

    term = t;

    reshapeOp = -1;
    ssrDelay = 0;
    fwLevel = 0;

    queueTime = 0;
    pendingTime = 0;

    hasVal = false;
    val.i = ~0;
}

void DQPointer::operator=(const TermedPointer &pointer)
{
    TermedPointer::operator=(pointer);

    hasVal = false;
}

WKPointer::WKPointer(const DQPointer &dqPointer)
{
    operator = (dqPointer);
}


void
WKPointer::operator=(const DQPointer &dqPointer)
{
    TermedPointer::operator=(dqPointer);

    hasVal = dqPointer.hasVal;

    reshapeOp = dqPointer.reshapeOp;
    ssrDelay = dqPointer.ssrDelay;
    fwLevel = dqPointer.fwLevel;

    queueTime = dqPointer.queueTime;
    pendingTime = dqPointer.pendingTime;

    val = dqPointer.val;

    isLocal = dqPointer.isLocal;
    wkType = WKType::WKOp;
}

WKPointer::WKPointer(const TermedPointer &pointer)
{
    operator = (pointer);
}

void WKPointer::operator=(const TermedPointer &pointer)
{
    TermedPointer::operator=(pointer);

    wkType = WKType::WKOp;
    hasVal = false;
}

WKPointer::WKPointer(const WKPointer &pointer)
{
    operator=(pointer);
}

WKPointer &WKPointer::operator=(const WKPointer &pointer)
{
    TermedPointer::operator=(pointer);

    hasVal = pointer.hasVal;
    val = pointer.val;

    reshapeOp = pointer.reshapeOp;
    ssrDelay = pointer.ssrDelay;
    fwLevel = pointer.fwLevel;

    queueTime = pointer.queueTime;
    pendingTime = pointer.pendingTime;

    isLocal = pointer.isLocal;
    wkType = pointer.wkType;

    return *this;
}

void TermedPointer::operator=(const TermedPointer &pointer)
{
    valid = pointer.valid;

    group = pointer.group;
    bank = pointer.bank;
    index = pointer.index;
    op = pointer.op;

    term = pointer.term;
}
