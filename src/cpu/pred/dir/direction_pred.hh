#ifndef __CPU_PRED_DIR_PRED_HH__
#define __CPU_PRED_DIR_PRED_HH__

#include <deque>

#include <boost/dynamic_bitset.hpp>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/loop_info.hh"
#include "cpu/pred/ras.hh"
#include "cpu/static_inst.hh"
#include "params/DirectionPredictor.hh"
#include "sim/probe/pmu.hh"
#include "sim/sim_object.hh"

class DirectionPredictor : public SimObject
{
  public:
    typedef DirectionPredictorParams Params;

    DirectionPredictor(const Params &p)
    : SimObject(p),
      instShiftAmt(p.instShiftAmt)
    {
      printf("DirectionPredictor is constructed\n");
    }

    /**
     * Tells the predictor that an unconditional jump has been encountered.
     */
    virtual void uncond(ThreadID tid, Addr pc, void * &bp_history) = 0;

    /**
     * Looks up a given PC in the BP to see if it is taken or not taken.
     * @param inst_PC The PC to look up.
     * @param bp_history Pointer that will be set to an object that
     * has the branch predictor state associated with the lookup.
     * @return Whether the branch is taken or not taken.
     */
    virtual bool lookup(ThreadID tid, Addr instPC, void * &bp_history) = 0;

    /**
     * If a branch is not taken, because the BTB address is invalid or missing,
     * this function sets the appropriate counter in the global and local
     * predictors to not taken.
     * @param inst_PC The PC to look up the local predictor.
     * @param bp_history Pointer that will be set to an object that
     * has the branch predictor state associated with the lookup.
     */
    virtual void btbUpdate(ThreadID tid, Addr instPC, void * &bp_history) = 0;

    /**
     * @param bp_history Pointer to the history object.  The predictor
     * will need to update any state and delete the object.
     */
    virtual void squash(ThreadID tid, void *bp_history) = 0;

    /**
     * Updates the BP with taken/not taken information.
     * @param inst_PC The branch's PC that will be updated.
     * @param taken Whether the branch was taken or not taken.
     * @param bp_history Pointer to the branch predictor state that is
     * associated with the branch lookup that is being updated.
     * @param squashed Set to true when this function is called during a
     * squash operation.
     * @param inst Static instruction information
     * @param corrTarget The resolved target of the branch (only needed
     * for squashed branches)
     * @todo Make this update flexible enough to handle a global predictor.
     */
    virtual void update(ThreadID tid, Addr instPC, bool taken,
                   void *bp_history, bool squashed,
                   const StaticInstPtr &inst, Addr corrTarget) = 0;

    const unsigned instShiftAmt;

    virtual unsigned getGHR(ThreadID tid, void* bp_history) const { return 0; }

    void dump() {
      printf("Not Implemented\n");
    }

    /************** For Oracle BP ***************/
    virtual bool isOracle() {
        return false;
    }

    virtual Addr getOracleAddr() {
        return 0;
    }
    virtual bool getLastDirection() {
        return false;
    }

    /** For ZPerceptron */
    virtual boost::dynamic_bitset<> getCurrentGHR(ThreadID tid) const {
        panic("Not implemented\n");
    };
};

#endif //__CPU_PRED_DIR_PRED_HH__
