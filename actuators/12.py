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
# target is a dictionary containing process details
def do_binslide(vmi, cfg, target):

    ts_addr = int(target['vaddr'])
    pid = int(target['pid'])

    mm_offset = vmi.get_offset('linux_mm')
    mm_p = ts_addr + mm_offset
    mm_addr = vmi.read_addr_va(mm_p, 0)

    # slide opcode
    slide = '\x90'

    # sys_exit shellcode
    exit = '\x48\xc7\xc0\x3c\x00\x00\x00\x0f\x05'
    exit_length = len(exit)

    # get range of binary in memory
    current_vma = vmi.read_addr_va(mm_addr, 0)  # mm_struct: mmap

    start = vmi.read_addr_va(current_vma + 0, 0)  # vm_area_struct: vm_start
    end = vmi.read_addr_va(current_vma + 8, 0)  # vm_area_struct: vm_end
    mapped_size = end - start

    path_str = get_vma_file(vmi, cfg, current_vma)
    perm_str = get_perms(vmi, cfg, current_vma, 'str')
    print('\n\ngoing to ruin %s %s' % (path_str, perm_str))
    print('actual start: %s\nend: %s' % (start, end))

    start = start + 0x3680
    mapped_size = end - start
    print('target start: %s\nend: %s' % (start, end))

    # place slide
    slide_size = (mapped_size - exit_length)
    buf = slide * slide_size
    try:
        written = vmi.write_va(start, pid, buf)
    except ValueError:
        print('exception when writing slide')
        written = -1
    print('wrote %dB slide' % written)

    # place exit
    try:
        written = vmi.write_va(start + slide_size, pid, exit)
    except ValueError:
        print('exception when writing exit')
        written = -1
    print('wrote %dB exit' % written)

    return
