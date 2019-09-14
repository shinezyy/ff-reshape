#ifndef __CPU_FANOUT_PRED_FEAT_HH__
#define __CPU_FANOUT_PRED_FEAT_HH__

#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "base/types.hh"

struct FPFeatures {
    std::vector<Addr> pastPCs;
    Addr lastCallSite;
    boost::dynamic_bitset<> globalBranchHist;
    boost::dynamic_bitset<> localBranchHist;
};

#endif
