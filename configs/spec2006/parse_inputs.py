from __future__ import print_function

import re
import os
import json

def get_input_dict():
    text = ''
    file_dir = os.path.dirname(os.path.realpath(__file__))
    with open(os.path.join(file_dir, 'input_list.txt')) as f:
        text = f.read()
        text = text.replace('\n\n', '\n')

    bname = re.compile('(\d{3}\.\w+) \((\d) inputs?\)')

    # current key
    k = None
    cmds = dict()
    name_map = dict()
    state = None
    num_inputs = 0
    skip_next = False

    for line in text.split('\n'):
        if skip_next or not line:
            skip_next = False
            continue
        if num_inputs == 0:
            m = bname.match(line)
            assert(m)
            # print m.group(1), m.group(2)
            k = m.group(1)
            num_inputs = int(m.group(2))
            cmds[k] = []
            n, name = k.split('.')
            name_map[name] = k
            skip_next = True
        else:
            # print line
            cmd, out, err = line.split('>')
            cmds[k].append(cmd.strip())
            num_inputs -= 1
    return name_map, cmds


if __name__ == '__main__':
    name_map, cmd_map = get_input_dict()
    # print(cmd_map)
    js = {}
    for name in name_map:
        id_ = name_map[name]
        cmds = cmd_map[id_]
        print(name, id_, cmds)
        if len(cmds) > 1:
            for i, cmd in enumerate(cmds):
                js[name + '_' + str(i)] = {}
                js[name + '_' + str(i)]['id'] = id_
                js[name + '_' + str(i)]['cmd'] = cmd.split(' ')
                print(name, js[name + '_' + str(i)])
        else:
            js[name] = {}
            js[name]['id'] = id_
            js[name]['cmd'] = cmds[0].split(' ')
            print(name, js[name])

    with open('spec2006_cmds.json', 'w') as fp:
        json.dump(js, fp, indent=4, sort_keys=True)

