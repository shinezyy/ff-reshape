//
// Created by zyy on 19-6-11.
//

#ifndef __FF_DIEWC_HH__
#define __FF_DIEWC_HH__


#include <cpu/timebuf.hh>

namespace FF{

template<class Impl>
class FFDIEWC //dispatch, issue, execution, writeback, commit
{

private:
    //Typedefs from Impl
    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::O3CPU O3CPU;

    typedef typename CPUPol::LSQ LSQ;
    typedef typename CPUPol::DecodeStruct DecodeStruct;

    /** Decode instruction queue interface. */
    TimeBuffer<DecodeStruct> *decodeQueue;

    /** Wire to get decode's output from decode queue. */
    typename TimeBuffer<DecodeStruct>::wire fromDecode;

    /** Variable that tracks if decode has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Structures whose free entries impact the amount of instructions that
     * can be renamed.
     */
    struct FreeEntries {
        unsigned iqEntries;
        unsigned robEntries;
        unsigned lqEntries;
        unsigned sqEntries;
    };

    FreeEntries freeEntries;

    /** Records if pipeline needs to serialize on the next instruction for any
     * thread.
     */
    bool serializeOnNextInst;
};

}


#endif //__FF_DIEWC_HH__
