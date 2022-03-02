//
// Created by zcq on 2022/2/11.
//

#include "controlplane.hh"
#include "mem/packet_access.hh"

FgJobMeta::FgJobMeta(ControlPlane *cp):
  JobMeta(cp)
{
}
void
FgJobMeta::jobUp(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  // cp->cpus[cpu_id]->setTaskId(LvNATasks::job2TaskId(job_id));
  cp->cpus[cpu_id]->setTaskId(LvNATasks::jobId2OptionalBypassIdx(job_id));
  up(now_cycle,now_insts);
}
void
FgJobMeta::jobDown(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  down(now_cycle,now_insts);
  cp->cpStat.JobCycles[job_id] += total_cycle;
  cp->cpStat.JobInsts[job_id] += total_insts;
}

BgCpuMeta::BgCpuMeta(ControlPlane *cp):
  JobMeta(cp)
{
}
void
BgCpuMeta::bgUp(int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  cp->cpus[cpu_id]->setTaskId(cpu_id);
  up(now_cycle,now_insts);
}
void
BgCpuMeta::bgDown(int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  down(now_cycle,now_insts);
  cp->cpStat.CPUBackgroundCycles[cpu_id] += total_cycle;
  cp->cpStat.CPUBackgroundInsts[cpu_id] += total_insts;
}

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
    l3(p->l3),
    np(cpus.size()),
    cpStat(*this)
{
  for (size_t i = 0; i < LvNATasks::NumJobs; i++)
  {
    FgJobMap[i] = new FgJobMeta(this);
  }
  for (size_t i = 0; i < np; i++)
  {
    BgCpuMap[i] = new BgCpuMeta(this);
  }
  resetTTIMeta();

}

ControlPlane *
ControlPlaneParams::create() const
{
  return new ControlPlane(this);
}

void ControlPlane::resetTTIMeta()
{
  JobIpc.assign(LvNATasks::NumJobs,0.0);
  CPUBackgroundIpc.assign(np,0.0);
  for (size_t i = 0; i < LvNATasks::NumJobs; i++)
  {
    FgJobMap[i]->resetMeta();
  }
  for (size_t i = 0; i < np; i++)
  {
    BgCpuMap[i]->resetMeta();
  }
}

void
ControlPlane::startTraining()
{
  //mark auto tuning start
}

void
ControlPlane::startQoS()
{
  //reset stats
  cpStat.resetStats();
  inform("start real QoS\n");
  // this->schedule();
}
void
ControlPlane::startTTI()
{
  inform("start a TTI\n");
  resetTTIMeta();
  // schedule auto tuning functions
  // this->schedule();
  //record all cpus as bg status at beginning
  for (size_t i = 0; i < np; i++)
  {
    //bg up
    uint64_t now_cycle = cpus[i]->getNumCycles();
    uint64_t now_insts = cpus[i]->getCommittedInsts();
    BgCpuMap[i]->bgUp(i,now_cycle,now_insts);
  }
}

void
ControlPlane::setJob(int job_id, int cpu_id, bool status)
{
  uint64_t now_cycle = cpus[cpu_id]->getNumCycles();
  uint64_t now_insts = cpus[cpu_id]->getCommittedInsts();
  if (status)
  {
    //job up
    FgJobMap[job_id]->jobUp(job_id,cpu_id,now_cycle,now_insts);
    //bg down
    BgCpuMap[cpu_id]->bgDown(cpu_id,now_cycle,now_insts);
  }
  else
  {
    //job down
    FgJobMap[job_id]->jobDown(job_id,cpu_id,now_cycle,now_insts);
    //bg up
    BgCpuMap[cpu_id]->bgUp(cpu_id,now_cycle,now_insts);
  }
}

void
ControlPlane::endTTI()
{
  inform("A TTI ends\n");
  //mark a TTI end, calculate stats
  //record all cpus in bg status in the end
  for (size_t i = 0; i < np; i++)
  {
    uint64_t now_cycle = cpus[i]->getNumCycles();
    uint64_t now_insts = cpus[i]->getCommittedInsts();
    BgCpuMap[i]->bgDown(i,now_cycle,now_insts);
  }
  //report stats

  // for (size_t i = 0; i < LvNATasks::NumJobs; i++)
  // {
  //   cpStat.JobCycles[i] += JobCycles[i];
  //   cpStat.JobInsts[i] += JobInsts[i];
  // }
  // for (size_t i = 0; i < np; i++)
  // {
  //   cpStat.CPUBackgroundCycles[i] += CPUBackgroundCycles[i];
  //   cpStat.CPUBackgroundInsts[i] += CPUBackgroundInsts[i];
  // }
}

ControlPlane::ControlPlaneStats::ControlPlaneStats(ControlPlane &cp)
  : Stats::Group(&cp),
  cp(cp),
  ADD_STAT(JobCycles, "cycles of a critical job working"),
  ADD_STAT(JobInsts , "Insts of a critical job committed"),
  ADD_STAT(JobIpc   , "IPC of a critical job"),
  ADD_STAT(CPUBackgroundCycles, "cycles of a cpu working in low priority"),
  ADD_STAT(CPUBackgroundInsts , "insts of a cpu working in low priority"),
  ADD_STAT(CPUBackgroundIpc   , "IPC of a cpu working in low priority")
{
  JobCycles
    .init(LvNATasks::NumJobs)
    .flags(Stats::nozero)
    .prereq(JobCycles);
  JobInsts
    .init(LvNATasks::NumJobs)
    .flags(Stats::nozero)
    .prereq(JobInsts);
  JobIpc
    .precision(6);
  JobIpc = JobInsts / JobCycles;

  CPUBackgroundCycles
    .init(cp.np)
    .flags(Stats::nozero)
    .prereq(CPUBackgroundCycles);
  CPUBackgroundInsts
    .init(cp.np)
    .flags(Stats::nozero)
    .prereq(CPUBackgroundInsts);
  CPUBackgroundIpc
    .precision(6);
  CPUBackgroundIpc = CPUBackgroundInsts / CPUBackgroundCycles;
}
void
ControlPlane::ControlPlaneStats::preDumpStats()
{
  Stats::Group::preDumpStats();
}
