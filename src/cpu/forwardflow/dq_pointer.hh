//
// Created by zyy on 19-6-12.
//

#ifndef __FF_DQ_POINTER_HH__
#define __FF_DQ_POINTER_HH__

struct DQPointer{
    bool valid;
    unsigned group;
    unsigned bank;
    unsigned index;
    unsigned op;
};

struct PointerPair{
    DQPointer dest;
    DQPointer payload;
};

#endif //__FF_DQ_POINTER_HH__
