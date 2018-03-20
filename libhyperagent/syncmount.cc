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
#include <fcntl.h>

#include <inttypes.h>
#include <sys/time.h>

#include "syncmount.h"

#include <libvmi/libvmi.h>
#include <libvmi/events.h>

#define DEVMAX 16

#define DO_SYNCMOUNT_DEBUG 1

#ifdef DO_SYNCMOUNT_DEBUG
#define SYNCMOUNT_DEBUG(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define SYNCMOUNT_DEBUG(fmt, args...)
#endif

namespace libhyperagent {

int interrupted = 0;
int do_cleanup = 0;
int quiet_qwork = 0;

uint64_t safety = 0;
uint8_t breakpoint_byte = 0xCC;
uint8_t null_byte = 0x00;

uint64_t sys_sync_front = 0;                // the memory address of sys_sync
uint8_t sys_sync_front_byte = 0x00;         // the original byte before we placed 0xCC
uint64_t sys_sync_front_orig = 0x00000000;  // the original contents before we placed 0xCC

uint64_t sys_sync_back = 0;
uint8_t sys_sync_back_byte = 0x00;
uint64_t sys_sync_back_orig = 0x00000000;

uint64_t qworko = 0;
uint8_t qworko_byte = 0x00;
uint64_t qworko_orig = 0x00000000;

uint64_t dwork = 0;
uint8_t dwork_byte = 0x00;
uint64_t dwork_orig = 0x00000000;

uint64_t dwork_back = 0;
uint8_t dwork_back_byte = 0x00;
uint64_t dwork_back_orig = 0x00000000;

uint64_t step_rewrite_loc = 0;

uint64_t target_work_fn = 0;
//uint64_t host_work_struct = 0;

addr_t kaslr_offset = 0;
addr_t our_stack = 0;
addr_t our_frame = 0;

reg_t saved_rdi;
reg_t saved_rsi;
reg_t saved_rdx;


//
// ------------------------------------------------------------------
// code related to VMI events

static inline uint64_t RDTSC()
{
    unsigned int hi, lo;
    __asm__ volatile("rdtsc": "=a" (lo), "=d" (hi));
    return ((uint64_t) hi << 32) | lo;
}

int print_regs(vmi_instance_t *vmi, addr_t kaslr)
{
    reg_t rip, rsp, rdi, rax;
    vmi_get_vcpureg(*vmi, &rip, RIP, 0);
    vmi_get_vcpureg(*vmi, &rsp, RSP, 0);
    vmi_get_vcpureg(*vmi, &rdi, RDI, 0);
    vmi_get_vcpureg(*vmi, &rax, RAX, 0);
    SYNCMOUNT_DEBUG("SM: RIP: %" PRIx64 ", %" PRIx64 "\n", rip, rip-kaslr);
    SYNCMOUNT_DEBUG("SM: RSP: %" PRIx64 ", %" PRIx64 "\n", rsp, rsp-kaslr);
    SYNCMOUNT_DEBUG("SM: RDI: %" PRIx64 ", %" PRIx64 "\n", rdi, rdi-kaslr);
    SYNCMOUNT_DEBUG("SM: RAX: %" PRIx64 ", %" PRIx64 "\n", rax, rax-kaslr);
    return 0;
}

/*
 * This is the main LibVMI event callback.  It is pairs with the step callback further down this file.
 */
event_response_t int_cb_front(vmi_instance_t vmi, vmi_event_t *event)
{
    reg_t rip, rdx, rax, rsp, rbp;

    uint64_t new_contents = 0;
    uint64_t old_contents = 0;

    uint64_t byte_location = 0;
    uint8_t byte_replacement = 0;

    event_response_t ret = VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;

#ifdef DO_SYNCMOUNT_DEBUG
    uint64_t rdtsc = RDTSC();  // this is only used for debugging/profiling
#endif

    safety++;
    if (safety > 50) {
        SYNCMOUNT_DEBUG("SM: Safety halt\n");
        interrupted = 1;
    }

    vmi_get_vcpureg(vmi, &rip, RIP, event->vcpu_id);
    vmi_get_vcpureg(vmi, &rdx, RDX, event->vcpu_id);
    vmi_get_vcpureg(vmi, &rax, RAX, event->vcpu_id);
    vmi_get_vcpureg(vmi, &rsp, RSP, event->vcpu_id);
    vmi_get_vcpureg(vmi, &rbp, RBP, event->vcpu_id);

    SYNCMOUNT_DEBUG("SM: %lu %lu rip: %" PRIx64 ", rsp: %" PRIx64 ", rbp: %" PRIx64 "\n", rdtsc, safety, rip, rsp, rbp);

    // step 0, our first encounter with the target_work_function
    // if a guest vCPU arrives here, it is likely because we attached a block device to the guest kernel
    if (rip == qworko) {
        byte_location = qworko;
        byte_replacement = qworko_byte;
        if (!quiet_qwork) {
            SYNCMOUNT_DEBUG("SM: %lu qworko rip: %" PRIx64 ", queue_work_on: %" PRIx64 "\n", safety, rip, qworko);
            // check work struct name, arg 2 in RSI
            SYNCMOUNT_DEBUG("SM: int_cb queue_work_on checking rdx: %" PRIx64 "\n", rdx);
            uint64_t work_fn = 0;
            vmi_read_64_va(vmi, rdx + 24, 0, &work_fn);  // work_func_t of work_struct
            SYNCMOUNT_DEBUG("SM: int_cb queue_work_on work_struct fn points to: %" PRIx64 " (?: %" PRIx64 ")\n", work_fn, target_work_fn);
            if (work_fn == target_work_fn) {
                SYNCMOUNT_DEBUG("SM:---\nSM: int_cb FOUND target_work_fn...\nSM:---\n");
                quiet_qwork = 1;
            } else {
                SYNCMOUNT_DEBUG("SM: int_cb Ignoring...\n");
            }
        }

    // step 1, jump from the beginning of dwork to the beginning of sys_sync
    // this is the point where we reset the vCPU on sys_sync
    } else if (rip == dwork) {
        byte_location = dwork;
        byte_replacement = dwork_byte;
        SYNCMOUNT_DEBUG("SM: %lu dwork rip: %" PRIx64 ", deferred_probe_work_func: %" PRIx64 "\n", safety, rip, dwork);
        our_stack = rsp;
        our_frame = rbp;
        SYNCMOUNT_DEBUG("SM: int_cb our stack: %" PRIx64 "\n", our_stack);
        SYNCMOUNT_DEBUG("SM: int_cb our frame: %" PRIx64 "\n", our_frame);
        SYNCMOUNT_DEBUG("SM: int_cb JUMP!\n");
        event->x86_regs->rip = sys_sync_front;  // manually jump the vCPU.
        saved_rdi = event->x86_regs->rdi;
        saved_rsi = event->x86_regs->rsi;
        saved_rdx = event->x86_regs->rdx;
        ret |= VMI_EVENT_RESPONSE_SET_REGISTERS;
        ret &= ~VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
        SYNCMOUNT_DEBUG("SM:---\nSM: int_cb moving to phase 1\nSM:---\n");

    // step 2, confirmation that the vCPU made it to sys_sync
    } else if (rip == sys_sync_front) {
        byte_location = sys_sync_front;
        byte_replacement = sys_sync_front_byte;
        SYNCMOUNT_DEBUG("SM: %lu sys_sync rip: %" PRIx64 ", sys_sync: %" PRIx64 "\n", safety, rip, sys_sync_front);
        SYNCMOUNT_DEBUG("SM: int_cb sys_sync has been called\n");
        SYNCMOUNT_DEBUG("SM: int_cb moving to phase 2\n");
        ret &= ~VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;

    //step 3, the vCPU made it to the tail end of sys_sync
    // it's time to put the vCPU back to dwork.
    } else if (rip == sys_sync_back) {
        byte_location = sys_sync_back;
        byte_replacement = sys_sync_back_byte;
        SYNCMOUNT_DEBUG("SM: %lu rip: %" PRIx64 ", at the end of sys_sync: %" PRIx64 "\n", safety, rip, sys_sync_back);
        SYNCMOUNT_DEBUG("SM: int_cb sys_sync RAX=%lu\n", rax);
        SYNCMOUNT_DEBUG("SM: int_cb JUMP BACK!\n");
        event->x86_regs->rip = dwork;  // skadoosh again.
        event->x86_regs->rdi = saved_rdi;
        event->x86_regs->rsi = saved_rsi;
        event->x86_regs->rdx = saved_rdx;
        ret |= VMI_EVENT_RESPONSE_SET_REGISTERS;
        ret &= ~VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
        SYNCMOUNT_DEBUG("SM:---\nSM: int_cb moving to phase 3\nSM:---\n");

    //step 4: the vCPU made it back in dwork, we're done!
    } else if (rip == dwork_back) {
        byte_location = dwork_back;
        byte_replacement = dwork_back_byte;
        SYNCMOUNT_DEBUG("SM:---\nSM: int_cb almost finished with dwork, all done!!\nSM:---\n");
        do_cleanup = 1;
        print_regs(&vmi, kaslr_offset);

    } else {
        SYNCMOUNT_DEBUG("SM: Where on earth are we? %" PRIx64 "\n", rip);
        SYNCMOUNT_DEBUG("SM: Pre-KASLR RIP = %" PRIx64 "\n", rip - kaslr_offset);
        SYNCMOUNT_DEBUG("SM: Is this our stack? %" PRIx64 "\n", our_stack);
        SYNCMOUNT_DEBUG("SM: Is this our frame? %" PRIx64 "\n", our_frame);
    }

    event->interrupt_event.reinject = 0;

    vmi_read_64_va(vmi, rip, 0, &old_contents);
    vmi_write_8_va(vmi, byte_location, 0, &byte_replacement);  // Remove 0xCC.
    vmi_read_64_va(vmi, rip, 0, &new_contents);
    step_rewrite_loc = rip;
    SYNCMOUNT_DEBUG("SM: int_cb old: %" PRIx64 ", new: %" PRIx64 "\n", old_contents, new_contents);

    return ret;
}

event_response_t step_cb_front(vmi_instance_t vmi, vmi_event_t *event)
{
    reg_t rip;
    uint64_t new_contents = 0;
    uint64_t old_contents = 0;

    vmi_get_vcpureg(vmi, &rip, RIP, event->vcpu_id);
    SYNCMOUNT_DEBUG("SM: step rip: %" PRIx64 ", step_rewrite: %" PRIx64 "\n", rip, step_rewrite_loc);

    if (do_cleanup == 1) {

        SYNCMOUNT_DEBUG("SM: step finshed do_cleanup, ending events...\n");
        interrupted = 1;
        SYNCMOUNT_DEBUG("SM: pausing VM\n");
        if (vmi_pause_vm(vmi) != VMI_SUCCESS) {
            SYNCMOUNT_DEBUG("SM: Failed to pause VM\n");
        }

    } else {

        vmi_read_64_va(vmi, step_rewrite_loc, 0, &old_contents);
        vmi_write_8_va(vmi, step_rewrite_loc, 0, &breakpoint_byte);  // Reinstall 0xCC.
        vmi_read_64_va(vmi, step_rewrite_loc, 0, &new_contents);

        SYNCMOUNT_DEBUG("SM: step old: %" PRIx64 ", new: %" PRIx64 "\n", old_contents, new_contents);
    }

    return VMI_EVENT_RESPONSE_TOGGLE_SINGLESTEP;
}

//
// ------------------------------------------------------------------
// the syncmount class

int syncmount::do_sync(vmi_instance_t *vmi)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    uint64_t old_contents = 0;
    uint64_t new_contents = 0;
    int attached = 0;
    char sysbuf[128];

    int r = 0;
    (void) r;
    status_t status;

    vmi_event_t int3_cb;
    memset(&int3_cb, 0, sizeof(vmi_event_t));
    vmi_event_t step_cb;
    memset(&step_cb, 0, sizeof(vmi_event_t));

    uint64_t vmid = vmi_get_vmid(*vmi);
    SYNCMOUNT_DEBUG("SM: vmid: %lu\n", vmid);

    SYNCMOUNT_DEBUG("SM: pausing VM\n");
    if (vmi_pause_vm(*vmi) != VMI_SUCCESS) {
        SYNCMOUNT_DEBUG("SM: Failed to pause VM\n");
    }

    status = vmi_translate_ksym2v(*vmi, "queue_work_on", &qworko);
    SYNCMOUNT_DEBUG("SM: queue_work_on location: %" PRIx64 ", masked address: %" PRIx64 "\n", qworko, qworko & ~0x7);
    vmi_read_64_va(*vmi, qworko & ~0x7, 0, &qworko_orig);
    vmi_read_8_va(*vmi, qworko, 0, &qworko_byte);
    vmi_write_8_va(*vmi, qworko, 0, &breakpoint_byte);
    vmi_read_64_va(*vmi, qworko & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: queue_work_on contents: %" PRIx64 " -> %" PRIx64 "\n", qworko_orig, new_contents);

    // Fedora 25 specific
    // use objdump on your particular guest kernel image to determine offsets
    int dwork_front_offset = 0;
    int dwork_back_offset = 170;
    int sys_sync_front_offset = 0;
    int sys_sync_back_offset = 156;
    //

    status = vmi_translate_ksym2v(*vmi, "deferred_probe_work_func", &target_work_fn);
    target_work_fn += dwork_front_offset;
    SYNCMOUNT_DEBUG("SM: target_work_fn/deferred_probe_work_func location: %" PRIx64 "\n", target_work_fn);
    dwork = target_work_fn;
    vmi_read_64_va(*vmi, dwork & ~0x7, 0, &dwork_orig);
    vmi_read_8_va(*vmi, dwork, 0, &dwork_byte);
    vmi_write_8_va(*vmi, dwork, 0, &breakpoint_byte);
    vmi_read_64_va(*vmi, dwork & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: deferred_probe_work_func contents: %" PRIx64 " -> %" PRIx64 "\n", dwork_orig, new_contents);

    status = vmi_translate_ksym2v(*vmi, "deferred_probe_work_func", &dwork_back);
    dwork_back += dwork_back_offset;
    SYNCMOUNT_DEBUG("SM: deferred_probe_work_func_back location: %" PRIx64 ", masked address: %" PRIx64 "\n", dwork_back, dwork_back & ~0x7);
    vmi_read_64_va(*vmi, dwork_back & ~0x7, 0, &dwork_back_orig);
    vmi_read_8_va(*vmi, dwork_back, 0, &dwork_back_byte);
    vmi_write_8_va(*vmi, dwork_back, 0, &breakpoint_byte);
    vmi_read_64_va(*vmi, dwork_back & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: dwork_back contents: %" PRIx64 " -> %" PRIx64 "\n", dwork_back_orig, new_contents);

    status = vmi_translate_ksym2v(*vmi, "sys_sync", &sys_sync_front);
    sys_sync_front += sys_sync_front_offset;
    SYNCMOUNT_DEBUG("SM: sys_sync location: %" PRIx64 "\n", sys_sync_front);
    vmi_read_64_va(*vmi, sys_sync_front & ~0x7, 0, &sys_sync_front_orig);
    vmi_read_8_va(*vmi, sys_sync_front, 0, &sys_sync_front_byte);
    vmi_write_8_va(*vmi, sys_sync_front, 0, &breakpoint_byte);
    vmi_read_64_va(*vmi, sys_sync_front & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: sys_sync contents: %" PRIx64 " -> %" PRIx64 "\n", sys_sync_front_orig, new_contents);

    status = vmi_translate_ksym2v(*vmi, "sys_sync", &sys_sync_back);
    sys_sync_back += sys_sync_back_offset;
    SYNCMOUNT_DEBUG("SM: sys_sync_back location: %" PRIx64 "\n", sys_sync_back);
    vmi_read_64_va(*vmi, sys_sync_back & ~0x7, 0, &sys_sync_back_orig);
    vmi_read_8_va(*vmi, sys_sync_back, 0, &sys_sync_back_byte);
    vmi_write_8_va(*vmi, sys_sync_back, 0, &breakpoint_byte);
    vmi_read_64_va(*vmi, sys_sync_back & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: sys_sync_back contents: %" PRIx64 " -> %" PRIx64 "\n", sys_sync_back_orig, new_contents);

    // Fedora 25 specific
    // cat ~/kernels/maps/System.map-4.8.6-300.fc25.x86_64 |grep deferred_probe_work_func
    // ffffffff8152d920 t deferred_probe_work_func
    addr_t normal_dwork = 0xffffffff8152d920;
    //

    kaslr_offset = target_work_fn - normal_dwork;
    SYNCMOUNT_DEBUG("SM: KASLR offset 0x%" PRIx64 "\n", kaslr_offset);

    if (target_work_fn == 0) {
        SYNCMOUNT_DEBUG("SM: VMI layer problem, exiting...");
        return 0;
    }

    SYNCMOUNT_DEBUG("SM: starting events...\n");

    SETUP_INTERRUPT_EVENT(&int3_cb, 0, int_cb_front);
    SETUP_SINGLESTEP_EVENT(&step_cb, 1, step_cb_front, 0);

    vmi_register_event(*vmi, &int3_cb);
    vmi_register_event(*vmi, &step_cb);

    SYNCMOUNT_DEBUG("SM: resuming VM\n");
    if (vmi_resume_vm(*vmi) != VMI_SUCCESS) {
        SYNCMOUNT_DEBUG("SM: Failed to resume VM\n");
    }

    SYNCMOUNT_DEBUG("SM: entering eventloop...\n");
    while (!interrupted) {
        status = vmi_events_listen(*vmi, 100);
        SYNCMOUNT_DEBUG("SM: EVENT TICK\n");
        if (status != VMI_SUCCESS) {
            SYNCMOUNT_DEBUG("SM: error waiting for events, quitting...\n");
            SYNCMOUNT_DEBUG("SM: pausing VM\n");
            interrupted = -1;
            if (vmi_pause_vm(*vmi) != VMI_SUCCESS) {
                SYNCMOUNT_DEBUG("SM: Failed to pause VM\n");
            }
        }
        if (attached == 0) {
            // attach device to VM
            memset(sysbuf, 0, 128);
            sprintf(sysbuf, "xl block-attach %lu file:%s xvdz r", vmid, this->ghost_image.c_str());
            SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
            r = system(sysbuf);
            SYNCMOUNT_DEBUG("SM: block-attach retval: %d\n", r);

            memset(sysbuf, 0, 128);
            sprintf(sysbuf, "xl block-list %lu", vmid);
            SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
            r = system(sysbuf);
            SYNCMOUNT_DEBUG("SM: block-list retval: %d\n", r);
            attached = 1;
        }
    }
    vmi_clear_event(*vmi, &int3_cb, NULL);
    vmi_clear_event(*vmi, &step_cb, NULL);

    //
    SYNCMOUNT_DEBUG("SM: replacing memory...\n");

    vmi_read_64_va(*vmi, dwork & ~0x7, 0, &old_contents);
    vmi_write_8_va(*vmi, dwork, 0, &dwork_byte);
    vmi_read_64_va(*vmi, dwork & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: dwork contents: %" PRIx64 " (old) -> %" PRIx64 " (orig) / %" PRIx64 " (now) byte: %u\n",
            old_contents, dwork_orig, new_contents, dwork_byte);

    vmi_read_64_va(*vmi, dwork_back & ~0x7, 0, &old_contents);
    vmi_write_8_va(*vmi, dwork_back, 0, &dwork_back_byte);
    vmi_read_64_va(*vmi, dwork_back & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: dwork_back contents: %" PRIx64 " (old) -> %" PRIx64 " (orig) / %" PRIx64 " (now) byte: %u\n",
            old_contents, dwork_back_orig, new_contents, dwork_back_byte);

    vmi_read_64_va(*vmi, sys_sync_front & ~0x7, 0, &old_contents);
    vmi_write_8_va(*vmi, sys_sync_front, 0, &sys_sync_front_byte);
    vmi_read_64_va(*vmi, sys_sync_front & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: sys_sync contents: %" PRIx64 " (old) -> %" PRIx64 " (orig) / %" PRIx64 " (now) byte: %u\n",
            old_contents, sys_sync_front_orig, new_contents, sys_sync_front_byte);

    vmi_read_64_va(*vmi, sys_sync_back & ~0x7, 0, &old_contents);
    vmi_write_8_va(*vmi, sys_sync_back, 0, &sys_sync_back_byte);
    vmi_read_64_va(*vmi, sys_sync_back & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: sys_sync_back contents: %" PRIx64 " (old) -> %" PRIx64 " (orig) / %" PRIx64 " (now) byte: %u\n",
            old_contents, sys_sync_back_orig, new_contents, sys_sync_back_byte);

    vmi_read_64_va(*vmi, qworko & ~0x7, 0, &old_contents);
    vmi_write_8_va(*vmi, qworko, 0, &qworko_byte);
    vmi_read_64_va(*vmi, qworko & ~0x7, 0, &new_contents);
    SYNCMOUNT_DEBUG("SM: queue_work_on contents: %" PRIx64 " (old) -> %" PRIx64 " (orig) / %" PRIx64 " (now) byte: %u\n",
            old_contents, qworko_orig, new_contents, qworko_byte);

    print_regs(vmi, kaslr_offset);
    SYNCMOUNT_DEBUG("SM: resuming VM\n");
    if (vmi_resume_vm(*vmi) != VMI_SUCCESS) {
        SYNCMOUNT_DEBUG("SM: Failed to resume VM\n");
    }
    print_regs(vmi, kaslr_offset);

    memset(sysbuf, 0, 128);
    sprintf(sysbuf, "xl block-detach %lu xvdz", vmid);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    r = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: block-detach retval: %d\n", r);

    memset(sysbuf, 0, 128);
    sprintf(sysbuf, "xl block-list %lu", vmid);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    r = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: block-list retval: %d\n", r);

    print_regs(vmi, kaslr_offset);

    return 0;
}


syncmount::syncmount(std::mutex *vmil, std::string mount_path, std::string ghost_image)
    : vmi_global_lock(vmil),
    base_mountpoint(mount_path),
    ghost_image(ghost_image),
    syncmount_records(DEVMAX)
{
    SYNCMOUNT_DEBUG("SM: syncmount constructor\n");

    //drop_all_mounts();

    for (int i=0; i<DEVMAX; i++) {
        SYNCMOUNT_DEBUG("%d ", i);

        syncmount_records[i].valid = 1;
        syncmount_records[i].mountindex = -1;
        syncmount_records[i].refcount = 0;
        syncmount_records[i].l.unlock();
        syncmount_records[i].vmi = NULL;
        gettimeofday(&syncmount_records[i].last_used, NULL);
    }
    SYNCMOUNT_DEBUG("SM: \n");
}


syncmount::~syncmount()
{
    SYNCMOUNT_DEBUG("SM: syncmount destructor\n");
    drop_all_mounts();
}


void syncmount::drop_all_mounts()
{
    SYNCMOUNT_DEBUG("SM: syncmount drop_all_mounts\n");
    char sysbuf[192];
    int r = 0;
    (void) r;

    for (int i=0; i<DEVMAX; i++) {
        memset(sysbuf, 0, 192);
        sprintf(sysbuf, "umount %snbd%d", base_mountpoint.c_str(), i);
        SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
        r = system(sysbuf);
        SYNCMOUNT_DEBUG("SM: umount retval: %d\n", r);

        memset(sysbuf, 0, 192);
        sprintf(sysbuf, "qemu-nbd -d /dev/nbd%d", i);
        SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
        r = system(sysbuf);
        SYNCMOUNT_DEBUG("SM: qemu-nbd retval: %d\n", r);
    }
}


std::string syncmount::get_mountpoint(int i)
{
    return base_mountpoint + "nbd" + std::to_string(i) + "/";
}


int syncmount::print_status()
{
    SYNCMOUNT_DEBUG("SM: request_mount status\n");
    struct timeval c;
    gettimeofday(&c, NULL);
    char *name;
    vmi_global_lock->lock();
    for (std::vector<syncmount_record>::size_type i = 0; i != syncmount_records.size(); i++) {
        int64_t d = timeval_diff(&syncmount_records[i].last_used, &c);
        (void)d;
        int locked = 0;
        (void)locked;
        if (syncmount_records[i].l.try_lock()) {
            locked = 1;
            syncmount_records[i].l.unlock();
        }
        if (syncmount_records[i].vmi != NULL) {
            name = vmi_get_name(*syncmount_records[i].vmi);
        } else {
            name = strdup("UNK");
        }
        SYNCMOUNT_DEBUG("SM: %2lu v=%d l=%d vm=%6s calls=%lu refcount=%d last_used %" PRId64 " vmi=%" PRIx64 "\n",
                        i, syncmount_records[i].valid, locked,
                        name, syncmount_records[i].calls,
                        syncmount_records[i].refcount, d, (addr_t) syncmount_records[i].vmi);
        free(name);
    }
    vmi_global_lock->unlock();
    return 0;
}


int syncmount::request_mount(vmi_instance_t *vmi, std::string disk_path)
{
    for (std::vector<syncmount_record>::size_type i = 0; i != syncmount_records.size(); i++) {
        if (syncmount_records[i].vmi == vmi) {  // this mount already exists from an earlier request_mount, return it
            syncmount_records[i].refcount++;
            syncmount_records[i].calls++;
            gettimeofday(&syncmount_records[i].last_used, NULL);
            SYNCMOUNT_DEBUG("SM: request_mount returning remount at index %lu, refcount=%d\n",
                            i, syncmount_records[i].refcount);
            return (int) i;
        }
    }

    // only new mounts make it to this point
    for (std::vector<syncmount_record>::size_type i = 0; i != syncmount_records.size(); i++) {
        if (syncmount_records[i].l.try_lock()) {  // try to grab pocession of a slot
            SYNCMOUNT_DEBUG("SM: request_mount got lock at index %lu\n", i);
            syncmount_records[i].refcount = 1;  // do not release if we see 0 here
            syncmount_records[i].mountindex = (int) i;
            syncmount_records[i].valid = 1;
            syncmount_records[i].vmi = vmi;
            syncmount_records[i].calls++;
            std::string nbd_dev = "nbd" + std::to_string(i);
            do_nbd_mount(nbd_dev.c_str(), vmi, disk_path);
            gettimeofday(&syncmount_records[i].last_used, NULL);
            SYNCMOUNT_DEBUG("SM: request_mount mounted at index %lu\n", i);
            return (int) i;
        }
    }

    // only new mounts which could not be mounted due to 0 available slots make it here
    // let's find the least recently used mount and unmount it
    SYNCMOUNT_DEBUG("SM: request_mount running LRU to nominate a close\n");
    struct timeval c = (struct timeval){0};
    int64_t max = 0;
    int64_t maxix = 0;

    char *name = NULL;
    if (vmi != NULL) {
        name = vmi_get_name(*vmi);
    } else {
        name = strdup("UNK");
    }

    int tries = 0;
    int keep_spinning = 1;
    while (keep_spinning == 1) {
        lk.lock();  // only one thread can try to steal a slot at a time
        gettimeofday(&c, NULL);
        for (std::vector<syncmount_record>::size_type i = 0; i != syncmount_records.size(); i++) {
            int64_t d = timeval_diff(&syncmount_records[i].last_used, &c);
            SYNCMOUNT_DEBUG("request_mount %s index %lu: %" PRId64 "\n", name, i, d);
            if (syncmount_records[i].refcount == 0) {
                SYNCMOUNT_DEBUG("request_mount %s index %lu has refcount==0\n", name, i);
                if (max < d) {
                    max = d;
                    maxix = i;
                    SYNCMOUNT_DEBUG("request_mount new max %" PRId64 "\n", max);
                }
            }
        }
        if (max < 10000) {  // only proceed if max is greater than 10sec
            SYNCMOUNT_DEBUG("SM: request_mount %s, no mounts available, sleeping\n", name);
            lk.unlock();  // this thread was unlucky, give up lock
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            SYNCMOUNT_DEBUG("SM: request_mount %s woke up, re-entering spinloop, try=%d\n", name, tries);
            keep_spinning = 1;
            tries++;
        } else {
            SYNCMOUNT_DEBUG("SM: request_mount %s decided to take ix=%lu\n", name, maxix);
            keep_spinning = 0;
        }
    }

    SYNCMOUNT_DEBUG("SM: request_mount %s closing %lu\n", name, maxix);
    //syncmount_records[maxix].l.unlock();
    std::string nbd_dev = "nbd" + std::to_string(maxix);
    do_nbd_unmount(nbd_dev.c_str(), vmi);

    syncmount_records[maxix].refcount = 1;  // do not release if we see 0 here
    syncmount_records[maxix].mountindex = (int) maxix;
    syncmount_records[maxix].valid = 1;
    syncmount_records[maxix].calls = 0;
    syncmount_records[maxix].vmi = vmi;
    //std::string nbd_dev = "nbd" + std::to_string(i);
    do_nbd_mount(nbd_dev.c_str(), vmi, disk_path);
    gettimeofday(&syncmount_records[maxix].last_used, NULL);
    SYNCMOUNT_DEBUG("SM: request_mount %s refcount remount at index %lu, refcount=%d, tries=%d\n",
            name, maxix, syncmount_records[maxix].refcount, tries);
    free(name);
    lk.unlock();  // successfully stolen, leaving request_mount

    return (int) maxix;
}


int syncmount::release_mount(vmi_instance_t* vmi, int dev)
{
    SYNCMOUNT_DEBUG("SM: release_mount index %d\n", dev);

    syncmount_records[dev].refcount--; // released, can be unmounted in the future

    vmi_global_lock->lock();
    vmi_resume_vm(*syncmount_records[dev].vmi);
    vmi_global_lock->unlock();
    SYNCMOUNT_DEBUG("SM: VM resumed\n");

    return 0;
}


int syncmount::do_nbd_mount(const char *nbd_device, vmi_instance_t *vmi, std::string disk_path)
{
    int a,b,c;
    (void) a;
    (void) b;
    (void) c;
    char sysbuf[192];
    char *name = NULL;

    SYNCMOUNT_DEBUG("SM: syncmount acquiring vmi lock\n");
    vmi_global_lock->lock();
    name = vmi_get_name(*vmi);
    SYNCMOUNT_DEBUG("SM: syncmount got name %s\n", name);
    SYNCMOUNT_DEBUG("SM: syncmount calling do_sync\n");
    int ret = do_sync(vmi);
    (void) ret;
    SYNCMOUNT_DEBUG("SM: syncmount returned from do_sync, retval: %d\n", ret);
    vmi_global_lock->unlock();

    // start reading
    // associate nbd
    memset(sysbuf, 0, 192);
    sprintf(sysbuf, "qemu-nbd -nf qcow2 -c /dev/%s %s", nbd_device, disk_path.c_str());
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    a = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: qemu-nbd retval: %d\n", a);

    if (a==256){
        SYNCMOUNT_DEBUG("SM: exiting on bad retval\n");
        exit(-1);
    }

    // build partition devices in mapper
    memset(sysbuf, 0, 192);
    sprintf(sysbuf, "kpartx -av /dev/%s", nbd_device);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    b = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: kpartx retval: %d\n", b);

    // pause VM
    SYNCMOUNT_DEBUG("SM: syncmount acquiring vmi lock\n");
    vmi_global_lock->lock();
    if (vmi_pause_vm(*vmi) != VMI_SUCCESS) {
        SYNCMOUNT_DEBUG("SM: Failed to pause VM\n");
    }
    SYNCMOUNT_DEBUG("SM: VM paused\n");
    std::string check = "";
    if (VMI_OS_LINUX == vmi_get_ostype(*vmi)) {
        check = "check=none,";
    } else {
        check = "";
    }
    vmi_global_lock->unlock();

    // mount disk
    memset(sysbuf, 0, 192);
    sprintf(sysbuf, "umount %s%s", base_mountpoint.c_str(), nbd_device);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    c = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: precautionary umount retval: %d\n", c);

    memset(sysbuf, 0, 192);
    sprintf(sysbuf, "mount -o %sro,nodev,nodiratime,noexec,noiversion,nomand,norelatime,nostrictatime,nosuid /dev/mapper/%sp1 %s%s",
            check.c_str(), nbd_device, base_mountpoint.c_str(), nbd_device);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    c = system(sysbuf);

    SYNCMOUNT_DEBUG("SM: mount retval: %d\n", c);

    free(name);

    return 0;
}


int syncmount::do_nbd_unmount(const char *nbd_device, vmi_instance_t *vmi)
{
    int e,f;
    (void) e;
    (void) f;
    char sysbuf[192];

    // unmount disk
    memset(sysbuf, 0, 192);
    // adding -l, lazy unmount
    sprintf(sysbuf, "umount -l %s%s", base_mountpoint.c_str(), nbd_device);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    e = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: umount retval: %d\n", e);

    // resume VM
    vmi_global_lock->lock();
    vmi_resume_vm(*vmi);
    vmi_global_lock->unlock();
    SYNCMOUNT_DEBUG("SM: VM resumed\n");

    // recover nbd device
    memset(sysbuf, 0, 192);
    sprintf(sysbuf, "qemu-nbd -d /dev/%s", nbd_device);
    SYNCMOUNT_DEBUG("SM: %s\n", sysbuf);
    f = system(sysbuf);
    SYNCMOUNT_DEBUG("SM: qemu-nbd retval: %d\n", f);

    return 0;
}


// Credit reference: http://www.mpp.mpg.de/~huber/util/timevaldiff.c
int64_t
syncmount::timeval_diff(const struct timeval *x, const struct timeval *y)
{
    int64_t msec;
    msec = (y->tv_sec - x->tv_sec) * 1000;
    msec += (y->tv_usec - x->tv_usec) / 1000;
    return msec;
}

} // namespace
