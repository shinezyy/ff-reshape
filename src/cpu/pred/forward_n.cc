//
// Created by yqszxx on 1/4/22.
//

#include "debug/ForwardN.hh"
#include "forward_n.hh"

ForwardN::ForwardNStats::ForwardNStats(Stats::Group *parent)
        : Stats::Group(parent, "forward_n"),
          ADD_STAT(lookups, "Number of ForwardN lookups"),
          ADD_STAT(hit, "Number of ForwardN hit"),
          ADD_STAT(hitRate, "ForwardN prediction hit rate",
                   hit / lookups),
          ADD_STAT(pcMiss, "ForwardN prediction miss count caused "
                           "by current pc"),
          ADD_STAT(histMiss, "ForwardN prediction miss count caused by history hash"),
          ADD_STAT(histTakenMiss, "ForwardN prediction miss count caused by "
                   "history taken records")
{
    hitRate.precision(4);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : FFBPredUnit(params),
          stats(this),
          histLength(params.histLength),
          histTakenLength(params.histTakenLength),
          traceStart(params.traceStart),
          traceCount(params.traceCount),
          histTaken(0)
{
    DPRINTF(ForwardN, "ForwardN, N=%u, "
                      "histLength=%u, "
                      "histTakenLength=%u\n",
                      getNumLookAhead(),
                      histLength,
                      histTakenLength
                      );

    for (int i = 0; i < histLength; i++) {
        lastCtrlsForPred.push_back(invalidPC);
        lastCtrlsForUpd.push_back(invalidPC);
    }
}

Addr ForwardN::lookup(ThreadID tid, Addr instPC, void * &bp_history) {
    auto hist = new BPState(state);
    bp_history = hist;

    ++stats.lookups;

    Addr nextK_PC = invalidPC;
    Addr lastPCsHash = hashHistory(lastCtrlsForPred);

    if (predictor.count(instPC)) {
        if (predictor[instPC].count(lastPCsHash)) {
            if (predictor[instPC][lastPCsHash].count(histTaken)) {
                ++stats.hit;
                nextK_PC = predictor[instPC][lastPCsHash][histTaken];
            } else {
                ++stats.histTakenMiss;
            }
        } else {
            ++stats.histMiss;
        }
    } else {
        ++stats.pcMiss;
    }

    return nextK_PC;
}

void ForwardN::update(ThreadID tid, const TheISA::PCState &thisPC,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst, Addr pred_nextK_PC, Addr corr_nextK_PC) {

    Addr pcNBefore = thisPC.pc();
    bool isControlNBefore = inst->isControl();
    bool isBranchingNBefore = thisPC.branching();

    Addr lastPCsHash = hashHistory(lastCtrlsForUpd);

    static uint64_t histTakenUpd = 0;

    predictor[pcNBefore][lastPCsHash][histTakenUpd] = corr_nextK_PC;

    if (inst->isControl()) {
        lastCtrlsForPred.push_back(thisPC.pc());
        lastCtrlsForPred.pop_front();

        histTaken <<= 1;
        histTaken |= thisPC.branching();
        histTaken &= ((1 << histTakenLength) - 1);
    }

    if (isControlNBefore) {
        lastCtrlsForUpd.push_back(pcNBefore);
        lastCtrlsForUpd.pop_front();

        histTakenUpd <<= 1;
        histTakenUpd |= isBranchingNBefore;
        histTakenUpd &= ((1 << histTakenLength) - 1);
    }

    if (squashed) {
        static int c = 0;
        if (c >= traceStart && c < traceStart + traceCount) {
            DPRINTF(ForwardN, "Mispred: pred=0x%016lX, act=0x%016lX, off=%d\n",
                    pred_nextK_PC,
                    corr_nextK_PC,
                    corr_nextK_PC > pred_nextK_PC ?
                        (signed int)(corr_nextK_PC - pred_nextK_PC) :
                        -(signed int)(pred_nextK_PC - corr_nextK_PC)
                    );
        }
        c++;
    }
}

Addr ForwardN::hashHistory(const std::deque<Addr> &history) {
    Addr hash = 0;
    std::for_each(
            history.begin(),
            history.end(),
            [&hash](Addr a) {
                hash ^= a;
            });
    return hash;
}

void ForwardN::squash(ThreadID tid, void *bp_history) {
    auto history_state = static_cast<BPState *>(bp_history);

    // TODO: recover predictor status

    delete history_state;
}
