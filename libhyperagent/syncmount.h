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

#ifndef SYNCMOUNT_H_
#define SYNCMOUNT_H_

#include <vector>
#include <mutex>
#include <map>
#include <string>

#include <libvmi/libvmi.h>

namespace libhyperagent {

class syncmount_record;

/*
 * This class manages the disk introspection aspect of libhyperagent.
 * Because qemu-nbd generally can only support 16 concurrent mounts, this class spends some time
 * deconflicting who is mounted where and for how long.  It will spin a caller in case all the mounts are busy,
 * and also unmount mounts that haven't been used recently.  All this is very important if the number of guests exceeds
 * the max number of mounts.
 *
 * On a call to release_mount, the mount's reference count is decremented but not actually unmounted.
 * This is to achieve a speedup if a caller tries to remount it later.
 *
 */
class syncmount
{
    public:
        syncmount(std::mutex *vmil, std::string mount_path, std::string ghost_image);
        ~syncmount();

        int print_status();

        int request_mount(vmi_instance_t *vmi, std::string disk_path);
        int release_mount(vmi_instance_t *vmi, int dev);  // some callers lack a VMI handle (a relic from the GRR port)

        int do_nbd_mount(const char *nbd_device, vmi_instance_t *vmi, std::string disk_path);
        int do_nbd_unmount(const char *nbd_device, vmi_instance_t *vmi);
        int do_sync(vmi_instance_t *vmi);

        void drop_all_mounts();
        std::string get_mountpoint(int i);

    private:
        int64_t timeval_diff(const struct timeval *x, const struct timeval *y);
        std::mutex *vmi_global_lock;                    // owned by HAMain
        std::string base_mountpoint;                    // the directory where guest FS are mounted in to (as a subdir)
        std::string ghost_image;                        // the ghost image to be mounted during WQI
        std::mutex lk;                                  // used in request_mount critical section so only one caller can
                                                        // try to "steal" a slot at any given time
        std::vector<syncmount_record> syncmount_records;
};

/*
 * This struct stores the current state of one of the 16 mountable slots supported by qemu-nbd.
 */
class syncmount_record
{
    public:
        syncmount_record() :
            valid(1),
            calls(0),
            mountindex(-1),
            refcount(0)
        {
            l.unlock();
            vmi = NULL;
            last_used.tv_sec = 0;
            last_used.tv_usec = 0;
        };

        int valid;
        uint64_t calls;
        int mountindex;
        int refcount;
        struct timeval last_used;   // used to determine the last time this record was used
        vmi_instance_t *vmi;        // owned by an agent's HAConfig
        std::mutex l;
};

} // namespace

#endif