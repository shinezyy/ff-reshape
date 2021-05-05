# Copyright (c) 2010 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2007 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Kevin Lim

from m5.SimObject import SimObject
from m5.defines import buildEnv
from m5.params import *
from m5.objects.FuncUnit import *

def getCommonOpList():
    return [OpDesc(opClass='No_OpClass'), OpDesc(opClass='IntAlu'),

        OpDesc(opClass='MemRead'), OpDesc(opClass='MemWrite'),
        OpDesc(opClass='FloatMemRead'), OpDesc(opClass='FloatMemWrite'),

        OpDesc(opClass='Forwarder'),]

class GroupBase(FUDesc):
    count = 1

class MDGroupBase(GroupBase):
    opList = getCommonOpList()
    # int MD
    opList += [ OpDesc(opClass='IntMult', opLat=3),
            OpDesc(opClass='IntDiv', opLat=20, pipelined=False) ]
    # FP MD
    opList += [ OpDesc(opClass='FloatMult', opLat=4),
            OpDesc(opClass='FloatMultAcc', opLat=5),
            OpDesc(opClass='FloatMisc', opLat=3),
            OpDesc(opClass='FloatDiv', opLat=12, pipelined=False),
            OpDesc(opClass='FloatSqrt', opLat=24, pipelined=False) ]

class FAddGroupBase(GroupBase):
    opList = getCommonOpList()
    # FP ALU
    opList += [ OpDesc(opClass='FloatAdd', opLat=2),
        OpDesc(opClass='FloatCmp', opLat=2)]

    # FP convert
    opList += [ OpDesc(opClass='FloatCvt', opLat=2)]

class Group0(MDGroupBase):
    pass

class Group1(FAddGroupBase):
    pass

class Group2(MDGroupBase):
    pass

class Group3(FAddGroupBase):
    pass

class Group4(MDGroupBase):
    pass

class Group5(FAddGroupBase):
    pass

class Group6(MDGroupBase):
    pass

class Group7(FAddGroupBase):
    pass

# not used
class GroupIPR(FAddGroupBase):
    opList = getCommonOpList()
    # IPR (not available in RV?)
    opList += [ OpDesc(opClass='IprAccess', opLat = 3, pipelined = False) ]
