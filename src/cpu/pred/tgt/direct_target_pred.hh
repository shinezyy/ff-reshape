#ifndef __CPU_PRED_DIR_TAR_PRED_HH__
#define __CPU_PRED_DIR_TAR_PRED_HH__

#include <deque>

#include <boost/dynamic_bitset.hpp>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/static_inst.hh"
#include "params/DirectTargetPredictor.hh"
#include "sim/sim_object.hh"

class DirectTargetPredictor : public SimObject
{
  public:
    typedef DirectTargetPredictorParams Params;

    DirectTargetPredictor(const Params &p)
    : SimObject(p)
    {}

    /**
     * Check whether the given pc has a valid entry in BTB
     */
    virtual bool valid(Addr instPC, ThreadID tid) { return false; }

    /**
     * Looks up a given PC in the BTB to get its target address.
     */
    virtual TheISA::PCState lookup(Addr instPC, ThreadID tid) = 0;

    /**
     * Updates the BTB with all information needed.
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
    virtual void update(Addr instPC, const TheISA::PCState &targetPC,
                ThreadID tid) = 0;

};

#endif //__CPU_PRED_DIR_TAR_PRED_HH__
