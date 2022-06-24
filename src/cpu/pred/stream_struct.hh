#ifndef __CPU_PRED_STREAM_STRUCT_HH__
#define __CPU_PRED_STREAM_STRUCT_HH__

#include <boost/dynamic_bitset.hpp>

#include "base/types.hh"
#include "cpu/inst_seq.hh"

struct FetchStream {
    Addr streamStart;
    bool pred_ended;
    Addr pred_streamEnd;
    // TODO: use PCState for target(gem5 specific)
    Addr pred_target;
    Addr pred_branchAddr;
    int pred_branchType;

    // for commit, write at redirect or fetch
    InstSeqNum branchSeq;
    bool exe_ended;
    Addr exe_streamEnd;
    // TODO: use PCState for target(gem5 specific)
    Addr exe_target;
    Addr exe_branchAddr;
    int exe_branchType;
    // TODO: remove signals below
    bool hasEnteredFtq;

    FetchStream(): streamStart(0), pred_ended(false), pred_streamEnd(0), pred_target(0),
        pred_branchAddr(0), pred_branchType(0), branchSeq(-1), exe_ended(false), exe_streamEnd(0), exe_target(0),
        exe_branchAddr(0), exe_branchType(0), hasEnteredFtq(0) {}

    // the default exe result should be consistent with prediction
    void set_exe_with_pred() {
        exe_ended = pred_ended;
        exe_streamEnd = pred_streamEnd;
        exe_target = pred_target;
        exe_branchAddr = pred_branchAddr;
        exe_branchType = pred_branchType;
    }
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
