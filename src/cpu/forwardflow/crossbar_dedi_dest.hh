#ifndef __FF_CROSSBAR_DEDI_HH__
#define __FF_CROSSBAR_DEDI_HH__

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
class CrossBarDedi {
    const uint32_t size;

    const uint32_t nOps;

    const uint32_t nChannels;

    std::list<uint32_t> prioList;
    std::list<uint32_t> opPrioList;

    std::vector<bool> destChannelUsed;
    std::vector<bool> srcChannelUsed;

public:
    std::vector<DQPacket<T>*> select(std::vector<DQPacket<T>*> &, DQPacket<T> *null);

    CrossBarDedi(uint32_t size, uint32_t n_ops);

    const std::string name() {return "CrossBarDediX4";};
};

template<class T>
CrossBarDedi<T>::CrossBarDedi(uint32_t size, uint32_t n_ops):
    size(size),
    nOps(n_ops),
    nChannels(size/nOps),
    destChannelUsed(nChannels),
    srcChannelUsed(nChannels)
{
    for (uint32_t i = 0; i < nChannels; i++) {
        prioList.push_back(i);
    }
    for (uint32_t i = 0; i < nChannels; i++) {
        opPrioList.push_back(i);
    }
}

template<class T>
std::vector<DQPacket<T>*>
CrossBarDedi<T>::select(std::vector<DQPacket<T>*> &inputs, DQPacket<T> *null)
{
    std::vector<DQPacket<T>*> outputs(size, null);
    assert(inputs.size() == size);
    std::fill(srcChannelUsed.begin(), srcChannelUsed.end(), false);
    std::fill(destChannelUsed.begin(), destChannelUsed.end(), false);

    for (uint32_t i: prioList) {
        unsigned sent = 0;
        for (uint32_t op: opPrioList) {
            auto pkt = null;
            if (inputs[i * nOps + op] && inputs[i * nOps + op]->valid) {
                pkt = inputs[i * nOps + op];
                if (pkt && pkt->valid) {
                    uint32_t dest_chan = pkt->dest/nOps;
                    bool is_dest = (pkt->dest % nOps) == 0;
                    bool is_src = !is_dest;

                    if (is_src && !srcChannelUsed[dest_chan]) {
                        DPRINTF(CrossBarNarrow, "Pass pkt[%i] to [%i]\n", i, pkt->dest);
                        outputs[pkt->dest] = pkt;
                        srcChannelUsed[dest_chan] = true;
                        sent++;

                    } else if (is_dest && !destChannelUsed[dest_chan]) {
                        DPRINTF(CrossBarNarrow, "Pass pkt[%i] to [%i]\n", i, pkt->dest);
                        outputs[pkt->dest] = pkt;
                        destChannelUsed[dest_chan] = true;
                        sent++;

                    } else {
                        DPRINTF(CrossBarNarrow,
                                "Reject pkt[%i] req to [%i], conflicted\n",
                                i, pkt->dest);
                    }
                }
            }
            if (sent >= 2) {
                break;
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
