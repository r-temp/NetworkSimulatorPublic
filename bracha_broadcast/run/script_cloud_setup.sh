echo "hello setup"

sudo apt-get update
sudo apt-get install hping3

ip_addr=$(hostname -I | awk '{print $1}')

chmod +x ./bracha_broadcast

kill $(pgrep -w "bracha")

echo "ip $ip_addr"

# TODO don't hardcode ./config.txt
awk -F: 'NR > 1 {print $1}' ./config.txt |  while read line; do
    average_ping=$(sudo hping3 -S -p 80 -c 5 $line 2>&1 | awk '{ if (match($0, /\/[0-9]+\.[0-9]+\//)) print substr($0, RSTART+1, RLENGTH-2) }')
    echo -e "ping $line $average_ping"
done


