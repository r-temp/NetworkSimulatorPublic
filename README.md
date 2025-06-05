# NetworkSimulatorPublic
A network simulator to evaluate distributed system performance. It uses QEMU VMs connected by tap interfaces to a program that schedule them one at a time and forward the packets between them with configurable latency.

Don't forget to clone the submodules \
`git clone --recurse-submodules git@github.com:r-temp/NetworkSimulatorPublic.git`

## QEMU

Currently using v10.0.2 

```
cd qemu
git checkout v10.0.2
```

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
../configure --target-list=x86_64-softmmu
make -j8
```

## Base image

The system assume the existence of a qcow2 image into which it can ssh on `root` with password `1234`. (This can be easily modified in `/network_simulator/start.py`)

### Creating the image
First create an empty qcow2 image \
`./qemu/build/qemu-img create -f qcow2 base.qcow2 16G` \
Download a linux image and start the VM using `./run_base_img.sh` **uncomment the 2 lines and replace "*.iso" with the name of your image** \
Once the installation is done comment the 2 lines out again. \
You can use `./run_base_img.sh` later to launch the base image without the simulator and configure it (install dependencies) \
You can set GRUB timeout to 0 to skip the menu and boot the VM faster. \
**Make sure there is no time sync daemons running** \
Ensure the network interface is up with address `10.0.2.15` and default gateway `10.0.2.2` and set the DNS server to `10.0.2.3` \
**Do not use `ping` to test QEMU user networking connectivity, it won't work, use curl (or other) instead** \
(ping will work between VMs using the TAP interface though)

When using `ping` to test connectivity between VM using QEMU with `-smp` more than 1 (as set in `/network_simulator/start.py`) you may see `ping: Warning: time of day goes back (-514824us), taking countermeasures`, maybe because the clock is not syncronized between the vCPUs. (<https://serverfault.com/questions/1078179/ping-warning-time-of-day-goes-back-203647us-taking-countermeasures>)

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
`make -j8` \

### Start the simulator
To start the simulator run `/network_simulator/start.py` which will setup the TAP interfaces (need sudo for this), start the VMs and the simulator. \
You need to install `paramiko` \
`/network_simulator/start.py` assumes that it is run from the root and that the network interface inside the VM connected to the TAP interface is named `ens4` (this can be easily changed) \
`sudo python3 ./start.py <number of VMs to create> <path to the base image> <folder where to create all temporary files like the instanciated VMs images> <path to latency configuration file>` \
The latency configuration file is just a .txt with a matrix of one way latencies (in ms) between VMs separated by spaces, latency [i][j] is the one way latency when sending to VM i from VM j. \
Example \
`sudo python3 ./network_simulator/start.py 3 ./base.qcow2 ./tmp_files ./latency_configs/delays_3x3.txt` \
Wait for `READY!` to appear, this means that all VMs started. \
At the start the simulator run all the VMs at the same time for 10sec to speed up booting before using the scheduling algorithm (also it seems to avoid staying stuck in GRUB somehow) \
You can then ssh into them using ports `2222, 2223, ...` \
`ssh -p 2222 root@127.0.0.1` \

### Stop the simulator
`Ctrl + C` \


## Bracha broadcast

See <https://www.sciencedirect.com/science/article/pii/089054018790054X> for the algorithm. \
Each node broadcast a given number of message to the others. A node wait for its current message to be delivered before sending the next one.

### Compile Bracha broadast

Install rust
`cd bracha_broadcast/node` \ 
`cargo build --jobs 8 --release` \

### Run Bracha broadcast on AWS
Open `./bracha_broadcast/run/bracha_broadcast_cloud.py` and configure the ip addresses and username to ssh into the VM's (Login is assumed to be done using ssh keys) and the number of message to send per node.
Then run \
`python3 bracha_broadcast/run/bracha_broadcast_cloud.py` \

### Run Bracha broadcast on the simulator
Install hping and bc on the base image
Open `./bracha_broadcast/run/bracha_broadcast_sim.py` and configure the number of VMs and the number of message to send per node.
Then run \
`python3 bracha_broadcast/run/bracha_broadcast_sim.py` \


## Hotstuff

### Run hotstuff on AWS
See <https://github.com/asonnino/hotstuff/wiki/AWS-Benchmarks>


### Apply modif to hostuff to launch on the sim
`cp ./hotstuff_modif/fabfile.py ./hotstuff/benchmark/` \
`cp ./hotstuff_modif/sleep_tmux_kill.sh ./hotstuff/benchmark/` \
`cp ./hotstuff_modif/remote_sim.py ./hotstuff/benchmark/benchmark/` \

### Compile hotstuff
Install required dep (clang and rust) 
`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh` \
and build using \
`cd hotstuff/node`
`cargo build --release --features benchmark`

### Prepare base image to run hotstuff
Install tmux and rust on the base image \
`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh` \
Once compiled, copy the hotstuff folder from your machine to the base image \
`sftp -P 2222 root@127.0.0.1` \
`put -r ./hotstuff /root/hotstuff` \
Share public Ed25519 key using `ssh-copy-id -p 2222 root@localhost` 

### Run hotstuff on the simulator
Configure the benchmark parameters in `hotstuff/benchmark/fabfile.py` and the number of VMs in `hotstuff/benchmark/\benchmark/remote_sim.py` \
Set the path to your ssh key in `hotstuff/benchmark/settings.py` \
Then run \
`cd ./hotstuff/benchmark` \
`fab remote-sim` \
Note : I managed to have hotstuff work using python 3.9 and running `pip install --upgrade numpy matplotlib`

### Plot results
`fab plot` \
But for some reason you might need to go in `./hotstuff/benchmark/results/*.txt` and remove some `-` before some numbers for it to work. \

## Test timeslices accuracy 
TODO run base image, compile and upload print_time_program, compile and start scheduler, measure accuracy



