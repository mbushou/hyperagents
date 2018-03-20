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

import collections
import pyvmi


vapa = collections.namedtuple('vapa', ['ix', 'va', 'pa'])


# vmi = pyvmi object
# va_start = starting VA address
# mem_size = linear range of bytes to convert
# pid = determines the address space used
def bulk_va2pa_translate(vmi, va_start, mem_size, pid):
    """ Takes a range of process virtual address space and translates it all into the corresponding 
    physical addresses.  VA space for which no PA exists will yield a PA of -1.
    """
    ret = []

    loops = mem_size / PAGE_SIZE
    assert (loops == int(loops))

    d = va_start / PAGE_SIZE  # 250 / 100 = 2.
    assert (d == int(d))  # start stack must always be on a PAGE_SIZE boundary.
    r = va_start % PAGE_SIZE  # 250 % 100 = 50.
    sw = (d + 1) * PAGE_SIZE  # 2 + 1 = 3, 3 * 100 = 300.  sw is on a boundary and ready for writing.

    stride = PAGE_SIZE  # Bounding size == 1 page.
    ix = sw  # Current offset from end of stack.
    me = 100  # Max error count before abort.
    e = 0  # Current error count.

    for i in xrange(0, loops):
        va = ix - stride
        try:
            pa = vmi.translate_uv2p(va, pid)
        except ValueError:
            e += 1
            pa = -1
        ret.append(vapa(ix=i, va=int(va), pa=int(pa)))
        ix -= stride
    return ret


def handle_stack(vmi, cfg, bulk_mapping, vma_info, target_info, vm_struct):

    mm_p = target_info['task_address'] + meta['mm_offset']

    mm = vmi.read_addr_va(mm_p, 0)  # Access mm_struct from task_struct.
    try:
        vma = vmi.read_addr_va(mm, 0)  # Read the first 8 bytes of mm (this points to the list of vmas).
    except ValueError as e:
        gk_verbose('UNKNOWN READ ERROR: %s' % e, buf)
        return

    ss_p = mm + meta['start_stack_offset']  # The start_stack pointer.
    ss = vmi.read_addr_va(ss_p, 0)  # The head of start_stack (a VMA?)

    start = vmi.read_addr_va(current_vma + vma_start_offset, 0)  # Process memory space start address.
    end = vmi.read_addr_va(current_vma + vma_end_offset, 0)  # Process memory space stop address.

    mem_size = end - start  # The total size of the contiguous block.

    bulk_mapping = bulk_va2pa_translate(vmi, ss, mem_size, pid)

    for page in bulk_mapping:
        if page.pa == -1:
            continue

        write_stride = PAGE_SIZE
        if stack_overwrite_ctr + PAGE_SIZE > max_overwrite:  # When only a portion remains.
            write_stride = max_overwrite - stack_overwrite_ctr

        wr = vmi.write_pa(page.pa, stack_overwrite_byte * write_stride)

