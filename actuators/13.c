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


// This is a code snippet that extracts networking information out of a Linux 
// guest.  Be sure to fill in the correct offsets for your guest kernel version!


vmi_instance_t *vmi;
vmi_pause_vm(*vmi);

std::string ifname = "mynic0";
std::string mac = "123456";
std::string ip = "12.34.56.78";

int net_off = 0;  // replace with offset for "net", "list"
unsigned long net_list_head;
vmi_translate_ksym2v(*vmi, "init_net", &net_list_head);
net_list_head += net_off;
int dev_off = 0;  // replace with offset for "net", "dev_base_head"
int dev_addr_off = 0;  // replace with offset for "net_device", "dev_addr");
int ip_ptr_off = 0;  // replace with offset for "net_device", "ip_ptr"
int dev_listoff = 0;  // replace with offset for "net_device", "dev_list"
int ifa_list_off = 0;  // replace with offset for "in_device", "ifa_list"
int ifa_address_off = 0;  // replace with offset for "in_ifaddr", 
    //"ifa_address"

printf("net_list_head: %lu\n", net_list_head);
addr_t next_netptr = net_list_head;
addr_t current_netptr = 0;
addr_t current_devptr = 0;
addr_t next_devptr = 0;
addr_t devptr_list_head = 0;

current_netptr = next_netptr - net_off;

if (vmi_read_addr_va(*vmi, current_netptr + dev_off, 0, &devptr_list_head) == VMI_FAILURE) {
    printf("failed to read devptr\n");
}
next_devptr = devptr_list_head;
printf("--devptr_list_head = %lu\n", devptr_list_head);

do {
    current_devptr = next_devptr - dev_listoff;

    char *netname = vmi_read_str_va(*vmi, current_devptr + 0, 0);
    if (netname != NULL) {
        printf("name: %s\n", netname);
        ifname = std::string(netname);
        free(netname);
    }

    // http://lxr.free-electrons.com/source/net/ethernet/eth.c#L354
    // http://lxr.free-electrons.com/source/net/ethernet/eth.c#L286
    // http://lxr.free-electrons.com/source/include/linux/netdevice.h#L1786
    addr_t devaddr_ptr = 0;
    size_t bufsize = 6;
    char *mac_address = (char*) calloc(1, 18);
    char z[2];
    vmi_read_addr_va(*vmi, current_devptr + dev_addr_off, 0, &devaddr_ptr);
    printf("devaddr_ptr = %lu\n", devaddr_ptr);
    unsigned char *dev_addr = (unsigned char*) malloc(bufsize);
    size_t r = 0;
    vmi_read_va(*vmi, devaddr_ptr, 0, bufsize, (void*)dev_addr, &r);
    printf("read %lu bytes\n", r);
    if (r == 0) {
        printf("read error, indicating EOL\n");
        free(mac_address);
        free(dev_addr);
        break;
    }
    uint8_t a = 0;
    char b;
    for (int i=0; i<r; i++) {
        b = dev_addr[i];
        a = (unsigned int) b;
        sprintf(z, "%02x", a);
        strncat(mac_address, z, 2);
        if (i < r-1) {
            strncat(mac_address, ":", 1);
        }
    }
    mac = std::string(mac_address);
    free(mac_address);
    free(dev_addr);
    printf("mac: %s\n", mac.c_str());

    unsigned u[6];
    int c = sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
            u+5, u+4, u+3, u+2, u+1, u);
    uint64_t res = 0;
    for (int i=0; i<6; i++) {
        res = (res << 8) + u[i];
    }
    printf("mac int: %lu\n", res);

    addr_t ip4_ptr = 0;
    addr_t ifa_ptr = 0;
    vmi_read_addr_va(*vmi, current_devptr + ip_ptr_off, 0, &ip4_ptr);
    printf("ip4_ptr = %lu\n", ip4_ptr);

    vmi_read_addr_va(*vmi, ip4_ptr + ifa_list_off, 0, &ifa_ptr);
    printf("ifa_ptr = %lu\n", ifa_ptr);
    //just the first item on the list (there may be more...)
    uint32_t ifa_address = 0;
    vmi_read_32_va(*vmi, ifa_ptr + ifa_address_off, 0, &ifa_address);
    printf("ifa_address = %u\n", ifa_address);

    struct in_addr ip_bucket;
    ip_bucket.s_addr = ifa_address;
    printf("ifa_address %s\n", inet_ntoa(ip_bucket));
    ip = std::string(inet_ntoa(ip_bucket));

    printf("got interface info, %s %s %s\n", ifname.c_str(), 
            mac.c_str(), ip.c_str());

    if (vmi_read_addr_va(*vmi, next_devptr, 0, &next_devptr) == VMI_FAILURE) {
        printf("failed to read next_devptr\n");
        break;
    }
    printf("--next_devptr = %lu\n\n", next_devptr);

} while (net_list_head != next_devptr + net_off - dev_off);

vmi_resume_vm(*vmi);
