/*
 * actuators (c) 2017-2018 Micah Bushouse
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 

// This is a code snippet that extracts and hashes a process's in-memory 
// executable pages.  Be sure to fill in the correct offsets for your guest 
// kernel version!


#include "openssl/sha.h"

vmi_instance_t *vmi;
vmi_pause_vm(*vmi);

// Works for Fedora 25
addr_t u_vm_file_offset = 160;
addr_t u_vm_file_path_offset = 16;
addr_t u_path_dentry_offset = 8;
addr_t u_dentry_d_name_offset = 32;
addr_t u_d_name_str_offset = 8;
addr_t u_dentry_d_parent_offset = 24;
addr_t u_dentry_inode_offset = 48;
addr_t u_inode_num_offset = 64;

addr_t start;
addr_t end;

// not shown: get pointer to a process's mm struct
if (VMI_FAILURE == vmi_read_addr_va(*vmi, mm_ptr, 0, &current_vma)) {
    printf("fullpath loop VMA read failed! bailing out\n");
    return NULL;
}

if (VMI_FAILURE == vmi_read_addr_va(*vmi, current_vma + 0, 0, &start)) {
    printf("fullpath loop VMA read failed! bailing out\n");
    return NULL;
}
if (VMI_FAILURE == vmi_read_addr_va(*vmi, current_vma + 8, 0, &end)) {
    printf("fullpath loop VMA read failed! bailing out\n");
    return NULL;
}

printf(" s: %" PRIx64 ", e: %" PRIx64 " ", start, end);
unsigned int n_pages = (end % start) / 4096;
printf(" pages: %d ", n_pages);

unsigned char hash[SHA256_DIGEST_LENGTH];
unsigned char pagebuf[4096];
SHA256_CTX sha256;
size_t rb = 0;
for (unsigned int q=0; q<n_pages; q++) {
    rb = vmi_read_va(*vmi, start + q * 4096, pid, pagebuf, 4096);
    if (rb/4096) {
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, pagebuf, rb);
        SHA256_Final(hash, &sha256);
        for (int i=0; i<SHA256_DIGEST_LENGTH; i++) {
            printf("%02x", hash[i]);
        }
        printf(" ");
    }
    else {
        printf(" 0 ");
    }
}

vmi_resume_vm(*vmi);
