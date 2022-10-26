//
// Created by zcq on 2022/2/11.
//

#ifndef GEM5_CONTROLPLANE_HH
#define GEM5_CONTROLPLANE_HH


#include "base/statistics.hh"
#include "cpu/o3/deriv.hh"
#include "dev/io_device.hh"
#include "dev/lvnaTasks.hh"
#include "mem/cache/cache.hh"
#include "params/ControlPlane.hh"

class ControlPlane;

class JobMeta
{
  protected:
  ControlPlane *cp;
  public:
  uint64_t start_cycle;
  uint64_t start_insts;
  uint64_t total_cycle;
  uint64_t total_insts;
  JobMeta(ControlPlane *cp)
  {
    this->cp = cp;
  }
  void resetMeta()
  {
    start_cycle = 0;
    start_insts = 0;
    total_cycle = 0;
    total_insts = 0;
  }
  protected:
  void up(uint64_t now_cycle, uint64_t now_insts)
  {
    start_cycle = now_cycle;
    start_insts = now_insts;
  }
  void down(uint64_t now_cycle, uint64_t now_insts)
  {
    total_cycle += now_cycle - start_cycle;
    total_insts += now_insts - start_insts;
  }
};

class FgJobMeta : public JobMeta
{
  public:
  FgJobMeta(ControlPlane *cp);

  void jobUp(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts);
  void jobDown(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts);
};
class BgCpuMeta : public JobMeta
{
  public:
  BgCpuMeta(ControlPlane *cp);

  void bgUp(int cpu_id,uint64_t now_cycle, uint64_t now_insts);
  void bgDown(int cpu_id,uint64_t now_cycle, uint64_t now_insts);
};

class ControlPlane: public BasicPioDevice
{
  private:
    std::map<int,FgJobMeta*> FgJobMap;
    std::map<int,BgCpuMeta*> BgCpuMap;
    void resetTTIMeta();
    //this is used to record contextID to QoS ID map
    std::map<uint32_t, uint32_t> context2QosIDMap;
    std::map<uint32_t, uint32_t> QosIDAlterMap;
    std::vector<uint64_t> l3_waymask_set;
    std::vector<uint64_t> l3_waymask_high_set;
    double l3_hot_threshold;

  public:
    std::vector<DerivO3CPU *> cpus;
    std::vector<Cache *> l2s;
    Cache * l3;
    int np;
    uint32_t l2_tb_inc, l3_tb_inc;
    uint32_t l2_tb_freq, l3_tb_freq;
    uint32_t l2_tb_size, l3_tb_size;
    double mixIpc;
    //these are used to record performance in one TTI
    std::vector<double> JobIpc;
    std::vector<double> CPUBackgroundIpc;

    typedef ControlPlaneParams Params;
    ControlPlane(const Params *p);

    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    //start QoS training
    void startTraining();
    //clean up stats, start real QoS simulation
    void startQoS();
    // adjust params after a TTI
    void tuning();
    //tell cp a TTI start
    void startTTI();
    //tell cp the TTI is end
    void endTTI();

    void setJob(int job_id, int cpu_id, bool status);

    void setContextQosId(uint32_t ctx_id, uint32_t qos_id);
    void registerRunningHighId(uint32_t qos_id, bool flag);
    void clearUsedHighIds();

  public:
    struct ControlPlaneStats : public Stats::Group
    {
      ControlPlaneStats(ControlPlane &cp);
      void preDumpStats() override;

      ControlPlane &cp;

      //these are for QoS stats, start counting after QoS Warmup,
      //these are total stat results of many TTIs
      Stats::Vector JobCycles;
      Stats::Vector JobInsts;
      Stats::Formula JobIpc;
      Stats::Vector CPUBackgroundCycles;
      Stats::Vector CPUBackgroundInsts;
      Stats::Formula CPUBackgroundIpc;

    }cpStat;

};


#endif //GEM5_CONTROLPLANE_HH
