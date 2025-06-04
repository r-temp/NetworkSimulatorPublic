# NetworkSimulatorPublic
A network simulator to evaluate distributed system performance. It uses QEMU VMs connected by tap interfaces to a program that schedule them one at a time and forward the packets between them with configurable latency.

Don't forget to clone the submodules \
`git clone --recurse-submodules git@github.com:r-temp/NetworkSimulatorPublic.git`

## QEMU

### Apply modif to qemu
The simulator need 2 custom QMP commands to easily be able to run VMs time slices. \
To add them to QEMU copy the following files (and replace the existing ones) from `qemu_modif` to `qemu` \
`cp ./qemu_modif/misc.json ./qemu/qapi/misc.json` \
`cp ./qemu_modif/qmp-cmds.c ./qemu/monitor/qmp-cmds.c` \
For more info on how to create new QMP commands \
<https://www.qemu.org/docs/master/devel/writing-monitor-commands.html>

### Compile QEMU
To compile QEMU follow the instructions given here \
<https://wiki.qemu.org/Hosts/Linux> \
Installing some dependencies may be needed
```
cd qemu
mkdir build
cd build
../configure --target-list=x86_64-softmmu --enable-debug
make -j8
```

## Base image

The system assume the existence of a base.qcow2 image into which it can ssh with password `1234`.

### Creating the image
First create an empty qcow2 image \
`./qemu/build/qemu-img create -f qcow2 base.qcow2 16G` \
Download a linux image and start the VM using `./run_base_img.sh` **uncomment the 2 lines and replace "*.iso" with the name of your image** \
Once the installation is done comment the 2 lines out again. \
You can use `./run_base_img.sh` later to launch the base image without the simulator and configure it (install dependencies) \
You can set GRUB timeout to 0 to skip the menu and boot the VM faster. \
Ensure the network interface is up with address `10.0.2.15` and default gateway `10.0.2.2` \
**Do not use `ping` to test QEMU user networking connectivity, it won't work, use curl (or other) instead** \
From QEMU documentation :
> If you are using the (default) SLiRP user networking, then ping (ICMP) will not work, though TCP and UDP will. Don't try to use ping to test your QEMU network configuration!

For more info on QEMU networking check <https://wiki.qemu.org/Documentation/Networking> \
\
I used an Arch image with openssh (configured to allow connection using password to root) and a small systemd service to configure the network.

`setup_user_networking.service` in `/etc/systemd/system`
```
[Unit]
Description=Setup user networking interface
After=sys-subsystem-net-devices-ens3.device
Requires=sys-subsystem-net-devices-ens3.device

[Service]
Type=oneshot
ExecStart=/bin/bash /home/setup_user_networking.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

`setup_user_networking.sh` in `/home`
```
ip link set ens3 up
ip addr add 10.0.2.15/24 dev ens3
ip route add default via 10.0.2.2 dev ens3
```

### Making the image bigger if needed (to install more dependendies)

If you realize later than the image is not big enough you should be able to resize it \
Check <https://gist.github.com/zakkak/ab08672ff9d137bbc0b2d0792a73b7d2> \
 <https://gist.github.com/ekawahyu/105b0bf96dfee5745e3a17746ff19af5> \
 <https://askubuntu.com/questions/107228/how-to-resize-virtual-machine-disk> \
**(maybe do a backup first just in case)** \
`./qemu/build/qemu-img resize base.img +20G` \
Then inside the VM \
Use fdisk to delete and recreate the partition (with the same start sector) \
Once the partition is written to the disk run `resize2fs /dev/vda1`

## Simulator
This is the program that connect to the VMs using QMP and TAP interfaces.

### Compile the simulator
`cd ./network_simulator/src` \
`make` \

### Start the simulator
To start the simulator run `start.py` which will setup the TAP interfaces (need sudo for this), start the VMs and the simulator. \
You need to install `paramiko` \
`start.py` assumes that it is run from the root and that the network interface inside the VM connected to the TAP interface is name `ens4` \
`sudo python3 ./start.py <number of VMs to create> <path to the base image> <folder where to create all temporary files like the instanciated VMs images> <path to latency configuration file>` \
The latency configuration file is just a .txt with a matrix of one way latencies (in ms) between VMs separated by spaces, each row/column is a VM. \
Example \
`sudo python3 ./network_simulator/start.py 3 ./base.qcow2 ./tmp_files ./latency_configs/delays_3x3.txt` \
Wait for `READY!` to appear, this means that all VMs started. \
You can then ssh into them using ports `2222, 2223, ...` \
`ssh -p 2222 root@127.0.0.1` \


## Bracha broadcast
TODO what does this implementation does (each node send and receive msg)

### Compile Bracha broadast
TODO
install rust
cargo build

### Run Bracha broadcast on AWS
TODO

### Run Bracha broadcast on the simulator
TODO 



## Hotstuff
TODO

### Apply modif to hostuff
TODO what to copy where

### Compile hotstuff
TODO install required dep and build

### Prepare base image to run hotstuff
TODO

### Run hotstuff on AWS
TODO

### Run hotstuff on the simulator
TODO



## Test timeslices accuracy 
TODO run base image, compile and upload print_time_program, compile and start scheduler, measure accuracy



