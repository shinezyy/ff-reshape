//
// Created by yqszxx on 1/21/22.
//

#include "base/trace.hh"
#include "debug/DepCheck.hh"
#include "depcheck.hh"

namespace gem5
{

DepCheck::DepCheckStats::DepCheckStats(statistics::Group *parent)
        : statistics::Group(parent, "depcheck"),
          ADD_STAT(dependent, statistics::units::Count::get(),
                   "Number of instructions that has value dependencies"),
          ADD_STAT(interGroup, statistics::units::Count::get(),
                   "Number of instructions that all dependencies "
                   "are inter-group dependencies"),
          ADD_STAT(intraGroup, statistics::units::Count::get(),
                   "Number of instructions that has intra-group dependencies")
{
}

DepCheck::DepCheck(const DepCheckParams &p)
      : ProbeListenerObject(p),
        groupSize(p.groupSize),
        stats(this)
{
}

void DepCheck::init() {
    DPRINTF(DepCheck, "DepCheck, groupSize=%u\n", groupSize);
}

void DepCheck::regProbeListeners() {
    typedef ProbeListenerArg<DepCheck, std::pair<SimpleThread*, StaticInstPtr>>
            SimPointListener;
    listeners.push_back(new SimPointListener(this, "Commit",
                                             &DepCheck::profile));
}

void DepCheck::profile(const std::pair<SimpleThread *, StaticInstPtr> &p) {
    StaticInstPtr inst = p.second;

    uint8_t numSrcRegs = inst->numSrcRegs();
    uint8_t numDestRegs = inst->numDestRegs();

    bool hasDependencies = false;
    bool hasIntraDep = true;

    const uint64_t currentGroup = instCount / groupSize;

    if (instCount % groupSize == 0) { // first inst in group
        accessedInGroup.clear();
    }

    for (int i = 0; i < numSrcRegs; i++)  {
        RegId r = inst->srcRegIdx(i);

        if (r.classValue() == IntRegClass || r.classValue() == FloatRegClass) {
            hasDependencies = true;

            int rid = r.index();
            if (r.classValue() ==  FloatRegClass) {
                rid += 32;
            }

            uint64_t producer = lastProducer[rid];
            uint64_t producerGroup = producer / groupSize;

            if (accessedInGroup.count(rid) == 0 &&
                producerGroup != currentGroup) {
                hasIntraDep = false;
                accessedInGroup.insert(rid);
            }
        }
    }

    if (numDestRegs != 0) {
        RegId r = inst->destRegIdx(0);

        if (r.classValue() == IntRegClass || r.classValue() == FloatRegClass) {
            lastProducer[
                    r.index() + (r.classValue() == FloatRegClass ? 32 : 0)
                    ] = instCount;
        }
    }

    if (hasDependencies) {
        ++stats.dependent;
        if (hasIntraDep) {
            ++stats.intraGroup;
        } else {
            ++stats.interGroup;
        }
    }

    ++instCount;
}

}