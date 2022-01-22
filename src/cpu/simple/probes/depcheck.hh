//
// Created by yqszxx on 1/21/22.
//

#ifndef GEM5_DEPCHECK_HH
#define GEM5_DEPCHECK_HH

#include "base/statistics.hh"
#include "cpu/simple_thread.hh"
#include "params/DepCheck.hh"
#include "sim/probe/probe.hh"

namespace gem5
{

class DepCheck : public ProbeListenerObject
{
public:
    DepCheck(const DepCheckParams &p);

    void init() override;

    void regProbeListeners() override;

    void profile(const std::pair<SimpleThread*, StaticInstPtr>&);

private:
    uint32_t groupSize;

    struct DepCheckStats : public statistics::Group
    {
        DepCheckStats(statistics::Group *parent);

        statistics::Scalar dependent;

        statistics::Scalar interGroup;

        statistics::Scalar intraGroup;
    } stats;

    uint64_t instCount = 0;

    uint64_t lastProducer[64] {};  // 32 int + 32 fp
};

}

#endif //GEM5_DEPCHECK_HH
