import os
import sys
import time

import paramiko
import threading
import socket

cleaning_cmds = []
cleaning_cmds.append('echo "cleaning..."')

### parse args ###

# number of vm to create
VM_COUNT = int(sys.argv[1])

BASE_IMG_PATH = sys.argv[2]


# folder to use to generate tmp files
TMP_FOLDER_PATH = sys.argv[3]
TMP_FOLDER_PATH = TMP_FOLDER_PATH if TMP_FOLDER_PATH.endswith('/') else TMP_FOLDER_PATH + '/'

if not os.path.exists(TMP_FOLDER_PATH):
        os.makedirs(TMP_FOLDER_PATH)

CONFIG_DELAYS_FILE_PATH = sys.argv[4]

### create VM TAPs ###
for i in range(VM_COUNT):
    os.system(f'ip tuntap add tap{i} mode tap')
    os.system(f'ip address add 192.168.14.{i}/24 dev tap{i}')
    os.system(f'ip link set dev tap{i} up')

    cleaning_cmds.append(f'ip link del tap{i}')

### create application TAPs ###

for i in range(VM_COUNT, 2*VM_COUNT):

    os.system(f'ip tuntap add tap{i} mode tap')
    os.system(f'ip address add 192.168.10.2{i}/24 dev tap{i}')
    os.system(f'ip link set dev tap{i} up')
    os.system(f'ip route del 192.168.10.2{i} table local')
    os.system(f'ip route add local 192.168.10.2{i} dev tap{i} table 13')
    os.system(f'ip rule add iif tap{i} lookup 13') # TODO check

    cleaning_cmds.append(f'ip link del tap{i}')

### create bridges ###

for i in range(VM_COUNT):

    os.system(f'ip link add br{i} type bridge')
    os.system(f'ip link set dev br{i} type bridge stp_state 0') # not sure if useful # TODO check
    os.system(f'ip link set dev br{i} type bridge forward_delay 0') # not sure if useful # TODO check
    os.system(f'ip link set dev tap{i} master br{i}')
    os.system(f'ip link set dev tap{i + VM_COUNT} master br{i}')
    os.system(f'ip link set dev br{i} up')

    cleaning_cmds.append(f'ip link set dev br{i} down')
    cleaning_cmds.append(f'ip link del br{i}') 
   

### create per VM images ###
for i in range(VM_COUNT):
    os.system(f'./qemu/build/qemu-img create -f qcow2 -F qcow2 -o backing_file={os.path.relpath(BASE_IMG_PATH, TMP_FOLDER_PATH)} {TMP_FOLDER_PATH}vm{i}.qcow2')

### launch VMs ###
for i in range(VM_COUNT):   
    os.system(f' \
        ./qemu/build/qemu-system-x86_64 \
        --enable-kvm \
        -m 8G \
        -smp 8 \
        -hda "{TMP_FOLDER_PATH}vm{i}.qcow2" \
        -net nic -net user,hostfwd=tcp::{2222+i}-:22 \
        -netdev tap,id=net1,ifname=tap{i},script=no,downscript=no \
        -device e1000,netdev=net1,mac=52:55:00:d1:55:{i:0>2} \
        -qmp tcp:localhost:{4444 + i},server,wait=off \
        -S \
        -display none \
        & \
    ')

cleaning_cmds.append("pkill -SIGTERM qemu-system")

time.sleep(0.5) # wait for qmp to start

### configure VMs tap interface ###

def set_tap_interface_in_guests():
    for i in range(VM_COUNT):   
        print(f"Waiting for VM {i}")     
        while True:
            try:
                with socket.create_connection(("127.0.0.1", (2222+i)), timeout=5) as sock: # TODO check timeout ?
                    sock.settimeout(10)
                    banner = sock.recv(1024).decode(errors="ignore")
                    if banner.startswith("SSH-"):
                        break
            except (socket.timeout, ConnectionRefusedError):
                time.sleep(1)
        
        ssh_connection = paramiko.SSHClient()
        
        ssh_connection.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        
        ssh_connection.connect(f"127.0.0.1", port=(2222+i), username='root', password="1234") # no timeout, waiting to boot, can take arbitrary time depending of the number of vms
        ssh_connection.exec_command(f"ip link set ens4 up")
        ssh_connection.exec_command(f"ip addr add 192.168.15.{i}/24 dev ens4")
        ssh_connection.exec_command(f"ip route add 192.168.15.0/24 dev ens4")
        ssh_connection.close()
    print("READY!")


t = threading.Thread(target=set_tap_interface_in_guests,)
t.start()


### start network simulator ###
with open(f'{TMP_FOLDER_PATH}config.txt', 'w') as file:
    for i in range(VM_COUNT):
        file.write(f"{4444 + i} 52:55:00:d1:55:{i:0>2} tap{VM_COUNT + i}\n")

os.system(f'./network_simulator/src/simulator {TMP_FOLDER_PATH}/config.txt {CONFIG_DELAYS_FILE_PATH} > /dev/null 2>&1 ') 


t.join()

### run ###

input("\n\nPress Enter to stop the simulator\n\n")

### clean ###
for cmd in cleaning_cmds:
    os.system(cmd)
