#!/bin/bash

# Un breve delay per assicurare che il server sia partito
sleep 5

# Script per testare il server. Il socket al quale i client si devono connettere è il
# primo parametro passato allo script

# Scrive nel server tutti i file presenti nella directory testcases, usando come directory
# per salvare eventuali file espulsi la directory save_writes creata
mkdir save_writes
./client.out -p -f $1 -w testcases,0 -D save_writes
#rm -r save_writes


# Quindi invia il segnale di terminazione al server. In questo caso il server sta eseguendo
# sotto valgrind, perciò il pid è quello di valgrind.bin
kill -2 $(pidof fs-server.out)

#rm -r save_reads
