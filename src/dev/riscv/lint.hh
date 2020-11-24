//
// Created by zyy on 2020/11/24.
//

#ifndef GEM5_LINT_HH
#define GEM5_LINT_HH


#include "dev/io_device.hh"
#include "params/Lint.hh"

class Lint: public BasicPioDevice
{
  private:
    uint64_t timeStamp;

  public:
    typedef LintParams Params;
    Lint(const Params *p);

    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;
};


#endif //GEM5_LINT_HH
