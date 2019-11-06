#ifndef __FF_CROSSBAR_NARROW_HH__
#define __FF_CROSSBAR_NARROW_HH__

#include <cstdint>
#include <tuple>

#include <boost/dynamic_bitset.hpp>

#include "base/intmath.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "cpu/forwardflow/network_common.hh"
#include "debug/CrossBarNarrow.hh"

namespace FF{

template <class T>
class CrossBarNarrow {
    const uint32_t size;

    const uint32_t nOps;

    const uint32_t nChannels;

    std::list<uint32_t> prioList;
    std::list<uint32_t> opPrioList;

    std::vector<bool> channelUsed;

public:
    std::vector<DQPacket<T>*> select(std::vector<DQPacket<T>*> &, DQPacket<T> *null);

    CrossBarNarrow(uint32_t size, uint32_t n_ops);

    const std::string name() {return "CrossBarSwitchX4";};
};

template<class T>
CrossBarNarrow<T>::CrossBarNarrow(uint32_t size, uint32_t n_ops):
    size(size),
    nOps(n_ops),
    nChannels(size/nOps),
    channelUsed(nChannels)
{
    for (uint32_t i = 0; i < nChannels; i++) {
        prioList.push_back(i);
    }
    for (uint32_t i = 0; i < nOps; i++) {
        opPrioList.push_back(i);
    }
}

template<class T>
std::vector<DQPacket<T>*>
CrossBarNarrow<T>::select(std::vector<DQPacket<T>*> &inputs, DQPacket<T> *null)
{
    std::vector<DQPacket<T>*> outputs(size, null);
    assert(inputs.size() == size);
    std::fill(channelUsed.begin(), channelUsed.end(), false);

    for (uint32_t i: prioList) {
        auto pkt = null;
        for (uint32_t op: opPrioList) {
            if (inputs[i * nOps + op] && inputs[i * nOps + op]->valid) {
                pkt = inputs[i * nOps + op];
                break;
            }
        }
        if (pkt && pkt->valid) {
            uint32_t dest_chan = pkt->dest/nOps;
            if (!channelUsed[dest_chan]) {
                DPRINTF(CrossBarNarrow, "Pass pkt[%i] to [%i]\n", i, pkt->dest);
                outputs[pkt->dest] = pkt;
                channelUsed[dest_chan] = true;
            } else {
                DPRINTF(CrossBarNarrow, "Reject pkt[%i] req to [%i], conflicted\n",
                        i, pkt->dest);
            }
        }
    }

    prioList.push_back(prioList.front());
    prioList.pop_front();

    opPrioList.push_back(opPrioList.front());
    opPrioList.pop_front();

    return outputs;
}

}
#endif
