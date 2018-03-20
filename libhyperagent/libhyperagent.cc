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

#include <libvmi/libvmi.h>

#include "syncmount.h"
#include "libhyperagent.h"

#define DO_HA_DEBUG 1

#ifdef DO_HA_DEBUG
#define HA_DEBUG(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define HA_DEBUG(fmt, args...)
#endif

namespace libhyperagent {

HAMain::HAMain(std::string mount_path, std::string ghost_image) :
    sm(&vmi_lock, mount_path, ghost_image),
    mount_path(mount_path),
    ghost_image(ghost_image)
{
    HA_DEBUG("HAMain constructor\n");
    vmi_lock.unlock();
}


HAMain::~HAMain()
{
    HA_DEBUG("HAMain destructor\n");
    for (auto it = agents.begin(); it != agents.end(); ++it) {
        delete it->second;
    }
}


bool
HAMain::add_agent(const std::string& vm_name,
                  const std::string& rekall_profile,
                  const std::string& disk_path)
{
    HA_DEBUG("HAMain add_agent %s\n", vm_name.c_str());
    HAConfig *ha = new HAConfig(vm_name, rekall_profile, disk_path, &vmi_lock);
    agents[vm_name] = ha;
    return true;
}


void
HAMain::get_vmi_lock()
{
    vmi_lock.lock();
}


void
HAMain::rel_vmi_lock()
{
    vmi_lock.unlock();
}


vmi_instance_t*
HAMain::get_vmi(const std::string& vm_name)
{
    return &agents[vm_name]->vmi;
}


std::string
HAMain::mount_disk(const std::string& vm_name)
{
    vmi_instance_t *vmi = &agents[vm_name]->vmi;

    int m = sm.request_mount(vmi, agents[vm_name]->disk_path);
    agents[vm_name]->mountdev = m;
    std::string mountpoint = sm.get_mountpoint(m);
    return mountpoint;
}


void
HAMain::unmount_disk(const std::string& vm_name)
{
    sm.release_mount(&agents[vm_name]->vmi,
                      agents[vm_name]->mountdev);
}

// -------------------------------------------------------------------

HAConfig::HAConfig(const std::string& vm_name,
                   const std::string& rekall_profile,
                   const std::string& disk_path,
                   std::mutex *vl) :
    rekall_profile(rekall_profile),  // If needed by actuator, comment out if not.
    vm_name(vm_name),
    disk_path(disk_path),
    mountpoint(),
    vmi(),
    vmtype(-1),
    sm(),
    vmi_lock(vl)
{
    HA_DEBUG("libhyperagent: %s constructor\n", vm_name.c_str());
    vmi_lock->lock();
    int flags = VMI_INIT_DOMAINNAME | VMI_INIT_EVENTS;
    if (vmi_init_complete(&vmi, (void *) vm_name.c_str(), flags, NULL,
                          VMI_CONFIG_GLOBAL_FILE_ENTRY, NULL, NULL ) == VMI_FAILURE) {
            printf("libhyperagent: Failed to init LibVMI library for %s.\n", vm_name.c_str());
            exit(1);
    }
    vmtype = vmi_get_ostype(vmi);
    vmi_lock->unlock();
}


HAConfig::~HAConfig()
{
    HA_DEBUG("libhyperagent: %s destructor\n", vm_name.c_str());
    vmi_lock->lock();
    vmi_destroy(vmi);
    vmi_lock->unlock();
}

} // namespace
