//
// Created by zyy on 19-6-12.
//

#ifndef __FF_DQ_POINTER_HH__
#define __FF_DQ_POINTER_HH__

struct WKPointer;

struct DQPointer{
    bool valid;
    unsigned group;
    unsigned bank;
    unsigned index;
    unsigned op;

    DQPointer() = default;

    DQPointer(bool, unsigned, unsigned, unsigned, unsigned);

    explicit DQPointer(WKPointer&);

    explicit DQPointer(WKPointer);
};

struct WKPointer{
    bool valid{};
    enum WKType {
        WKOp, // wakeup operands
        WKMem, // wakeup mem dependency
        WKMisc // wakeup non-speculative, barrier, etc.
    };
    WKType wkType;
    unsigned group{};
    unsigned bank{};
    unsigned index{};
    unsigned op{};

    WKPointer() = default;;

    explicit WKPointer(DQPointer &dqPointer);

    explicit WKPointer(DQPointer &&dqPointer);
};


struct PointerPair{
    DQPointer dest;
    DQPointer payload;
};

#endif //__FF_DQ_POINTER_HH__
