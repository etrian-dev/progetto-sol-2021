#!/bin/bash

# Lancio il server in background
./fs-server.out -f test2.conf &
# Un breve delay per assicurare che il server sia partito
sleep 1

# Script per testare il server. Il socket al quale i client si devono connettere è il
# primo parametro passato allo script

# Scrive nel server tutti i file presenti nella directory testcases, usando come directory
# per salvare eventuali file espulsi la directory save_writes creata
mkdir save_writes
mkdir save_reads
./client.out -p -f $1 -w testcases,0 -D save_writes
./client.out -p -f $1 -R 5
./client.out -p -f $1 -R 0 -d save_reads

server_pid=$(pidof ./fs-server.out)
# Quindi invia il segnale di terminazione al server
printf "\n\n\n******* SIGHUP inviato a $(ps --no-headers -o command $server_pid) *******\n"
kill -2 $server_pid
# aspetta la terminazione del server e poi termina lo script
while [ "x"$(ps --no-headers -o comm $server_pid) != "x" ]; do
    sleep 1
done
echo "Test 2 terminato"

