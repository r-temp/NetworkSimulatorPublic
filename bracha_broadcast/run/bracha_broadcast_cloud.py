import os
import time
import paramiko
import threading

#### TODO configure ###

username = "ubuntu"
addresses_ssh = [
    ("3.121.86.202", 22),
    ("35.159.161.160", 22),
    ("35.159.110.172", 22),
    ("16.52.52.68", 22),
    ("16.52.59.70", 22),
    ("35.182.237.108", 22),
    ("16.176.16.92", 22),
    ("3.107.56.145", 22),
    ("54.252.234.120", 22),
]

# To generate config file
port_number = 42512 # hardcoded into main.rs
addresses = [
    ("3.121.86.202", port_number),
    ("35.159.161.160", port_number),
    ("35.159.110.172", port_number),
    ("16.52.52.68", port_number),
    ("16.52.59.70", port_number),
    ("35.182.237.108", port_number),
    ("16.176.16.92", port_number),
    ("3.107.56.145", port_number),
    ("54.252.234.120", port_number),
]

number_msg_to_broadcast = 50

#########################

files_path = [
    "./bracha_broadcast/node/target/release/bracha_broadcast",
    "./tmp_files_cloud/config.txt",
    "./bracha_broadcast/run/script_cloud_setup.sh",
    "./bracha_broadcast/run/script_launch.sh",
]

def exec_ssh_cmd(cmd, ssh_con):
    stdin, stdout, stderr = ssh_con.exec_command(cmd)
    print("stdout :", stdout.read().decode("utf-8"))
    print("stderr :", stderr.read().decode("utf-8"))



# create config file
if not os.path.exists("./tmp_files_cloud"):
        os.makedirs("./tmp_files_cloud")

with open(f"./tmp_files_cloud/config.txt", "w") as f:
    f.write(str(number_msg_to_broadcast) + "\n")
    for (addr, port) in addresses:
        f.write(f"{addr}:{port}\n")

ssh_connections = []

for (ssh_addr, ssh_port) in addresses_ssh :

    ssh_connection = paramiko.SSHClient()
    
    ssh_connection.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    ssh_connection.connect(ssh_addr, port=ssh_port, username=username, timeout=3)

    ssh_connections.append(ssh_connection)


transfer_threads = []
def transfer_files(ssh_con):

    sftp = ssh_con.open_sftp()
    for file_path in files_path:
        sftp.put(file_path, file_path.split('/')[-1])
    sftp.close()

    exec_ssh_cmd(f"chmod +x ./script_cloud_setup.sh", ssh_con)
    exec_ssh_cmd(f"chmod +x ./script_launch.sh", ssh_con)
    
for i, ssh_con in enumerate(ssh_connections):

    t = threading.Thread(
        target=transfer_files,
        args=(ssh_con,)
    )
    t.start()
    transfer_threads.append(t)


for t in transfer_threads:
    t.join()


print("Finished copying files")
print("Running setup....")
print("___________________________________")

stdout_channels = []
stderr_channels = []
for i, ssh_con in enumerate(ssh_connections):

    stdin, stdout, stderr = ssh_con.exec_command(f"./script_cloud_setup.sh {i} {port_number}")
    stdout_channels.append(stdout)
    stderr_channels.append(stderr)

for chan in stdout_channels:
    for line in chan.readlines():
        if "ping" in line or "ip" in line:
            print(line)
    print("___________________________________")

    
print("Finished running setup")
print("___________________________________")

stdout_channels = []
stderr_channels = []
for i, ssh_con in enumerate(ssh_connections):

    stdin, stdout, stderr = ssh_con.exec_command(f"./script_launch.sh {i} {port_number}")
    stdout_channels.append(stdout)
    stderr_channels.append(stderr)

print("Started all launch scripts")
print("___________________________________")

start_time = time.time()

for stdout in stdout_channels:
    for line in stdout.readlines():
        if 'time' in line or "Delivered" in line:
            print(line)
    print("___________________________________")

end_time = time.time()
print("Total time is ", end_time-start_time)

print("\n\n\nstderr for debug")
for stderr in stderr_channels:
    print(stderr.read().decode("utf-8"))
    print("___________________________________")

for ssh_con in ssh_connections:
    ssh_con.close()
