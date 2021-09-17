from typing import List
from enum import Enum, auto, unique
import itertools
from heapq import heappop, heappush


@unique
class ActOp(Enum):
    wakeup = auto()
    stop = auto()


class EventQueue():
    eq = []                         # list of entries arranged in a heap
    entry_finder = {}               # mapping of events to entries
    REMOVED = '<removed-event>'     # placeholder for a removed event
    counter = itertools.count()     # unique sequence count

    def add_event(self, event, cycle):
        'Add a new event or update the cycle of an existing event'
        if event in self.entry_finder:
            self.remove_event(event)
        count = next(self.counter)
        entry = [cycle, count, event]
        self.entry_finder[event] = entry
        heappush(self.eq, entry)

    def remove_event(self, event):
        'Mark an existing event as REMOVED. Raise KeyError if not found.'
        entry = self.entry_finder.pop(event)
        entry[-1] = self.REMOVED

    def pop_event(self):
        'Remove and return the lowest cycle event. Raise KeyError if empty.'
        while self.eq:
            _, _, event = heappop(self.eq)
            if event is not self.REMOVED:
                del self.entry_finder[event]
                return event
        raise KeyError('pop from an empty Event queue')

    def peek_cycle(self) -> int:
        'Return the next work cycle of queue head'
        return self.eq[0][0]


class SmallEvent:
    def __init__(self, work_cycle, target_job, target_action) -> None:
        self.work_cycle = work_cycle
        self.target_job = target_job
        self.target_action = target_action

    def schedule_out(self, now_cycle):
        self.target_job.act(self.target_action, now_cycle)


class SmallActionMeta:
    def __init__(self, target_job, opcode: ActOp, wait_cycle: int) -> None:
        self.opcode = opcode
        self.target_job = target_job
        self.wait_cycle = wait_cycle

    def create_event(self, now_cycle) -> SmallEvent:
        return SmallEvent(self.wait_cycle + now_cycle, self.target_job, self.opcode)


class SmallJob():
    wakeup_list = []
    stop_list = []

    def stop_action(self, now_cycle):
        print("Job[%d] %s stop at %dw"% (self.job_id, self.name, now_cycle))
        self.test_sys.cpu[self.job_id].suspendAllContexts()
        for a in self.stop_list:
            eve = a.create_event(now_cycle)
            self.event_queue.add_event(eve, eve.work_cycle)

    def wakeup_action(self, now_cycle):
        print("Job[%d] %s wake up at %dw"% (self.job_id, self.name, now_cycle))
        self.test_sys.cpu[self.job_id].activateAllContexts()
        for a in self.wakeup_list:
            eve = a.create_event(now_cycle)
            self.event_queue.add_event(eve, eve.work_cycle)

    def __init__(self, job_id: int, name: str, event_queue: EventQueue, test_sys) -> None:
        self.job_id = job_id
        self.name = name
        self.event_queue = event_queue
        self.test_sys = test_sys
        self.act_map = {
            ActOp.wakeup: self.wakeup_action,
            ActOp.stop: self.stop_action,
        }

    def act(self, opcode, now_cycle) -> None:
        act_fun = self.act_map[opcode]
        act_fun(now_cycle)

    def set_wakeup_action(self, act_list: List[SmallActionMeta]):
        'usually used to record the next stop event of itself '\
        'and the next wake events of other jobs'
        self.wakeup_list = act_list

    def set_stop_action(self, act_list: List[SmallActionMeta]):
        #temporarily unused
        self.stop_list = act_list
