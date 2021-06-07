#!/bin/bash

# Script per testare il server. Il socket al quale i client si devono connettere è il
# primo parametro passato allo script

delay=200

# Scrive i file f1 e f2 (vuoti), poi concatena a f1 il file utils.h e a f2 la stringa "ciao mondo"
./client.out -p -t $delay -f $1 -W f1,f2 -a "f1:utils.h,f2:ciao mondo"
# Scrive nel server tutti i file presenti nella directory testcases
./client.out -p -t $delay -f $1 -w testcases,0
# Legge tutti i file presenti in testcases e li salva nella directory save_reads
mkdir save_read
./client.out -p -t $delay -f $1 -R 0 -d save_read
