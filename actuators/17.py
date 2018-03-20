#-------------------------
# actuators (c) 2017-2018 Micah Bushouse
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#-------------------------

import pyvmi

# vmi = pyvmi object
# cfg is a dictionary of OS offsets (this will need to be supplied)
# ts_addr is the base address of an in-memory task_struct object
def get_args_and_envs(vmi, cfg, ts_addr, full=False):
    mm_offset = vmi.get_offset('linux_mm')
    mm_p = ts_addr + mm_offset
    mm = vmi.read_addr_va(mm_p, 0)

    pid_offset = vmi.get_offset('linux_pid')
    pid = vmi.read_32_va(ts_addr + pid_offset, 0)

    p = {'shell': '', 'user': ''}

    p['start_brk'] = vmi.read_addr_va(mm + cfg['u_start_brk'], 0)
    p['brk'] = vmi.read_addr_va(mm + cfg['u_brk'], 0)
    p['start_stack'] = vmi.read_addr_va(mm + cfg['u_start_stack'], 0)  # mm_struct: start_stack
    p['arg_start'] = vmi.read_addr_va(mm + cfg['u_arg_start'], 0)
    p['arg_end'] = vmi.read_addr_va(mm + cfg['u_arg_end'], 0)
    p['env_start'] = vmi.read_addr_va(mm + cfg['u_env_start'], 0)
    p['env_end'] = vmi.read_addr_va(mm + cfg['u_env_end'], 0)

    arglen = p['arg_end'] - p['arg_start']
    envlen = p['env_end'] - p['env_start']

    args = None
    env = None

    try:
        karg = vmi.translate_uv2p(p['arg_start'], pid)
        kenv = vmi.translate_uv2p(p['env_start'], pid)
        # args = vmi.read_va(karg, arglen, 0)
        args = vmi.read_pa(karg, arglen)
        env = vmi.read_pa(kenv, envlen)
        # env = vmi.read_va(p['env_start'], envlen, p['pid'])
        # args = vmi.read_va(p['arg_start'], arglen, p['pid'])
    except ValueError as e:
        print('env and arg capture error: %s' % e)
        karg = None
        kenv = None

    p['arg_block'] = args.replace('\x00', ' ').strip()
    if full:
        p['env_block'] = env
    p['arglen'] = arglen
    p['envlen'] = envlen

    p['env_list'] = {}
    try:
        for item in env.split('\x00'):
            try:
                # SSH_CLIENT, HOSTNAME, SHELL, PATH
                # PWD, _, LANG, USER
                key, val = item.split('=', 1)
                if full:
                    p['env_list'][key] = val
                if key == 'USER':
                    p[key.lower()] = val
                elif key == 'SHELL':
                    p[key.lower()] = val.split('/')[-1]
            except ValueError as e:
                if item == '\x00' or item == '':
                    continue
                else:
                    print('env item processing loop: %s for %s' % (e, item))
                    pass
    except Exception as e:
        print('env processing outer loop: %s for %s' % (e, env))
        print('%s: %sB from %s/%s: %s' % (p['pid'], arglen, p['arg_start'], karg, args))
        print('%s: %sB from %s/%s: %s' % (p['pid'], envlen, p['env_start'], kenv, env))
        pass

    return p
