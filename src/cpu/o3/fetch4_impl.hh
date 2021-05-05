#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch.hh"
#include "debug/Fetch4.hh"

// IF4 do branch predict
template<class Impl>
void
FetchStage4<Impl>::fetch(bool &status_change, PipelineFetch<Impl> *upper)
{
    printf("FetchStage4.fetch() is called\n");
    ThreadID tid = upper->getFetchingThread();

    assert(!upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    // squash logic

    // The current PC.
    TheISA::PCState thisPC = upper->toFetch4->pc;
    printf("fetch4: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    // decode
    if (decoder[tid]->instReady()) {
        staticInst = decoder[tid]->decode(thisPC);
    }

    // DynInstPtr instruction =
    //     upper->buildInst(tid, staticInst, nullptr,
    //                 thisPC, nextPC, true);

    // lookupAndUpdateNextPC(instruction, nextPC);

    if (!this->stalls[tid].decode) {
        // upper->toDecode->pc[upper->toDecode->size++] = thisPC;
        DPRINTF(Fetch4, "[tid:%i] Sending if4 pc:%x to decode\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    }

    // upper->pc[tid] = nextPC;
}

template <class Impl>
bool
FetchStage4<Impl>::lookupAndUpdateNextPC(
        const DynInstPtr &inst, TheISA::PCState &nextPC)
{
    // Do branch prediction check here.
    // A bit of a misnomer...next_PC is actually the current PC until
    // this function updates it.
    bool predict_taken;
    bool cpc_compressed = nextPC.compressed();

    if (!inst->isControl()) {
        DPRINTF(Fetch, "Advancing PC from %s", nextPC);
        TheISA::advancePC(nextPC, inst->staticInst);
        DPRINTFR(Fetch, " to %s\n", nextPC);

        inst->setPredTarg(nextPC);
        inst->setPredTaken(false);
        return false;
    }

    ThreadID tid = inst->threadNumber;
    Addr branch_pc = nextPC.pc();
    predict_taken = branchPred->predict(inst->staticInst, inst->seqNum,
                                        nextPC, tid);

    [[maybe_unused]] bool real_pred_taken = cpc_compressed ?
        branch_pc + 2 != nextPC.pc() :
        branch_pc + 4 != nextPC.pc();

    if (predict_taken) {
        DPRINTF(Fetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be taken to %s\n",
                tid, inst->seqNum, inst->pcState().instAddr(), nextPC);
    } else {
        DPRINTF(Fetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be not taken\n",
                tid, inst->seqNum, inst->pcState().instAddr());
    }

    DPRINTF(Fetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
            "predicted to go to %s\n",
            tid, inst->seqNum, inst->pcState().instAddr(), nextPC);
    inst->setPredTarg(nextPC);
    inst->setPredTaken(predict_taken);

    /*++fetchStats.branches;

    if (predict_taken) {
        ++fetchStats.predictedBranches;
    }*/

    return predict_taken;
}

template<class Impl>
std::string
FetchStage4<Impl>::name() const
{
    return std::string(".Fetch4");
}
