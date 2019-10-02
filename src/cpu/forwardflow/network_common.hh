#ifndef __FF_NET_COMMON_HH__
#define __FF_NET_COMMON_HH__


#include <cstdint>

#include <boost/dynamic_bitset.hpp>

template <class T>
struct DQPacket{
    bool valid{};
    T payload;
    uint32_t source{};
    boost::dynamic_bitset<> destBits;
};


#endif
