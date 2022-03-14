#ifndef __SET_BYPASS_LOGIC_H_
#define __SET_BYPASS_LOGIC_H_

#include <cstdint>
#include <queue>
#include <unordered_set>

#include "base/types.hh"
#include "dev/controlplane.hh"
#include "mem/cache/base.hh"
#include "mem/cache/cache.hh"
#include "mem/packet.hh"
#include "mem/token_bucket.hh"

class SetBypassLogic
{
    private:
        Cache * cache;
        const uint32_t numSets;
        std::map<uint32_t, std::vector<uint>*> jobSetStatMap;
        std::map<uint32_t, std::vector<uint>*> lastTJobSetStatMap;
    public:
        bool trainingStarted;
        bool QosStarted;
        std::map<uint32_t, uint> jobThreshold;
        SetBypassLogic(Cache *_cache,uint32_t nSets);
        void recordLastTTI();
        bool couldBypass(uint32_t qosId, uint32_t setNum);
};
#endif
