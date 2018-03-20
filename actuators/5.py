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


# cfg is a dictionary of OS offsets (this will need to be supplied)
# task_struct is the base address of an in-memory task_struct object
def clear_sighandlers(vmi, cfg, task_struct):
    ret = {}
    try:
        sighand = vmi.read_addr_va(task_struct + cfg['u_ts_sighand'], 0)
    except ValueError:
        print('ERROR could not read sighand')
        sighand = 0

    if sighand == 0:
        raise ValueError('sighand is zero!')

    for i in xrange(0, 64):
        new_sigaction = 'N/A'
        try:
            current_sigaction = vmi.read_addr_va(cfg['u_sighand_sigaction'] + sighand + i * cfg['u_sigaction_size'], 0)
        except ValueError:
            print(('sighand loop %s failed to read memory' % i))
            current_sigaction = -1
            break

        if current_sigaction > 0:
            current_flags = vmi.read_64_va(8 + sighand + i * cfg['u_sigaction_size'] + cfg['u_current_flags'], 0)
            current_restorer = vmi.read_addr_va(8 + sighand + i * cfg['u_sigaction_size'] + cfg['u_current_restorer'], 0)
            current_mask = vmi.read_64_va(8 + sighand + i * cfg['u_sigaction_size'] + cfg['u_current_mask'], 0)

            vmi.write_va(8 + sighand + i * cfg['u_sigaction_size'], 0, '\x00\x00\x00\x00\x00\x00\x00\x00')
            new_sigaction = vmi.read_addr_va(8 + sighand + i * cfg['u_sigaction_size'], 0)
            new_sigaction = hex(new_sigaction)
            ret[i] = {'new_sigaction': new_sigaction,
                      'result': False if current_sigaction == new_sigaction else True,
                      'flags': current_flags,
                      'restorer': current_restorer,
                      'mask': current_mask,
                      'current_sigaction': current_sigaction,}

    return ret
