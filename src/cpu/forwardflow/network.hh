//
// Created by zyy on 19-6-6.
//

#ifndef __FF_NETWORK_HH__
#define __FF_NETWORK_HH__


#include <cstdint>
#include <tuple>

#include <boost/dynamic_bitset.hpp>

#include "base/intmath.hh"
#include "base/trace.hh"
#include "debug/Omega.hh"

namespace FF{

using std::tie;
using std::tuple;

template <class T>
struct DQPacket{
    bool valid{};
    T payload;
    uint32_t source{};
    boost::dynamic_bitset<> destBits;
};

template <class T>
class CrossBar {
public:

    CrossBar(uint32_t bits, uint32_t stage, bool ascendPrio);

    tuple<DQPacket<T>*, DQPacket<T>*> cross(DQPacket<T>*, DQPacket<T>*);

protected:
    const uint32_t bits;
    const uint32_t stage;
    const bool ascendPrio;

};

template <class T>
class OmegaNetwork {
    const uint32_t size;
    std::vector<std::vector<CrossBar<T>>> switches;

public:
    std::vector<DQPacket<T>*> select(std::vector<DQPacket<T>*> &);

    OmegaNetwork(uint32_t size, bool ascendPrio);

    void connect(std::vector<DQPacket<T>*>*, std::vector<DQPacket<T>*>*);

    void swap(std::vector<DQPacket<T>*>*&, std::vector<DQPacket<T>*>*&);
};

}

namespace FF{
using namespace std;

template<class T>
CrossBar<T>::CrossBar(uint32_t bits, uint32_t stage, bool ascendPrio)
:bits(bits),
stage(stage),
ascendPrio(ascendPrio)
{

}

template<class T>
tuple<DQPacket<T>*, DQPacket<T>*>
CrossBar<T>::cross(DQPacket<T> *input0, DQPacket<T> *input1)
{
    int low = 1- ascendPrio;
    int high = ascendPrio;
    int direction_bit = bits - stage - 1;

    DQPacket<T> * inputs[2];
    assert(input0);
    assert(input1);
    inputs[0] = input0;
    inputs[1] = input1;

    DQPacket<T> * outputs[2];
    bool low_granted;
    low_granted = false;

    DPRINTF(Omega, "high = %d, low = %d\n", high, low);
    DPRINTF(Omega, "destBits size = %d, direction_bit = %d\n",
            inputs[high]->destBits.size(), direction_bit);

    int high_demand = inputs[high]->valid ?
            inputs[high]->destBits[direction_bit] : 0;
    int low_demand = inputs[low]->valid ?
            inputs[low]->destBits[direction_bit] : 0;

    if (inputs[high]->valid) {
        outputs[high_demand] = inputs[high];
        low_granted = high_demand != low_demand;
    }
    if (low_granted || !inputs[high]->valid) {
        outputs[low_demand] = inputs[low];
    }

    DPRINTF(Omega, "X reach 4\n");
    return make_tuple(outputs[0], outputs[1]);
}



template<class T>
OmegaNetwork<T>::OmegaNetwork(uint32_t size, bool ascendPrio)
        :
        size(size)
{
    for (auto y = 0; y < size/2; y++) {
        switches.push_back(vector<CrossBar<T>>());
        vector<CrossBar<T>> &row = switches.back();
        for (auto x = 0; x < ceilLog2(size); x++) {
            row.push_back(CrossBar<T>(static_cast<uint32_t>(ceilLog2(size)),
                    static_cast<uint32_t>(x), ascendPrio));
        }
    }
    assert(size >= 2);
    assert(isPowerOf2(size));
}

template<class T>
std::vector<DQPacket<T> *>
        OmegaNetwork<T>::select(std::vector<DQPacket<T> *> &_inputs)
{
    array<vector<DQPacket<T>*>, 2> buffer;
//    fill(buffer.begin(), buffer.end(), vector<DQPacket<T>*>(size));
    buffer[0] = _inputs;
    buffer[1] = vector<DQPacket<T>*>(size);

    vector<DQPacket<T>*> *inputs = &buffer[0], *outputs = &buffer[1];
//    vector<bool> grants(size, false);

    for (uint32_t x = 0; x < ceilLog2(size); x++) {
        connect(inputs, outputs);
        swap(inputs, outputs);
        for (uint32_t y = 0; y < size/2; y += 2) {
            assert(y < outputs->size());
            assert(y + 1 < outputs->size());
            assert(y < inputs->size());
            assert(y + 1 < inputs->size());
            assert(y/2 < switches.size());
            assert(x < switches[y/2].size());

            tie((*outputs)[y], (*outputs)[y+1]) =
                    switches[y/2][x].cross((*inputs)[y], (*inputs)[y+1]);
        }
        swap(inputs, outputs);
    }

//    for (int i = 0; i < size; i++) {
//        if ((*inputs)[i]->valid) {
//            grants[(*inputs)[i]->source] = true;
//        }
//    }

    return *inputs;
}

template<class T>
void OmegaNetwork<T>::connect(vector<DQPacket<T> *> *left,
                              std::vector<DQPacket<T>*> *right)
{
    assert(left->size() == right->size());
    assert(left->size() > 0);
    for (uint32_t i = 0; i < size/2; i++) {
        (*right)[2*i] = (*left)[i];
    }
    for (uint32_t i = 0; i < size/2; i++) {
        (*right)[2*i + 1] = (*left)[i + size/2];
    }
}

template<class T>
void
OmegaNetwork<T>::swap(vector<DQPacket<T> *> *&l, vector<DQPacket<T> *> *&r)
{
    auto tmp = l;
    l = r;
    r = tmp;
}

}
#endif //__FF_NETWORK_HH__
