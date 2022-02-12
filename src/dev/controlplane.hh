//
// Created by zcq on 2022/2/11.
//

#ifndef GEM5_CONTROLPLANE_HH
#define GEM5_CONTROLPLANE_HH


#include "cpu/o3/deriv.hh"
#include "dev/io_device.hh"
#include "mem/cache/cache.hh"
#include "params/ControlPlane.hh"

namespace LvNATaskId {
    enum JobId {
        MaxLowPrivId = 7,
        NumId = 16
    };
}

class ControlPlane: public BasicPioDevice
{
  protected:
    std::vector<DerivO3CPU *> cpus;
    std::vector<Cache *> l2s;
    Cache * l3;

  public:
    typedef ControlPlaneParams Params;
    ControlPlane(const Params *p);

    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;
};


#endif //GEM5_CONTROLPLANE_HH
