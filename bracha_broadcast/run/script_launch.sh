echo "hello launch"

start=$(date +%s.%N)
./bracha_broadcast $1 ./config.txt
end=$(date +%s.%N)

elapsed=$(echo "$end - $start" | bc)
echo "Finished executing program"
echo "Execution time: ${elapsed} seconds"




