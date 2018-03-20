#!/bin/bash -x

#-------------------------
# libhyperagent (c) 2017-2018 Micah Bushouse
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#-------------------------

# usage: this script should help in getting qemu-nbd running for the first time
# tested on Fedora 26
# remember to: sudo modprobe nbd max_part=63

# $1: path to qcow2, e.g., /home/user/myimage.qcow2
# $2: vm name given by xen
# $3: partition number, X, where X is /dev/mapper/nbd0pX
# $4: nbd number, X, where X is /dev/nbdX
# $5: mount paths for guest file systems, e.g., /home/user/nbd_dirs

for i in {0..15}; do
	sudo umount $5/nbd$ip1
	sudo umount $5/nbd$ip2
	sudo umount $5/nbd$ip3
	sudo umount $5/nbd$ip4
	sudo qemu-nbd -d /dev/nbd$i
done

sudo qemu-nbd -nf qcow2 -c /dev/nbd$4 $1

sudo kpartx -av /dev/nbd$4

# suspend the guest somehow
# sudo virsh suspend $2
sudo xl pause $2

sudo mount -o check=none,ro,nodev,nodiratime,noexec,noiversion,nomand,norelatime,nostrictatime,nosuid /dev/mapper/nbd$4p$3 $5/nbd$4

ls -lah $5/nbd$4/

# do something interesting
#find $5/nbd$4/ -ipath \*/Windows/system32/vmicsvc.exe -print -exec sha256sum {} \; -quit
#find $5/nbd$4/ -ipath \*/etc/shadow -print -exec sha256sum {} \; -quit

sudo umount -l $5/nbd$4/

# resume the guest somehow
# sudo virsh resume $2
sudo xl resume $2

sudo qemu-nbd -d /dev/nbd$4

#for i in {0..15}; do
#	sudo umount $5/nbd$ip1
#	sudo umount $5/nbd$ip2
#	sudo umount $5/nbd$ip3
#	sudo umount $5/nbd$ip4
#	sudo qemu-nbd -d /dev/nbd$i
#done
