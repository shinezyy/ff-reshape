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
    ftqSize = 32;
    fetchStreamQueueSize = 64;
    streamMiss = true;
    s0StreamPC = 0x80000000;
    ftqEnqPC = 0x80000000;
    fetchReadFtqEntryBufferValid = false;
    fetchReadFtqEntryBuffer = std::make_pair(0, FtqEntry());
}

void DecoupledBranchPred::tick()
{
    DPRINTF(DecoupleBP, "ticking BP, s0StreamPC = %x, ftqEnqPC=%x, ftqEnqFsqID=%lu, ftqID=%lu, fsqID=%lu, fetchFtqID=%lu, bufferValid=%d\n",
        s0StreamPC, ftqEnqPC, ftqEnqFsqID, ftqID, fsqID, fetchFtqID, fetchReadFtqEntryBufferValid);
    DPRINTF(DecoupleBP, "ftq entires %d, fsq entries %d\n", ftq.size(), fetchStreamQueue.size());
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
            // find current stream with ftqEnqfsqID in fetchStreamQueue
            auto it = fetchStreamQueue.find(ftqEnqFsqID);
            if (it != fetchStreamQueue.end()) {
                auto streamToEnq = it->second;
                if (!streamToEnq.pred_ended) {
                    assert(ftqEnqPC <= s0StreamPC); // TODO: this may break
                    auto ftqEntry = FtqEntry();
                    if (!streamToEnq.hasEnteredFtq) {
                        ftqEntry.startPC = streamToEnq.streamStart;
                        streamToEnq.hasEnteredFtq = true;
                        it->second = streamToEnq;
                        ftqEnqPC = alignToCacheLine(ftqEntry.startPC + 0x40);
                    } else {
                        ftqEntry.startPC = ftqEnqPC;
                        ftqEnqPC = alignToCacheLine(ftqEntry.startPC + 0x40);
                    }
                    // align to the end of current cache line
                    // TODO: parameterize, use blockAlignPC
                    ftqEntry.endPC = alignToCacheLine(ftqEntry.startPC + 0x40);

                    ftqEntry.taken = false;
                    ftqEntry.takenPC = 0;
                    ftqEntry.fsqID = ftqEnqFsqID;
                    ftq.insert(std::make_pair(ftqID, ftqEntry));
                    ftqID++;
                    DPRINTF(DecoupleBP, "a miss stream inc ftqID: %lu -> %lu\n", ftqID-1, ftqID);
                    printFtqEntry(ftqEntry);
                } else {
                    assert(ftqEnqPC < streamToEnq.pred_streamEnd);
                    auto ftqEntry = FtqEntry();
                    // TODO: reduce duplicate logic
                    if (!streamToEnq.hasEnteredFtq) {
                        ftqEntry.startPC = streamToEnq.streamStart;
                        streamToEnq.hasEnteredFtq = true;
                        it->second = streamToEnq;
                        ftqEnqPC = alignToCacheLine(ftqEntry.startPC + 0x40);
                    } else {
                        ftqEntry.startPC = ftqEnqPC;
                        ftqEnqPC += 0x40;
                    }
                    // check if this is the last cache line of the stream
                    bool end_is_within_line = streamToEnq.pred_streamEnd - alignToCacheLine(ftqEntry.startPC) <= 0x40;
                    // whether the first byte of the branch instruction lies in this cache line
                    bool branch_is_within_line = streamToEnq.pred_branchAddr - alignToCacheLine(ftqEntry.startPC) < 0x40;
                    if (end_is_within_line || branch_is_within_line) {
                        ftqEntry.endPC = streamToEnq.pred_streamEnd;
                        ftqEntry.taken = true;
                        ftqEntry.takenPC = streamToEnq.pred_branchAddr;
                        // done with this stream
                        DPRINTF(DecoupleBP, "done stream %lu entering ftq %lu\n", ftqEnqFsqID, ftqID);
                        ftqEnqFsqID++;
                    } else {
                        ftqEntry.endPC = alignToCacheLine(ftqEntry.startPC + 0x40);
                        ftqEntry.taken = false;
                        ftqEntry.takenPC = 0;
                    }
                    ftqEntry.fsqID = ftqEnqFsqID;

                    ftq.insert(std::make_pair(ftqID, ftqEntry));
                    ftqID++;
                    DPRINTF(DecoupleBP, "an ended stream inc ftqID: %lu -> %lu\n", ftqID-1, ftqID);
                    printFtqEntry(ftqEntry);
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

            auto entry = FetchStream();
            entry.streamStart = s0StreamPC;
            // TODO: for real predictors it should be hit signal
            entry.pred_ended = false;
            // TODO: when hit, the remaining signals should be the prediction result
            entry.set_exe_with_pred();
            fetchStreamQueue.insert(std::make_pair(fsqID, entry));
            // only if the stream is predicted to be ended can we inc fsqID
            if (entry.pred_ended) {
                fsqID++;
            }
            DPRINTF(DecoupleBP, "an new stream inc fsqID when empty: %lu -> %lu\n", fsqID-1, fsqID);
            printFsqEntry(entry);
        } else {
            auto it = fetchStreamQueue.end();
            it--;
            auto back = it->second;
            if (back.pred_ended || back.exe_ended) {
                // make new predictions
                auto entry = FetchStream();
                entry.streamStart = s0StreamPC;
                // TODO: remove duplicate logic
                // TODO: for real predictors it should be hit signal
                entry.pred_ended = false;
                // TODO: when hit, the remaining signals should be the prediction result
                entry.set_exe_with_pred();
                fetchStreamQueue.insert(std::make_pair(fsqID, entry));
                // only if the stream is predicted to be ended can we inc fsqID
                if (entry.pred_ended) {
                    fsqID++;
                }
                DPRINTF(DecoupleBP, "an new stream inc fsqID: %lu -> %lu\n", fsqID-1, fsqID);
                printFsqEntry(entry);
            } else {
                bool hit = false; // TODO: use prediction result
                // if hit, do something
                if (hit) {
                    // TODO: use prediction result
                    back.pred_ended = true;
                    back.pred_streamEnd = 0;
                    back.pred_target = 0;
                    back.pred_branchAddr = 0;
                    back.pred_branchType = 0;
                    back.hasEnteredFtq = false;
                    s0StreamPC = back.pred_target;
                    DPRINTF(DecoupleBP, "fsq entry of id %lu modified to:\n", it->first);
                    // pred ended, inc fsqID
                    fsqID++;
                    printFsqEntry(back);
                } else {
                    // streamMiss = true;
                    s0StreamPC += 0x40;
                }
            }
        }
    }
    DPRINTF(DecoupleBP, "exit ticking BP, s0StreamPC = %x, ftqEnqPC=%x, ftqEnqFsqID=%lu, ftqID=%lu, fsqID=%lu, fetchFtqID=%lu, bufferValid=%d\n",
        s0StreamPC, ftqEnqPC, ftqEnqFsqID, ftqID, fsqID, fetchFtqID, fetchReadFtqEntryBufferValid);
    DPRINTF(DecoupleBP, "ftq entires %d, fsq entries %d\n", ftq.size(), fetchStreamQueue.size());


    // s0StreamPC += 0x40; // TODO: use cache line parameters

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

bool
DecoupledBranchPred::tryToFillFtqEntryBuffer()
{
    if (!fetchReadFtqEntryBufferValid || fetchReadFtqEntryBuffer.first != fetchFtqID) {
        auto iter = ftq.find(fetchFtqID);
        if (iter != ftq.end()) {
            DPRINTF(DecoupleBP, "found ftq entry with id %lu, writing to fetchReadFtqEntryBuffer\n", fetchFtqID);
            fetchReadFtqEntryBufferValid = true;
            fetchReadFtqEntryBuffer.first = fetchFtqID;
            fetchReadFtqEntryBuffer.second = iter->second;
        } else {
            DPRINTF(DecoupleBP, "not found ftq entry with id %lu, buffervalid %d\n",
                fetchFtqID, fetchReadFtqEntryBufferValid);
            if (ftq.size() > 0) {
                --iter;
                DPRINTF(DecoupleBP, "last entry of ftq is %lu\n", iter->first);
                assert(iter->first < fetchFtqID);
            }
            return false;
        }
    }
    return true;
}

// taken, target, ftq_entry_done/empty
std::tuple<bool, TheISA::PCState, bool>
DecoupledBranchPred::willTaken(TheISA::PCState &pc)
{
    // if taken, store prediction history here
    
    // read ftq using fetchFtqID and check if we run out of entries
    // if so, we need to stall the fetch engine
    DPRINTF(DecoupleBP, "looking up pc %x\n", pc.instAddr());
    FtqEntry ftqEntryToFetch = FtqEntry();
    auto hasSomethingToFetch = fetchReadFtqEntryBufferValid && fetchReadFtqEntryBuffer.first == fetchFtqID;
    DPRINTF(DecoupleBP, "fetchFtqID %lu, fetchReadFtqEntryBuffer id %lu\n", fetchFtqID, fetchReadFtqEntryBuffer.first);
    printFtqEntry(fetchReadFtqEntryBuffer.second);
    if (!hasSomethingToFetch) {
        return std::make_tuple(false, TheISA::PCState(0), true);
    }
    ftqEntryToFetch = fetchReadFtqEntryBuffer.second;
    // found corresponding entry
    auto start = ftqEntryToFetch.startPC;
    auto end = ftqEntryToFetch.endPC;
    auto takenPC = ftqEntryToFetch.takenPC;
    DPRINTF(DecoupleBP, "ftq entry start: %x, end: %x, takenPC: %x, taken: %d\n",
        start, end, takenPC, ftqEntryToFetch.taken);
    assert(pc.instAddr() < end && pc.instAddr() >= start);
    bool taken = pc.instAddr() == takenPC && ftqEntryToFetch.taken;
    assert(!taken); // TODO: remove this and use PCState as target
    bool run_out_of_this_entry = pc.nextInstAddr() >= end; // npc could be end + 2
    if (run_out_of_this_entry) {
        // dequeue the entry
        ftq.erase(fetchFtqID);
        DPRINTF(DecoupleBP, "running out of ftq entry %lu\n", fetchFtqID);
        fetchFtqID++;
    }
    return std::make_tuple(taken, TheISA::PCState(0), run_out_of_this_entry);
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
    // ftq.emplace(id, StreamPredictionWithID{fetchStream, id});
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

void DecoupledBranchPred::controlSquash(const FtqID inst_ftq_id, const FsqID inst_fsq_id, const TheISA::PCState control_pc,
                                        const TheISA::PCState &corr_target, bool is_conditional, bool is_indirect,
                                        bool actually_taken, const InstSeqNum seq)
{
    /* two cases:
     *  sn exists in bpHistory, which indicates that BP already knows it is a control
     *  sn does not exist in bpHistory, which indicates that BP treates is as a non-control or not-taken branch
     */
    DPRINTF(DecoupleBP,
            "Control squash: ftq_id=%lu, fsq_id=%lu, control_pc=0x%lx, corr_target=0x%lx, is_conditional=%u, is_indirect=%u, "
            "actually_taken=%u, branch seq: %lu\n",
            inst_ftq_id, inst_fsq_id, control_pc.instAddr(), corr_target.instAddr(), is_conditional, is_indirect, actually_taken, seq);

    // auto it = bpHistory.find(pred_id);
    // assert(it != bpHistory.end());

    // DPRINTF(DecoupleBP, "Found prediction %u (start: %#lx) in bpHistory\n", pred_id, it->second.streamStart);

    // streamUBTB->update(pred_id, it->second.streamStart, control_pc.instAddr(), corr_target.instAddr(), is_conditional,
    //                    is_indirect, actually_taken, it->second.history);

    // streamPred->update(pred_id, it->second.streamStart, control_pc.instAddr(), corr_target.instAddr(), is_conditional,
    //                    is_indirect, actually_taken, it->second.history);
    s0StreamPC = corr_target.instAddr();

    // bpHistory.erase(pred_id);
    // ftq.erase(pred_id);
    // only implemented 
    ftq.clear();

    DPRINTF(DecoupleBP, "dumping fsq entries...\n");
    for (auto it = fetchStreamQueue.begin(); it != fetchStreamQueue.end(); it++) {
        DPRINTF(DecoupleBP, "fsqID %lu\n", it->first);
        printFsqEntry(it->second);
    }
    auto it = fetchStreamQueue.find(inst_fsq_id);
    // DPRINTF(DecoupleBP, "1.5 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);

    // miss entry, write end info in it
    assert(it != fetchStreamQueue.end());
    if (actually_taken) {
        auto fsqEntry = it->second;
        fsqEntry.exe_ended = true;
        fsqEntry.exe_branchAddr = control_pc.instAddr();
        fsqEntry.exe_branchType = 0;
        fsqEntry.exe_streamEnd = control_pc.nextInstAddr();
        fsqEntry.exe_target = corr_target.instAddr();
        fsqEntry.branchSeq = seq;
        it->second = fsqEntry;
        DPRINTF(DecoupleBP, "a miss flow was redirected by taken branch, new fsq entry is:\n");
        printFsqEntry(fsqEntry);

        // clear younger fsq entries
        // TODO: support non-mispredict squash
        auto erase_it = fetchStreamQueue.upper_bound(inst_fsq_id);
        while (erase_it != fetchStreamQueue.end()) {
            DPRINTF(DecoupleBP, "erasing entry %lu\n", erase_it->first);
            printFsqEntry(erase_it->second);
            fetchStreamQueue.erase(erase_it++);
        }
    }
    DPRINTF(DecoupleBP, "dumping fsq entries after flushing fsq...\n");
    for (auto it = fetchStreamQueue.begin(); it != fetchStreamQueue.end(); it++) {
        DPRINTF(DecoupleBP, "fsqID %lu\n", it->first);
        printFsqEntry(it->second);
    }
    // if (fetchStreamQueue.size() >= 1) {
    //     auto debug_it = fetchStreamQueue.end();
    //     debug_it--;
    //     DPRINTF(DecoupleBP, "debug: after squash fetchStreamQueue end id %lu, entry below\n", debug_it->first);
    //     printFsqEntry(it->second);
    // }

    // DPRINTF(DecoupleBP, "2 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);

    ftqID = inst_ftq_id + 1;
    fsqID = inst_fsq_id + 1;
    ftqEnqPC = corr_target.instAddr();
    ftqEnqFsqID = inst_fsq_id + 1;

    fetchFtqID = inst_ftq_id + 1;
    fetchReadFtqEntryBufferValid = false;
    // DPRINTF(DecoupleBP, "3 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);
    
    DPRINTF(DecoupleBP, "bpHistory size: %u, ftq size: %u fsq size: %u\n", bpHistory.size(), ftq.size(), fetchStreamQueue.size());
    DPRINTF(DecoupleBP, "ftqEnqPC set to %x, s0StreamPC set to %x, ftqEnqFsqID %lu, fetchFtqID %lu, fsqID %lu, ftqID %lu\n",
        ftqEnqPC, s0StreamPC, ftqEnqFsqID, fetchFtqID, fsqID, ftqID);
    // here, in fact we make another prediction
}

void DecoupledBranchPred::nonControlSquash(const FtqID inst_ftq_id, const FsqID inst_fsq_id,
                                           const TheISA::PCState inst_pc, const InstSeqNum seq)
{
    // for (auto &control : bpHistory) {
    //     if (control.first >= squashed_sn) {
    //         // squash it and release resource
    //     } else {
    //         break;
    //     }
    // }

    DPRINTF(DecoupleBP, "non control squash: ftq_id: %lu, fsq_id: %lu, inst_pc: %x, seq: %lu\n",
            inst_ftq_id, inst_fsq_id, inst_pc.instAddr(), seq);
    s0StreamPC = inst_pc.nextInstAddr();

    // bpHistory.erase(pred_id);
    // ftq.erase(pred_id);
    // only implemented 
    ftq.clear();

    DPRINTF(DecoupleBP, "dumping fsq entries...\n");
    for (auto it = fetchStreamQueue.begin(); it != fetchStreamQueue.end(); it++) {
        DPRINTF(DecoupleBP, "fsqID %lu\n", it->first);
        printFsqEntry(it->second);
    }

    // make sure the fsq entry still exists
    auto it = fetchStreamQueue.find(inst_fsq_id);
    assert(it != fetchStreamQueue.end());

    // clear potential exe results and set to predicted result
    auto fsqEntry = it->second;
    fsqEntry.set_exe_with_pred();
    fsqEntry.branchSeq = -1;
    it->second = fsqEntry;
    // fetching from the original predicted fsq entry
    // since this is not a mispredict
    // but we should use a new ftq entry id
    ftqID = inst_ftq_id + 1;
    fsqID = inst_fsq_id;
    ftqEnqPC = inst_pc.nextInstAddr();
    ftqEnqFsqID = inst_fsq_id;
    fetchFtqID = inst_ftq_id + 1;
    fetchReadFtqEntryBufferValid = false;
    // DPRINTF(DecoupleBP, "3 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);
    
    DPRINTF(DecoupleBP, "bpHistory size: %u, ftq size: %u fsq size: %u\n", bpHistory.size(), ftq.size(), fetchStreamQueue.size());
    DPRINTF(DecoupleBP, "ftqEnqPC set to %x, s0StreamPC set to %x, ftqEnqFsqID %lu, fetchFtqID %lu, fsqID %lu, ftqID %lu\n",
        ftqEnqPC, s0StreamPC, ftqEnqFsqID, fetchFtqID, fsqID, ftqID);

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
    // do not need to dequeue when empty
    if (fetchStreamQueue.empty()) return;
    auto it = fetchStreamQueue.begin();
    while (it != fetchStreamQueue.end() && committedSeq > it->second.branchSeq) {
        // TODO: do update here

        // dequeue
        DPRINTF(DecoupleBP, "dequeueing stream id: %lu, entry below:\n", it->first);
        printFsqEntry(it->second);
        fetchStreamQueue.erase(it++);
    }

}