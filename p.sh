#!/bin/bash

# 1. Handle command line arguments and execute ma.sh
if [ -z "$1" ]; then
    ./ma.sh tstring
    ./ma.sh sieve
    ./ma.sh e
    ./ma.sh tm
    ./ma.sh ttt
    ./ma.sh pihex
    ./ma.sh mm
else
    ./ma.sh tstring nopeep
    ./ma.sh sieve nopeep
    ./ma.sh e nopeep
    ./ma.sh tm nopeep
    ./ma.sh ttt nopeep
    ./ma.sh pihex nopeep
    ./ma.sh mm nopeep
fi

# 2. Run the ntvcm commands
ntvcm -p build/TSTRING
ntvcm -p build/SIEVE
ntvcm -p build/E
ntvcm -p build/TM
ntvcm -p build/TTT 10
ntvcm -p build/PIHEX
ntvcm -p build/MM

# 3. List the files in build/ sorted by date
# Note: Linux uses 'ls -t' to sort files by modification time (newest first).
# If you want to see standard long listing, you can uncomment the ls line below.
# ls -lt build/*.com

for file in tstring.com sieve.com e.com tm.com ttt.com pihex.com mm.com; do
    if [ -f "build/$file" ]; then
        stat -c "%s %n" "build/$file"
    fi
done
