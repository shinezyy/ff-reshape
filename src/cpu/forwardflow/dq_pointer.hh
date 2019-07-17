//
// Created by zyy on 19-6-12.
//

#ifndef __FF_DQ_POINTER_HH__
#define __FF_DQ_POINTER_HH__

#include <cstddef>
#include <functional>

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

    bool operator==(const DQPointer& that) const {
        return group == that.group &&
            bank == that.bank &&
            index == that.index &&
            op == that.op;
    }
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

namespace std
{
template<>
struct hash<DQPointer>
{
    size_t operator()(const DQPointer& ptr) const
    {
        const size_t g = ptr.group << 16; // no more that 2^16 entry in a group
        const size_t i = ptr.index << 5; // 32 bank max
        const size_t b = ptr.bank;
        return g | i | b;
    }
};
}

#endif //__FF_DQ_POINTER_HH__
