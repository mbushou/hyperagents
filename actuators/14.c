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


// This is a code snippet that extracts uname-like information from a Linux guest.
//  Be sure to fill in the correct offsets for your guest kernel version!


addr_t uts_offset = 0 // replace with offset for "uts_namespace", "name"
addr_t init_uts_ns;
vmi_translate_ksym2v(*vmi, "init_uts_ns", &init_uts_ns);

addr_t sysoff = 0 // replace with offset for "new_utsname", "sysname"
addr_t nodoff = 0 // replace with offset for "new_utsname", "nodename"
addr_t reloff = 0 // replace with offset for "new_utsname", "release"
addr_t veroff = 0 // replace with offset for "new_utsname", "version"
addr_t macoff = 0 // replace with offset for "new_utsname", "machine"
addr_t domoff = 0 // replace with offset for "new_utsname", "domainname"

char *sysn = vmi_read_str_va(*vmi, init_uts_ns + uts_offset + sysoff, 0);
char *node = vmi_read_str_va(*vmi, init_uts_ns + uts_offset + nodoff, 0);
char *rele = vmi_read_str_va(*vmi, init_uts_ns + uts_offset + reloff, 0);
char *veri = vmi_read_str_va(*vmi, init_uts_ns + uts_offset + veroff, 0);
char *mach = vmi_read_str_va(*vmi, init_uts_ns + uts_offset + macoff, 0);
char *doma = vmi_read_str_va(*vmi, init_uts_ns + uts_offset + domoff, 0);
