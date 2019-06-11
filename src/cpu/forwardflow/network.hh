//
// Created by zyy on 19-6-6.
//

#ifndef __FF_NETWORK_HH__
#define __FF_NETWORK_HH__


#include <cstdint>
#include <tuple>

#include <boost/dynamic_bitset.hpp>

namespace FF{

using std::tie;
using std::tuple;

template <class T>
struct Packet{
    bool valid{false};
    T payload;
    uint32_t source;
    boost::dynamic_bitset<> destBits;
};

template <class T>
class CrossBar {
public:

    CrossBar(uint32_t bits, uint32_t stage, bool ascendPrio);

    tuple<Packet<T>*, Packet<T>*> cross(Packet<T>*, Packet<T>*);

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
    tuple<std::vector<bool>, std::vector<Packet<T>*>> select(std::vector<Packet<T>*> &);

    OmegaNetwork(uint32_t size, bool ascendPrio);

    void connect(std::vector<Packet<T>*>*, std::vector<Packet<T>*>*);

    void swap(std::vector<Packet<T>*>*&, std::vector<Packet<T>*>*&);
};

}
#endif //__FF_NETWORK_HH__
