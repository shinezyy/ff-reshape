//
// Created by zcq on 2022/2/11.
//

#include "controlplane.hh"
#include "mem/packet_access.hh"

Tick ControlPlane::read(PacketPtr pkt) {
  pkt->setLE(0);
  pkt->makeAtomicResponse();
  return pioDelay;
}

Tick ControlPlane::write(PacketPtr pkt) {
  pkt->makeAtomicResponse();
  return pioDelay;
}

ControlPlane::ControlPlane(const ControlPlaneParams *p) :
    BasicPioDevice(*p, p->pio_size),
    cpus(p->cpus),
    l2s(p->l2s),
    l3(p->l3)
{

}

ControlPlane *
ControlPlaneParams::create() const
{
  return new ControlPlane(this);
}

