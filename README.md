# NetworkSimulatorPublic
TODO short description

TODO don't forget to clone repo with submodules



## Base image

### Content
TODO Arch install... + vim, sudo, openssh + passwd("1234") + config script to setup network (write it here) and allow ssh + systemd task (write config here)
skip grub

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

### Run base image to configure it
TODO
`run_base_img.sh`


### Making the image bigger if needed (to install more dependendies)
TODO qemu-img, fdisk, resize2fs

## QEMU

### Apply modif to qemu
TODO what to copy where
https://www.qemu.org/docs/master/devel/writing-monitor-commands.html

### Compile qemu
TODO cf qemu instruction
https://wiki.qemu.org/Hosts/Linux
Some dependencies may be needed, check the above link
```
cd qemu
mkdir build
cd build
../configure --target-list=x86_64-softmmu --enable-debug
make -j8
```

## Simulator
TODO

### Compile the simulator
TODO

### Start the simulator
TODO start + ssh into the VM



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



