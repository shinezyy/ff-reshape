#ifndef __CPU_PRED_STREAM_STRUCT_HH__
#define __CPU_PRED_STREAM_STRUCT_HH__


#include "base/types.hh"
#include "cpu/inst_seq.hh"

struct FetchStream {
    Addr streamStart;
    Addr streamEnd;
};

struct FetchingStream: public FetchStream {
    Addr curPC;
};

struct IdealStreamStorage {
    Addr tag;  // addr of the taken branch?
    Addr bbStart;
    Addr bbEnd;
    Addr nextStream;
    unsigned length;
    unsigned instCount;
    unsigned hysteresis;
    bool endIsRet;
};

struct RealStreamStorage {

};

using StreamStorage = IdealStreamStorage;


struct StreamPrediction {
    Addr bbStart;
    Addr bbEnd;
    Addr nextStream;
    uint16_t streamLength;
    bool endIsRet;
    bool rasUpdated;
};


#endif // __CPU_PRED_STREAM_STRUCT_HH__
