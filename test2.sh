#!/bin/bash

# Script per testare il server. Il socket al quale i client si devono connettere è il
# primo parametro passato allo script

# Lancio il server in background
valgrind --track-origins=yes --leak-check=full --show-leak-kinds=all ./fs-server.out -f test2.conf &
server_pid=$!
# Un breve delay per assicurare che il server sia partito
sleep 1

# se non presenti crea la directory save_reads e save_writes e save_writes_2
if [ ! -d save_reads ]; then
    mkdir save_reads
fi
if [ ! -d save_writes ]; then
    mkdir save_writes
fi
if [ ! -d save_writes_2 ]; then
    mkdir save_writes_2
fi

./client.out -p -f $1 -w testcases,0 -D save_writes
./client.out -p -f $1 -R 5
./client.out -p -f $1 -R 0 -d save_reads &
./client.out -p -f $1 -W "testcases/pic" -D save_writes_2

# Quindi invia il segnale di terminazione al server
printf "\n\n\n******* SIGHUP inviato a $(ps --no-headers -o command $server_pid) *******\n"
kill -1 $server_pid
# aspetta la terminazione del server e poi termina lo script
while [ "x"$(ps --no-headers -o comm $server_pid) != "x" ]; do
    sleep 1
done
echo "Test 2 terminato"

