#include "set_bypass_logic.hh"

SetBypassLogic::SetBypassLogic(Cache *_cache,uint32_t _numSets)
    : cache(_cache),numSets(_numSets),
    trainingStarted(false),QosStarted(false)
{
    for (size_t i = 0; i < LvNATasks::NumId; i++)
    {
        jobSetStatMap[i] = new std::vector<uint>(numSets,0);
        lastTJobSetStatMap[i] = new std::vector<uint>(numSets,0);
        jobThreshold[i] = 4;
    }
}

void
SetBypassLogic::recordLastTTI()
{
    for (size_t i = 0; i < LvNATasks::NumId; i++)
    {
        delete(lastTJobSetStatMap[i]);
        lastTJobSetStatMap[i] = jobSetStatMap[i];
        jobSetStatMap[i] = new std::vector<uint>(numSets,0);
    }
}

bool
SetBypassLogic::couldBypass(uint32_t qosId, uint32_t setNum)
{
    if (!trainingStarted)
    {
        return true;
    }
    std::set<uint32_t> *jobsetp =  cache->runningJobQosPtr;
    std::vector<uint>* set_stat_ptr = jobSetStatMap[qosId];
    (*set_stat_ptr)[setNum]++;
    if (!QosStarted)
    {
        return true;
    }
    if (jobsetp->count(qosId) != 0)
    {
        //it is a high priority job
        return true;
    }
    else
    {
        for (const auto &j: (*jobsetp))
        {
            std::vector<uint>* last_set_stat_ptr = lastTJobSetStatMap[j];
            if ((*last_set_stat_ptr)[setNum]>=jobThreshold[j])
            {
                return false;
            }
        }
        return true;
    }
}