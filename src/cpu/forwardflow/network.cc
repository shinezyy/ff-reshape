//
// Created by zyy on 19-6-6.
//

#include <base/intmath.hh>

#include "network.hh"

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
tuple<Packet<T>*, Packet<T>*>
CrossBar<T>::cross(Packet<T> *input0, Packet<T> *input1)
{
    int low = 1- ascendPrio;
    int high = ascendPrio;
    int direction_bit = bits - stage - 1;

    Packet<T> * inputs[2];
    inputs[0] = input0;
    inputs[1] = input1;

    Packet<T> * outputs[2];
    bool low_granted;
    low_granted = false;

    int high_demand = inputs[high]->destBits[direction_bit];
    int low_demand = inputs[low]->destBits[direction_bit];

    if (inputs[high]->valid) {
        outputs[high_demand] = inputs[high];
        low_granted = high_demand != low_demand;
    }
    if (low_granted || !inputs[high]->valid) {
        outputs[low_demand] = inputs[low];
    }

    return make_tuple(outputs[0], outputs[1]);
}



template<class T>
OmegaNetwork<T>::OmegaNetwork(uint32_t size, bool ascendPrio)
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
tuple<std::vector<bool>, std::vector<Packet<T> *>>
        OmegaNetwork<T>::select(std::vector<Packet<T> *> &_inputs)
{
    vector<Packet<T>*> buffer[2](size);

    vector<Packet<T>*> *inputs = &buffer[0], *outputs = &buffer[1];
    vector<bool> grants(size, false);

    for (uint32_t x = 0; x < ceilLog2(size); x++) {
        connect(inputs, outputs);
        swap(inputs, outputs);
        for (uint32_t y = 0; y < size/2; y += 2) {
            tie(*outputs[y], *outputs[y+1]) = switches[y/2][x].cross(*inputs[y], *inputs[y+1]);
        }
        swap(inputs, outputs);
    }

    for (int i = 0; i < size; i++) {
        if ((*inputs)[i]->valid) {
            grants[(*inputs)[i]->source] = true;
        }
    }

    return make_tuple(grants, inputs);
}

template<class T>
void OmegaNetwork<T>::connect(vector<Packet<T> *> *left,
                              std::vector<Packet<T>*> *right)
{
    assert(left->size() == right->size());
    assert(left->size() > 0);
    for (uint32_t i = 0; i < size/2; i++) {
        *right[2*i] = *left[i];
    }
    for (uint32_t i = 0; i < size/2; i++) {
        *right[2*i + 1] = *left[i + size/2];
    }
}

template<class T>
void
OmegaNetwork<T>::swap(vector<Packet<T> *> *&l, vector<Packet<T> *> *&r)
{
    auto tmp = l;
    l = r;
    r = tmp;
}

}

