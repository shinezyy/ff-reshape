import argparse
from common import *
import matplotlib.pyplot as plt
import numpy as np
import os
import pandas as pd
import re
from sklearn.linear_model import LinearRegression

def parse_single_file(filepath, d2c, epoch_num):
    regexps = ['system\.'+x+'\s+([0-9\.]+)' for x in d2c]
    res = []
    with open(filepath, 'r') as f:
        c = f.read()
    for exp in regexps:
        finds = re.findall(exp, c)
        # convert str to float
        # and discard the first data (warmup)
        finds.pop(0)
        # patch missing data (sometimes a gem5 run was shut down halfway)
        if len(finds) != epoch_num:
            finds.extend([-1 for i in range(epoch_num-(len(finds)))])
        res.append([float(x) for x in finds[:epoch_num]])
    #  res like [[exp1_matches],[exp2_matches]]
    return res

def parse_multi_file(bm, exps, epoch_num):
    os.chdir(ff_base+'log/')
    data = []
    cases = []
    for case in os.listdir('.'):
        # the cases we collect data from
        # here we collect from different tests of the same benchmark
        name = re.match(bm+'_(\w+)', case)
        if name == None:
            continue
        data.append(parse_single_file(case+'/stats.txt', exps, epoch_num))
        cases.append(name.group(1))
    # data like [[[exp1],[exp2]], [[exp1],[exp2]]]
    # cases is the index of data (data[case])
    return data, cases

def reshape_data(data, epoch_num):
    # ========= data organization ==========
    #   data[cases][exp][epoch]
    #
    #       case3  _1_ _2_ _3_ _4_ _5_ 
    #             |   |   |   |   |   |
    #    case2   _1_ _2_ _3_ _4_ _5_  |
    #           |   |   |   |   |   | |
    # case1    _1_ _2_ _3_ _4_ _5_  | | 
    #    exp1 |   |   |   |   |   | |
    #    exp2 |   |   |   |   |   | |
    #    exp3 |   |   |   |   |   |
    #
    # ========= reshaped data ==============
    #   data[epoch][cases][exp]
    #
    #           3  _exp1_ _exp2_ _exp3_
    #             |      |      |      |
    #        2   _exp1_ _exp2_ _exp3_  |
    #           |      |      |      | |
    #     1    _exp1_ _exp2_ _exp3_  |  
    #   case1 |      |      |      | |
    #   case2 |      |      |      |
    #   case3 |      |      |      |
    # ========== To Data Frame =============
    #
    #   __________ _exp1_ _exp2_ _exp3_    
    #       case1 |      |      |      | 
    #    1  case2 |      |      |      |
    #       case3 |      |      |      |
    #   ________________________________         
    #       case1 |      |      |      | 
    #    2  case2 |      |      |      |
    #       case3 |      |      |      |
    #   ________________________________

    data = np.transpose(data, [2, 0, 1])
    data = np.reshape(data, [-1, data.shape[2]])
    indexx = [cases for i in range(epoch_num)]
    indexx = np.reshape(indexx, [-1])
    data = pd.DataFrame(data,columns=d2c,index=indexx,dtype=float)
    return data

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-b','--benchmark', type=str, required=True, help="like gcc-xal-xal-xal")
    args = parser.parse_args()
    bm = args.benchmark

    # data to collect
    d2c = []
    d2c.append('cpu0.ipc')
    d2c.append('l3.demand_accesses::.cpu0.data')
    d2c.append('l3.demand_accesses::.cpu1.data')
    d2c.append('l3.demand_accesses::.cpu2.data')
    d2c.append('l3.demand_accesses::.cpu3.data')
    d2c.append('mem_ctrls.avgRdQLen')
    d2c.append('mem_ctrls.requestorReadAvgLat::.cpu0.data')
    d2c.append('l20.demand_avg_mshr_miss_latency::.cpu0.data')
    d2c.append('l3.demand_avg_mshr_miss_latency::.cpu0.data')

    d2c.append('l3.overall_miss_latency::.cpu0.data')
    epoch_num = 3

    data, cases = parse_multi_file(bm, d2c, epoch_num)
    cnum = len(cases) # casenum
    data = reshape_data(data, epoch_num)

    ipc = data['cpu0.ipc']
    x0 = data['l3.demand_accesses::.cpu0.data']
    x1 = data['l3.demand_accesses::.cpu1.data']
    x2 = data['l3.demand_accesses::.cpu2.data']
    x3 = data['l3.demand_accesses::.cpu3.data']
    l2misslat       = data['l20.demand_avg_mshr_miss_latency::.cpu0.data']
    l3misslat       = data['l3.demand_avg_mshr_miss_latency::.cpu0.data']
    memlat          = data['mem_ctrls.requestorReadAvgLat::.cpu0.data']
    rdqlen          = data['mem_ctrls.avgRdQLen']
    lower_accs      = data['lower_acc'] = (x1+x2+x3)
    l3acc_percent   = data['cpu0_part'] = x0/(x0+x1+x2+x3)
    
    mc = data['l3.overall_miss_latency::.cpu0.data']/1000

    PRINT_DATA = True
    DRAW_PLOT = True

    if PRINT_DATA:
        for i in range(epoch_num):
            print("epoch {} ==============".format(i))
            print(data.iloc[i*cnum:i*cnum+cnum].sort_values(axis=0,ascending=True,by=['cpu0.ipc']))
    
    if DRAW_PLOT:
        # x = 1/l3acc_percent
        x = lower_accs
        y = ipc

        plt.figure(figsize=(15,10))
        for i in range(epoch_num):
            xi = x[i*cnum:i*cnum+cnum]
            yi = y[i*cnum:i*cnum+cnum]
            plt.scatter(xi,yi,s=8,label=i)
            
            # linear regression
            xi = xi.values.reshape(-1,1)
            reg = LinearRegression().fit(xi,yi)
            plt.plot(xi,reg.predict(xi),label='y=%.2ex+<%.2f>,R^2=%.2f'%(reg.coef_, reg.intercept_, reg.score(xi,yi)))

        label_x = x.name
        label_y = y.name
        x = x.values.reshape(-1,1)
        reg = LinearRegression().fit(x,y)
        plt.plot(x,reg.predict(x),label='Total y=%.2ex+<%.2f>,R^2=%.2f'%(reg.coef_, reg.intercept_, reg.score(x,y)))

        plt.title(bm)
        plt.xlabel(label_x)
        plt.ylabel(label_y)
        plt.legend(loc='lower right')
        # plt.savefig('{}_{}-{}.png'.format(bm, label_x, label_y))
        plt.show()

