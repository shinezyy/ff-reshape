#include "cpu/pred/decoupled_branch_pred.hh"
#include "debug/DecoupleBP.hh"
#include "cpu/o3/decoupled_fetch.hh"

DecoupledBranchPred::DecoupledBranchPred(const Params &params)
    : SimObject(params),
    streamPred(params.stream_pred),
    streamUBTB(params.stream_ubtb)
{
    commitHistory.resize(historyBits, 0);
    speculativeHistory.resize(historyBits, 0);
    s2CtrlPC = 0x80000000;
    recordPrediction(0x80000000, unlimitedStreamLen, predictionID, speculativeHistory, 0);

    // TODO: remove this
    ftqSize = 64;
    fetchStreamQueueSize = 64;
    streamMiss = true;
    s0StreamPC = 0x80000000;
    ftqEnqPC = 0x80000000;
}

void DecoupledBranchPred::tick()
{
    // s2
    // get stream from UBTB

    // bool inconsistent = check_prediction(s1BackingPred, s1UbtbPred);
    // bool overriding = false;
    // if (inconsistent) {
    //     overrideStream();
    //     overrideUpdateUBTB();  // do we update it after override?
    // }
    // if (s1BackingPred.valid) {
    //     incPredictionID();
    //     add2FTQ(s1BackingPred, predictionID);
    //     recordPrediction(s1BackingPred.bbStart, s1BackingPred.streamLength, s1BackingPred.nextStream,
    //                      s1History, predictionID);
    // }
    // s2Pred = s1BackingPred;
    // s1BackingPred.valid = false;
    // updateS2Hist();

    // // s1
    // // get stream prediction initated XX cycles before
    // auto [branch_pc, stream_payload] = streamPred->getStreamS1();
    // // sanity check
    // if (branch_pc) {  // pc = 0 indicates no valid prediction
    //     // the pc of return predicton must be sent X cycle ago
    //     assert(branch_pc == s1StreamPC);
    // }
    // if (stream_payload.endIsRet && !ras.empty()) {
    //     stream_payload.nextStream = ras.top().instAddr();
    //     stream_payload.rasUpdated = true;
    // }
    // s1StreamPC = s0StreamPC;

    // // s1BackingPred = stream_payload;
    // // temporarily s1BackingPred = s0ubtbPred;
    // if (s0UbtbPred.valid) {
    //     s1BackingPred = s0UbtbPred;
    //     DPRINTF(DecoupleBP, "Forward s0 ubtb pred (%#lx..%u) to s1 backing pred\n", s0UbtbPred.bbStart,
    //             s0UbtbPred.streamLength);
    // }

    // s1UbtbPred = s0UbtbPred;
    // updateS1Hist();

    // // s0
    // streamPred->putPCHistory(s0StreamPC, s0History);
    // streamUBTB->putPCHistory(s0StreamPC, s0History);

    // s0UbtbPred = streamUBTB->getStream();
    // if (s0UbtbPred.valid) {
    //     s0StreamPC = s0UbtbPred.nextStream;
    //     DPRINTF(DecoupleBP, "Update s0 stream (%#lx..%u) with ubtb\n", s0UbtbPred.bbStart,
    //             s0UbtbPred.streamLength);
    // }
    // if (overriding) {
    //     s0StreamPC = s2Pred.nextStream;
    // }
    // updateS0Hist();

    // ftq is at the next stage of fetchStreamQueue
    // so we deal with it first
    if (ftq.size() < ftqSize) {
        // ftq can accept new cache lines,
        // get cache lines from fetchStreamQueue
        if (fetchStreamQueue.size() != 0) {
            // this should be a reference...right?
            const FetchStream dummy = {0,0,0,0,0,0,0};
            // find current stream with ftqEnqfsqID in fetchStreamQueue
            auto streamToEnq = find(fetchStreamQueue.begin(), fetchStreamQueue.end(), FetchStreamWithID(dummy,ftqEnqFsqID));
            if (streamToEnq != fetchStreamQueue.end()) {
                if (!streamToEnq->ended) {
                    assert(ftqEnqPC < s0StreamPC); // TODO: this may break
                    auto ftqEntry = new FtqEntryWithID;
                    if (!streamToEnq->hasEnteredFtq) {
                        ftqEntry.startPC = streamToEnq->streamStart;
                        streamToEnq->hasEnteredFtq = true;
                        ftqEnqPC = bufferAlignPC(ftqEntry.startPC + 0x20, fetchBufferMask);
                    } else {
                        ftqEntry.startPC = ftqEnqPC;
                        ftqEnqPC += 0x20;
                    }
                    // align to the end of current cache line
                    // TODO: parameterize, use blockAlignPC
                    ftqEntry.endPC = bufferAlignPC(ftqEntry.startPC + 0x20, fetchBufferMask);

                    ftqEntry.taken = false;
                    ftqEntry.takenPC = 0;
                    ftqEntry.id = ftqID;

                    ftq.push_back(ftqEntry);
                    ftqID++;
                    DPRINTF(DecoupleBP, "a miss stream inc ftqID: %lu -> %lu\n", ftqEntry.id, ftqID);
                } else {
                    assert(ftqEnqPC < streamEnd);
                    auto ftqEntry = new FtqEntryWithID;
                    // TODO: reduce duplicate logic
                    if (!streamToEnq->hasEnteredFtq) {
                        ftqEntry.startPC = streamToEnq->streamStart;
                        streamToEnq->hasEnteredFtq = true;
                        ftqEnqPC = bufferAlignPC(ftqEntry.startPC + 0x20, fetchBufferMask);
                    } else {
                        ftqEntry.startPC = ftqEnqPC;
                        ftqEnqPC += 0x20;
                    }
                    // check if this is the last cache line of the stream
                    bool end_is_within_line = streamToEnq->streamEnd - bufferAlignPC(ftqEntry.startPC, fetchBufferMask) <= 0x20;
                    // whether the first byte of the branch instruction lies in this cache line
                    bool branch_is_within_line = streamToEnq->branchAddr - bufferAlignPC(ftqEntry.startPC, fetchBufferMask) < 0x20;
                    if (end_is_within_line || branch_is_within_line) {
                        ftqEntry.endPC = streamToEnq->streamEnd;
                        ftqEntry.taken = true;
                        ftqEntry.takenPC = streamToEnq->branchAddr;
                        // done with this stream
                        DPRINTF(DecoupleBP, "done stream %lu entering ftq %lu\n", ftqEnqFsqID, ftqID);
                        ftqEnqFsqID++;
                    } else {
                        ftqEntry.endPC = bufferAlignPC(ftqEntry.startPC + 0x20, fetchBufferMask);
                        ftqEntry.taken = false;
                        ftqEntry.takenPC = 0;
                    }
                    ftqEntry.id = ftqID;

                    ftq.push_back(FtqEntry);
                    ftqID++;
                    DPRINTF(DecoupleBP, "an ended stream inc ftqID: %lu -> %lu\n", ftqEntry.id, ftqID);
                }
            }
        } else {
            // no stream that have not entered ftq
            DPRINTF(DecoupleBP, "no stream to enter ftq in fetchStreamQueue\n");
        }
    }

    if (fetchStreamQueue.size() < fetchStreamQueueSize) {
        // if queue empty, should make predictions
        if (fetchStreamQueue.size() == 0) {
            auto entry = new FetchStream;
            entry.streamStart = s0StreamPC;
            // TODO: for real predictors it should be hit signal
            entry.ended = false;
            // TODO: when hit, signals below should be the prediction result
            entry.streamEnd = 0;
            entry.target = 0;
            entry.branchAddr = 0;
            entry.branchType = 0;
            entry.hasEnteredFtq = false;
            entry.id = fsqID;
            fetchStreamQueue.push_back(entry);
            fsqID++;
            DPRINTF(DecoupleBP, "an new stream inc fsqID when empty: %lu -> %lu\n", entry.id, fsqID);
        } else {
            auto back = fetchStreamQueue.back();
            if (back.ended) {
                // make new predictions
                auto entry = new FetchStream;
                entry.streamStart = s0StreamPC;
                // TODO: remove duplicate logic
                // TODO: for real predictors it should be hit signal
                entry.ended = false;
                // TODO: when hit, signals below should be the prediction result
                entry.streamEnd = 0;
                entyr.target = 0;
                entry.branchAddr = 0;
                entry.branchType = 0;
                entry.hasEnteredFtq = false;
                entry.id = fsqID;
                fetchStreamQueue.push_back(entry);
                fsqID++;
                DPRINTF(DecoupleBP, "an new stream inc fsqID: %lu -> %lu\n", entry.id, fsqID);
            } else {
                bool hit = false; // TODO: use prediction result
                // if hit, do something
                if (hit) {
                    // TODO: use prediction result
                    back.ended = true;
                    back.streamEnd = 0;
                    back.target = 0;
                    back.branchAddr = 0;
                    back.branchType = 0;
                    back.hasEnteredFtq = false;
                    s0StreamPC = back.target;
                } else {
                    // streamMiss = true;
                    s0StreamPC += 0x20;
                }
            }
        }
    }


    // s0StreamPC += 0x20; // TODO: use cache line parameters

}

void DecoupledBranchPred::recordPrediction(Addr stream_start, StreamLen stream_len, Addr next_stream,
                                           const boost::dynamic_bitset<> &history, PredictionID id)
{
    DPRINTF(DecoupleBP, "Make prediction: id: %u, stream=0x%lx, length=%u\n", id, stream_start, stream_len);
    assert(bpHistory.find(id) == bpHistory.end());
    bpHistory[id] = BPHistory{history, stream_start, stream_len, next_stream};
}

void DecoupledBranchPred::updateS0Hist()
{
}

void DecoupledBranchPred::updateS1Hist()
{
}

void DecoupledBranchPred::updateS2Hist()
{
}

// taken, target, ftq_empty
std::tuple<bool, TheISA::PCState, bool>
DecoupledBranchPred::willTaken(TheISA::PCState &pc)
{
    // if taken, store prediction history here
    
    // read ftq using fetchFtqID and check if we run out of entries
    // if so, we need to stall the fetch engine
    const FtqEntry dummy = {0,0,0,0};
    auto ftqEntryToFetch = find(ftq.begin(), ftq.end(), FtqEntryWithID(dummy, fetchFtqID));
    // found corresponding entry
    if (ftqEntryToFetch != ftq.end()) {
        auto start = ftqEntryToFetch->startPC;
        auto end = ftqEntryToFetch->endPC;
        auto takenPC = ftqEntryToFetch->takenPC;
        assert(pc < end && pc >= start);
        bool taken = pc == takenPC && ftqEntryToFetch->taken;
        assert(!taken); // TODO: remove this and use PCState as target
        bool run_out_of_this_entry = pc.nextInstAddr() == endPC;
        if (run_out_of_this_entry) {
            DPRINTF(DecoupledBP, "running out of ftq entry %lu", fetchFtqID);
            fetchFtqID++;
            // find if there is a new entry
            auto ftqEntryToFetch = find(ftq.begin(), ftq.end(), FtqEntryWithID(dummy, fetchFtqID));
            bool empty = ftqEntryToFetch == ftq.end();
            return std::make_tuple(taken, TheISA::PCState(0), empty);
        }
        return std::make_tuple(false, TheISA::PCState(0), false);
    } else {
        return std::make_tuple(false, TheISA::PCState(0), true);
    }

}

void
DecoupledBranchPred::notifyStreamSeq(const InstSeqNum seq)
{
    // We cannot index BP history with Instseq num, because it does not exists
    // when make a prediction.
}

void DecoupledBranchPred::add2FTQ(const StreamPrediction &fetchStream, PredictionID id)
{
    DPRINTF(DecoupleBP, "Add prediction %u to FTQ, start: %lx\n", id, fetchStream.bbStart);
    ftq.emplace(id, StreamPredictionWithID{fetchStream, id});
}

bool DecoupledBranchPred::check_prediction(const StreamPrediction &ubtb_prediction,
                                           const StreamPrediction &main_predictor_prediction)
{
    return false;
}

void DecoupledBranchPred::overrideStream()
{
}

void DecoupledBranchPred::overrideUpdateUBTB()
{
}

void DecoupledBranchPred::controlSquash(const PredictionID pred_id, const TheISA::PCState control_pc,
                                        const TheISA::PCState &corr_target, bool is_conditional, bool is_indirect,
                                        bool actually_taken)
{
    /* two cases:
     *  sn exists in bpHistory, which indicates that BP already knows it is a control
     *  sn does not exist in bpHistory, which indicates that BP treates is as a non-control or not-taken branch
     */
    DPRINTF(DecoupleBP,
            "Control squash: pred_id=%u, control_pc=0x%lx, corr_target=0x%lx, is_conditional=%u, is_indirect=%u, "
            "actually_taken=%u\n",
            pred_id, control_pc.instAddr(), corr_target.instAddr(), is_conditional, is_indirect, actually_taken);

    auto it = bpHistory.find(pred_id);
    assert(it != bpHistory.end());

    DPRINTF(DecoupleBP, "Found prediction %u (start: %#lx) in bpHistory\n", pred_id, it->second.streamStart);

    streamUBTB->update(pred_id, it->second.streamStart, control_pc.instAddr(), corr_target.instAddr(), is_conditional,
                       is_indirect, actually_taken, it->second.history);

    streamPred->update(pred_id, it->second.streamStart, control_pc.instAddr(), corr_target.instAddr(), is_conditional,
                       is_indirect, actually_taken, it->second.history);
    s0StreamPC = corr_target.instAddr();

    bpHistory.erase(pred_id);
    ftq.erase(pred_id);
    DPRINTF(DecoupleBP, "bpHistory size: %u, ftp size: %u\n", bpHistory.size(), ftq.size());
    // here, in fact we make another prediction
}

void DecoupledBranchPred::nonControlSquash(const InstSeqNum squashed_sn)
{
    for (auto &control : bpHistory) {
        if (control.first >= squashed_sn) {
            // squash it and release resource
        } else {
            break;
        }
    }
}

void DecoupledBranchPred::commitInst(const InstSeqNum committedSeq)
{
    // commit controls in local prediction history buffer to committedSeq
    // mark all committed control instructions as correct
    for (auto &control : bpHistory) {
        if (control.first <= committedSeq) {
            // This is a predicted taken control, mark it as correct
        } else {
            break;
        }
    }
}