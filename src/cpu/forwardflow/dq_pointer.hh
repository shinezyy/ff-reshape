//
// Created by zyy on 19-6-12.
//

#ifndef __FF_DQ_POINTER_HH__
#define __FF_DQ_POINTER_HH__

#include <cstddef>
#include <functional>

#include "base/types.hh"

struct BasePointer {
    bool valid{};
    unsigned group{};
    unsigned bank{};
    unsigned index{};
    unsigned op{};

    bool operator==(const BasePointer& that) const {
        return group == that.group &&
               bank == that.bank &&
               index == that.index &&
               op == that.op;
    }

};

struct TermedPointer: public BasePointer {
    int term{-1};

    void operator = (const TermedPointer &pointer);
};

struct WKPointer;

struct DQPointer: public TermedPointer{

    bool hasVal{false};
    FFRegValue val;

    int reshapeOp{-1};
    unsigned ssrDelay{};
    unsigned fwLevel{};

    unsigned queueTime{};
    unsigned pendingTime{};

    DQPointer() = default;

    DQPointer(bool, unsigned, unsigned, unsigned, unsigned);

    DQPointer(bool, unsigned, unsigned, unsigned, unsigned, int);

    void operator = (const TermedPointer &dqPointer);

    explicit DQPointer(const WKPointer&);

    bool isLocal{false};

};

struct WKPointer: public TermedPointer{
    enum WKType {
        WKOp, // wakeup operands
        WKMem, // wakeup mem blocked dependency
        WKOrder, // wakeup store to load dependency
        WKMisc, // wakeup non-speculative, barrier, etc.
        WKBypass, // bypassing value on the path like store->load->load
        WKLdReExec // wakeup non-speculative, barrier, etc.
    };
    WKType wkType;

    bool hasVal;
    FFRegValue val;

    int reshapeOp{-1};
    unsigned ssrDelay{};
    unsigned fwLevel{};

    unsigned queueTime{};
    unsigned pendingTime{};


    WKPointer() = default;;

    void operator = (const TermedPointer &dqPointer);
    explicit WKPointer(const TermedPointer &pointer);


    void operator = (const DQPointer &dqPointer);
    explicit WKPointer(const DQPointer &dqPointer);

    WKPointer &operator = (const WKPointer &pointer);
    WKPointer(const WKPointer &pointer);

    // explicit WKPointer(DQPointer &&dqPointer);

    bool isFwExtra{false};

    bool isLocal{false};
};


struct PointerPair{
    bool isBypass;
    bool isBarrier;
    TermedPointer dest;
    TermedPointer payload;
    PointerPair() {
        isBypass = false;
        isBarrier = false;
        dest.valid = false;
        payload.valid = false;
    };
    PointerPair(const TermedPointer &d, const TermedPointer &p)
    : isBypass(false), isBarrier(false), dest(d), payload(p)
    {}
};

namespace std
{
template<>
struct hash<BasePointer>
{
    size_t operator()(const BasePointer& ptr) const
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
