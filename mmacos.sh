rm -f dcc dccpeep dccrtlstrip dccmake

sh src/dcc/build-dcc.sh
clang -std=c89 -Wall -Wextra -O2 -g -o dccpeep src/dccpeep/dccpeep.c
clang -std=c89 -Wall -Wextra -O2 -g -o dccrtlstrip src/dccrtlstrip/dccrtlstrip.c
clang -std=c89 -Wall -Wextra -O2 -g -o dccmake src/dccmake/dccmake.c

chmod +x runall.sh
chmod +x ma.sh