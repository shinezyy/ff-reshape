#ifndef __CPU_PRED_STREAM_STRUCT_HH__
#define __CPU_PRED_STREAM_STRUCT_HH__

#include <boost/dynamic_bitset.hpp>

#include "base/types.hh"
#include "cpu/inst_seq.hh"

struct FetchStream {
    Addr streamStart;
    bool ended;
    Addr streamEnd;
    // TODO: use PCState for target(gem5 specific)
    Addr target;
    Addr branchAddr;
    int branchType;

    // TODO: remove signals below
    bool hasEnteredFtq;

};

struct FetchingStream: public FetchStream {
    Addr curPC;
};

struct IdealStreamStorage {
    // Addr tag;  // addr of the taken branch?
    Addr bbStart;
    Addr bbEnd;
    Addr nextStream;
    unsigned length;
    // unsigned instCount;
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
    bool valid;
    bool endIsRet;
    bool rasUpdated;
};

struct StreamPredictionWithID: public StreamPrediction {
    PredictionID id;

    StreamPredictionWithID(const StreamPrediction &pred, PredictionID id) : StreamPrediction(pred), id(id) {}
};

using StreamLen = uint16_t;
#define unlimitedStreamLen (std::numeric_limits<StreamLen>::max())

// each entry corrsponds to a cache line
struct FtqEntry {
    Addr startPC;
    Addr endPC; // TODO: use PCState and it can be included in takenPC
    Addr takenPC; // TODO: use PCState
    bool taken;
    Addr target; // TODO: use PCState
};

struct FetchStreamWithID: public FetchStream {
    PredictionID id;
    bool operator==(const FetchStreamWithID &other) const {
        return id == other.id;
    }
    FetchStreamWithID(const FetchStream &stream, PredictionID id) : FetchStream(stream), id(id) {}
}

struct FtqEntryWithID: public FtqEntry {
    PredictionID id;
    bool operator==(const FtqEntryWithID &other) const {
        return id == other.id;
    }
    FtqEntryWithID(const FtqEntry &entry, PredictionID id) : FtqEntry(entry), id(id) {}
}

#endif // __CPU_PRED_STREAM_STRUCT_HH__
