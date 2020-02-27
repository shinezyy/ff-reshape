//
// Created by zyy on 19-6-12.
//

#ifndef __FF_DQ_POINTER_HH__
#define __FF_DQ_POINTER_HH__

#include <cstddef>
#include <functional>

#include "base/types.hh"

struct WKPointer;

struct DQPointer{
    bool valid;
    unsigned group;
    unsigned bank;
    unsigned index;
    unsigned op;

    bool hasVal;
    FFRegValue val;

    int reshapeOp{-1};
    unsigned ssrDelay{};
    unsigned fwLevel{};

    unsigned queueTime{};
    unsigned pendingTime{};

    DQPointer() = default;

    DQPointer(bool, unsigned, unsigned, unsigned, unsigned);

    DQPointer(bool, unsigned, unsigned, unsigned, unsigned, int);

    explicit DQPointer(const WKPointer&);

    int term{-1};

    bool isLocal{false};

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
        WKMem, // wakeup mem blocked dependency
        WKOrder, // wakeup store to load dependency
        WKMisc // wakeup non-speculative, barrier, etc.
    };
    WKType wkType;
    unsigned group{};
    unsigned bank{};
    unsigned index{};
    unsigned op{};

    bool hasVal;
    FFRegValue val;

    int reshapeOp{-1};
    unsigned ssrDelay{};
    unsigned fwLevel{};

    unsigned queueTime{};
    unsigned pendingTime{};

    int term{-1};

    WKPointer() = default;;

    explicit WKPointer(const DQPointer &dqPointer);

    // explicit WKPointer(DQPointer &&dqPointer);

    bool isFwExtra{false};

    bool isLocal{false};
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

#define extptrp(x) (x)->valid, (x)->group, (x)->index, (x)->bank, (x)->op
#define extptr(x) (x).valid, (x).group, (x).index, (x).bank, (x).op

#define ptrfmt " (%i) (%i %i B%i X%i) "

#endif //__FF_DQ_POINTER_HH__
