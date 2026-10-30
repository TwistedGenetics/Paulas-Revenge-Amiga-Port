#!/bin/sh
# usage: tools/fuzzrun.sh seed actions   (run from build/)
seed=$1; n=$2
python3 ../tools/mkfuzz.py $seed $n ../sim/fuzz$seed.txt
rm -rf rH$seed rS$seed oH$seed oS$seed && mkdir -p rH$seed rS$seed oH$seed oS$seed
cp simroot/test.prv simroot/sn.wav rH$seed/ && cp simroot/test.prv simroot/sn.wav rS$seed/
ASAN_OPTIONS=detect_leaks=0 ../host/replay_asan ../sim/fuzz$seed.txt oH$seed rH$seed > oH$seed/log.txt 2>&1
./m68ksim PaulasRevenge ../sim/fuzz$seed.txt oS$seed rS$seed > oS$seed/log.txt 2>&1
echo "sim exit $?" >> oS$seed/log.txt
python3 ../tools/compare.py oS$seed oH$seed rS$seed rH$seed > cmp$seed.txt 2>&1
echo "compare exit $?" >> cmp$seed.txt
