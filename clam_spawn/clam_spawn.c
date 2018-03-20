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
#include <unistd.h>
#include <sys/wait.h>

#include "libhyperagent.h"
#include "syncmount.h"

#define DEBUG

#ifdef DEBUG
#define PRINT_DEBUG(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define PRINT_DEBUG(fmt, args...)
#endif

// g++ -g -Wno-write-strings -Wall `pkg-config --cflags --libs libvmi` syncmount.cc clam_spawn.c -o ./bin/clam_spawn
// sudo ./bin/clam_spawn clamscan -r .

int read_waitpid_status(int status);

int main (int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    libhyperagent::HAMain ha;

    std::string vm_name = "";            // place VM name here
    std::string rekall_profile = "";     // place recall profile here
    std::string disk_path = "";          // place fullpath to VM disk image here

    ha.add_agent(vm_name, rekall_profile, disk_path);

    vmi_instance_t *vmi = ha.get_vmi(vm_name);
    ha.get_vmi_lock();
    PRINT_DEBUG("vm_name: %s\n", vmi_get_name(*vmi));
    ha.rel_vmi_lock();

#ifdef DEBUG
    std::string mp = ha.mount_disk(vm_name);
    std::string cmd = "ls -lah " + mp;
    system(cmd.c_str());
#endif

    pid_t pid;
    int status;

    pid = fork();
    if (!pid) {  // gets run by child only
        chdir(mp.c_str());
        execvp(argv[1], argv + 1);
        return -1;
    }

    waitpid(pid, &status, 0);
    read_waitpid_status(status);

    ha.unmount_disk(vm_name);
}

int read_waitpid_status(int status)
{
    int ret = 0;
    if (WIFEXITED(status)) {
        PRINT_DEBUG("child exited with rc=%d\n", WEXITSTATUS(status));
        ret = WEXITSTATUS(status) + 128;
    }
    if (WIFSIGNALED(status)) {
        char *strsig = strsignal(WTERMSIG(status));
        PRINT_DEBUG("child exited via signal %s\n", strsig);
        PRINT_DEBUG("core dump? %d\n", WCOREDUMP(status));
    }
    if (WIFCONTINUED(status)) {
        PRINT_DEBUG("child process was resumed\n");
    }
    return ret;
}
