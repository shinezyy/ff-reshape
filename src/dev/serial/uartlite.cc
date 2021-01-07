//
// Created by zyy on 2020/11/24.
//

#include "uartlite.hh"

#include "mem/packet_access.hh"

Tick UartLite::read(PacketPtr pkt) {
  assert(pkt->getAddr() >= pioAddr && pkt->getAddr() < pioAddr + pioSize);
  auto offset = pkt->getAddr() - pioAddr;
  assert(pkt->getSize() == 1);

  switch (offset) {
    case UARTLITE_STAT_REG:
      pkt->setRaw((uint8_t) 0);
      break;
    default:
      warn("Read to other uartlite addr %i is not implemented\n", offset);
  }
  pkt->makeAtomicResponse();
  return pioDelay;
}

Tick UartLite::write(PacketPtr pkt) {
  assert(pkt->getAddr() >= pioAddr && pkt->getAddr() < pioAddr + pioSize);
  auto offset = pkt->getAddr() - pioAddr;
  assert(pkt->getSize() == 1);

  switch (offset) {
    case UARTLITE_TX_FIFO:
      putc(pkt->getRaw<uint8_t>(), stdout);
      break;
    default:
      warn("Write to other uartlite addr %i is not implemented\n", offset);
  }

  pkt->makeAtomicResponse();
  return pioDelay;
}

UartLite::UartLite(UartLiteParams *params) :
    BasicPioDevice(params, params->pio_size)
{

}

UartLite *
UartLiteParams::create()
{
  return new UartLite(this);
}