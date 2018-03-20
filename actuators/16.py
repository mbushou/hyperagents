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
def get_mm_stats(vmi, cfg, ts_addr):
    mm_offset = vmi.get_offset('linux_mm')
    mm_p = ts_addr + mm_offset
    mm = vmi.read_addr_va(mm_p, 0)

    try:
        nr_ptes = vmi.read_64_va(mm + cfg['u_nr_ptes'], 0)
        nr_pmds = vmi.read_64_va(mm + cfg['u_nr_pmds'], 0)
        map_count = vmi.read_64_va(mm + cfg['u_map_count'], 0)
        exec_vm = vmi.read_64_va(mm + cfg['u_exec_vm'], 0)
        stack_vm = vmi.read_64_va(mm + cfg['u_stack_vm'], 0)
        total_vm = vmi.read_64_va(mm + cfg['u_total_vm'], 0)
    except ValueError:
        nr_ptes = -1
        nr_pmds = -1
        map_count = -1
        exec_vm = -1
        stack_vm = -1
        total_vm = -1

    return {'nr_ptes': nr_ptes, 'nr_pmds': nr_pmds, 'map_count': map_count,
            'exec_vm': exec_vm, 'stack_vm': stack_vm, 'total_vm': total_vm}
