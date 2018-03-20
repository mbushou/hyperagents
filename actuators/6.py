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
# current_vma is the base address of a vma (to get the file path of the exe,
# this will likely be the first vma)
def get_vma_file(vmi, cfg, current_vma):

    path = []
    path_str = ''
    #  vma -> *file -> path -> *dentry -> qstr -> *char
    file_p = vmi.read_addr_va(current_vma + cfg['u_vm_file_offset'], 0)
    if file_p == 0:
        pass
    else:
        dentry_p = vmi.read_addr_va(file_p + cfg['u_vm_file_path_offset'] + cfg['u_path_dentry_offset'], 0)
        while True:
            char_p = vmi.read_addr_va(dentry_p + cfg['u_dentry_d_name_offset'] + cfg['u_d_name_str_offset'], 0)
            vm_file = vmi.read_str_va(char_p, 0)

            if vm_file == '/':
                path_str = ''
                for i in path[::-1]:
                    path_str = '%s/%s' % (path_str, i)  # invert the path
                break
            else:
                path.append(vm_file)

            dentry_p = vmi.read_addr_va(dentry_p + cfg['u_dentry_d_parent_offset'], 0)

    return path_str
