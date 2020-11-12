//
// Created by zyy on 11/9/20.
//

#ifndef __GEM5_LOOP_INFO_HH__
#define __GEM5_LOOP_INFO_HH__

#include <base/types.hh>

struct LoopInfo
{
    bool valid;
    unsigned loopSize;
    int restIterations;
    Addr loopStart;
    Addr loopEnd;
};

#endif //__GEM5_LOOP_INFO_HH__
