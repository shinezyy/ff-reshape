#ifndef __FF_CROSSBAR_HH__
#define __FF_CROSSBAR_HH__

#include <cstdint>
#include <tuple>

#include <boost/dynamic_bitset.hpp>

#include "base/intmath.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "cpu/forwardflow/network_common.hh"
#include "debug/CrossBar.hh"

namespace FF{

template <class T>
class CrossBar {
    const uint32_t size;

    std::list<uint32_t> prioList;

public:
    std::vector<DQPacket<T>*> select(std::vector<DQPacket<T>*> &);

    CrossBar(uint32_t size);

    const std::string name() {return "CrossBarSwitch";};
};

template<class T>
CrossBar<T>::CrossBar(uint32_t size): size(size)
{
    for (uint32_t i = 0; i < size; i++) {
        prioList.push_back(i);
    }
}

template<class T>
std::vector<DQPacket<T>*>
CrossBar<T>::select(std::vector<DQPacket<T>*> &inputs)
{
    DQPacket<T> null;
    null.valid = false;
    std::vector<DQPacket<T>*> outputs(size, &null);
    assert(inputs.size() == size);

    for (uint32_t i: prioList) {
        const auto &pkt = inputs[i];
        if (pkt && pkt->valid) {
            if (!outputs[pkt->dest]->valid) {
                DPRINTF(CrossBar, "Pass pkt[%i] to [%i]\n", i, pkt->dest);
                outputs[pkt->dest] = inputs[i];
            } else {
                DPRINTF(CrossBar, "Reject pkt[%i] req to [%i], conflict by [%i]\n",
                        i, pkt->dest, outputs[i]->source);
            }
        }
    }

    prioList.push_back(prioList.front());
    prioList.pop_front();
    return outputs;
}

}
#endif
