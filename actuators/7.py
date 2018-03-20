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
# task_struct is that of the target process
# sig is the signal number
def set_sigpending(vmi, cfg, task_struct, sig):
    # If these aren't zero, there are other signals afoot.
    # print vmi.read_addr_va(task_struct + task_struct_sigpending_offset, 0)
    # print vmi.read_addr_va(task_struct + task_struct_sigpending_offset + 8, 0)

    original_sigpending = None
    signal_va = task_struct + cfg['u_task_struct_sigpending_offset'] + cfg['u_sigpending_signal_offset']
    try:
        original_sigpending = vmi.read_addr_va(signal_va, 0)
        vmi.write_64_va(signal_va, 0, int(sig))
        new_sigpending = vmi.read_addr_va(signal_va, 0)

        list_head = vmi.read_addr_va(task_struct + cfg['u_task_struct_sigpending_offset'], 0)  # First pending entry.
        next_list_entry = list_head

        while True:
            try:
                current_sigpending = vmi.read_addr_va(next_list_entry + cfg['u_sigpending_signal_offset'], 0)
                vmi.write_64_va(next_list_entry + cfg['u_sigpending_signal_offset'], 0, int(sig))
                new_sigpending = vmi.read_addr_va(next_list_entry + cfg['u_sigpending_signal_offset'], 0)
            except ValueError:
                print('error setting sigpending')
                break
            if list_head == next_list_entry:
                break
    except Exception as e:
        print(('set_sigpending ERROR: %s' % e))
        current_sigpending = -1
        new_sigpending = -1

    return original_sigpending, new_sigpending
