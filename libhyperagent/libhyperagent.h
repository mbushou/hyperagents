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


#ifndef LIBHYPERAGENT_H_
#define LIBHYPERAGENT_H_

#include <libvmi/libvmi.h>

#include "syncmount.h"

/*
 * Introduction:
 * HAConfig and HAMain assist the agent developer in managing VMI-specific objects for their hyperagent.
 * One instance of HAMain should be initalized in the hyperagent main function.
 * As logical agents (HAConfigs) are created, they should also be added to HAMain using add_agent().
 *
 * API:
 * As the hyperagent initializes, create a HAConfig instance for each guest and add it to HAConfig using add_agent.
 * Later, at the actuator sites:
 * - Use get_vmi_lock and rel_vmi_lock should be used to perform VMI.
 * - Use mount_disk and unmount_disk should be used to perform disk introspection.
 *
 * Other notes:
 * - HAConfig's constructor initializes VMI for its guest, and a VM's VMI handle belongs to its HAConfig.
 * - If performing disk introspection, ensure qemu-nbd works outside of libhyperagent first.
 *
 * Danger:
 * - This design grew organically and has not had the benefit of a refactor with an eye towards common OO patterns.
 * - There are likely better ways to do this; if you find this code too smelly, it is hoped that it can at least be
 * used as a reference.
 *
 */

namespace libhyperagent {

class HAConfig;

class HAMain
{
    public:
        HAMain(std::string mount_path, std::string ghost_image);
        ~HAMain();

        bool add_agent(const std::string& vm_name, const std::string& rekall_profile, const std::string& disk_path);

        vmi_instance_t* get_vmi(const std::string& vm_name);  // to be used at the actuator site.
        void get_vmi_lock();  // only one actuator should use VMI at a time
        void rel_vmi_lock();

        /*
         * Disk mounting generally wraps syncmount.h functions.
         * Syncmount handles flushing a Linux VM's page cache.
         */
        std::string mount_disk(const std::string& vm_name);
        void unmount_disk(const std::string& vm_name);

    private:
        std::map<std::string, HAConfig*> agents;
        /*
         * VMI global lock.
         * Each HAConfig controls their own vmi instance, however these instances must not be used in parallel because
         * LibVMI uses globals.
         */
        syncmount sm;
        std::string mount_path;
        std::string ghost_image;
        std::mutex vmi_lock;
};

/*
 * Each logical agent will have its own HAConfig.
 */
class HAConfig
{
    public:
        HAConfig(const std::string& vm_name,
                 const std::string& rekall_profile,
                 const std::string& disk_path,
                 std::mutex *vl);

        ~HAConfig();

        const std::string rekall_profile;  // Rekall profile filename
        const std::string vm_name;         // domain name
        const std::string disk_path;       // disk path
        std::string mountpoint;            // mount point on the hypervisor

        vmi_instance_t vmi;     // hyperagent-wide VMI object for a particular guest
        int vmtype;             // follows LibVMI's VMI_OS_WINDOWS/LINUX
        int mountdev;           // currently mounted nbd slot

    private:
        syncmount *sm;          // belongs to HAMain
        std::mutex *vmi_lock;   // belongs to HAMain
};

} // namespace

#endif
