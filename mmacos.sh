rm -f dcc dccpeep dccrtlstrip dccmake m80c

sh src/dcc/build-dcc.sh
clang -std=c89 -Wall -Wextra -O2 -g -o dccpeep src/dccpeep/dccpeep.c
clang -std=c89 -Wall -Wextra -O2 -g -o dccrtlstrip src/dccrtlstrip/dccrtlstrip.c
clang -std=c89 -Wall -Wextra -O2 -g -o dccmake src/dccmake/dccmake.c
clang -std=c89 -Wall -Wextra -O2 -g -o m80c src/m80c/m80c.c

chmod +x runall.sh
chmod +x ma.sh