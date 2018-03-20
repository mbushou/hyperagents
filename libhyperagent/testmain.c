/*
 * libhyperagent (c) 2017-2018 Micah Bushouse
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

#include <mutex>
#include <map>
#include <vector>
#include <iostream>
#include <thread>
#include <queue>
#include <chrono>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctime>
#include <fcntl.h>

#include "libhyperagent.h"
#include "syncmount.h"

// g++ -g -Wno-write-strings -Wall `pkg-config --cflags --libs libvmi` syncmount.cc testmain.c -o ./bin/testmain
// sudo ./bin/testmain

int main(int argc, const char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    std::string vm_name = argv[1];
    std::string rekall_profile = argv[2];
    std::string disk_path = argv[3];
    std::string basemount_path = argv[4];
    std::string ghost_image = argv[5];

    libhyperagent::HAMain ha(basemount_path, ghost_image);
    ha.add_agent(vm_name, rekall_profile, disk_path);

    vmi_instance_t *vmi = ha.get_vmi(vm_name);

    ha.get_vmi_lock();
    printf("vm_name: %s\n", vmi_get_name(*vmi));
    ha.rel_vmi_lock();

    std::string mp = ha.mount_disk(vm_name);

    std::string cmd = "ls -lah " + mp;
    system(cmd.c_str());

    ha.unmount_disk(vm_name);
}
