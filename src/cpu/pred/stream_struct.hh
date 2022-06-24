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

    FetchStream(): streamStart(0), ended(false), streamEnd(0), target(0),
        branchAddr(0), branchType(0){}

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
    FsqID fsqID;
    FtqEntry(): startPC(0), endPC(0), takenPC(0), taken(false), target(0), fsqID(0) {}
};

// struct FetchStreamWithID: public FetchStream {
//     FsqID id;
//     bool operator==(const FetchStreamWithID &other) const {
//         return id == other.id;
//     }
//     FetchStreamWithID(const FetchStream &stream, FsqID id) : FetchStream(stream), id(id) {}
// }

// struct FtqEntryWithID: public FtqEntry {
//     FtqID id;
//     bool operator==(const FtqEntryWithID &other) const {
//         return id == other.id;
//     }
//     FtqEntryWithID(const FtqEntry &entry, FtqID id) : FtqEntry(entry), id(id) {}
// }

#endif // __CPU_PRED_STREAM_STRUCT_HH__
