#ifndef __CPU_PRED_XSBP_HH__
#define __CPU_PRED_XSBP_HH__

#include "cpu/pred/bpred_unit.hh"
#include "params/XSBP.hh"

class XSBP : public BPredUnit
{
  public:
    typedef XSBPParams Params;
    XSBP(const Params &p) : BPredUnit(p)
    {}
};

#endif // __CPU_PRED_XSBP_HH__
