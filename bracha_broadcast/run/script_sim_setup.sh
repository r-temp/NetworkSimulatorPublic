echo "hello update"

ip_addr=$(ip -f inet addr show ens4 | awk '/inet / {print $2}')

echo "ip $ip_addr"

chmod +x ./bracha_broadcast

kill $(pgrep -w "bracha")

# TODO don't hardcode ./config.txt
# awk -F: 'NR > 1 {print $1}' ./config.txt |  while read line; do
#     echo "debug" # TODO remove 
#     average_ping=$(hping3 -S -p 80 -c 3 $line 2>&1 | awk '{ if (match($0, /\/[0-9]+\.[0-9]+\//)) print substr($0, RSTART+1, RLENGTH-2) }')
#     echo -e "ping $line $average_ping"
# done


