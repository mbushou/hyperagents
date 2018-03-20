Hyperagents
=======

Introduction
-------
This repository consists of three artifacts related to the CODASPY'18 paper
"Hyperagents: Migrating Host Agents to the Hypervisor."

1. List of actuator references; useful for building new actuators.
1. `libhyperagent` library: to mask implementation details of qemu-nbd and VMI handling.
1. Example ClamAV wrapper: Flushes a VM's page cache using VMI, then execs clamscan.

Tested on
-------
- Stock Fedora 26 dom0
- Stock Xen hypervisor package (4.8.1)
- Fedora 25 HVM guest
- LibVMI master branch

Limitations
-------
- The syncmount technique relies on hard coded offsets that work on x86_64 guests running 4.8.6 kernels.  Replace these
offsets with the correct offsets for your guest kernel.

Paper citation
-------

```
@inproceedings{18CODASPY_Hyperagents,
 title = {{Hyperagents: Migrating Host Agents to the Hypervisor}},
 author = {Bushouse, Micah and Reeves, Douglas},
 booktitle = {Proceedings of the Eighth ACM on Conference on Data and Application Security and Privacy},
 series = {CODASPY '18},
 year = {2018},
 location = {Tempe, Arizona, USA},
}
```
