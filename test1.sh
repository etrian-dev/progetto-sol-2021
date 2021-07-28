#!/bin/bash

# Script per testare il funzionamento delle opzioni del client e la relativa implementazione
# Il server viene eseguito con valgrind per catturare eventuali memory leak
# Il socket del server al quale i client si devono connettere è il primo parametro passato allo script
# ed è specificato nel file test1.conf generato dal target test1 del Makefile

# Lancio il server in background con valgrind
valgrind --leak-check=full ./fs-server.out -f test1.conf &
# Recupero il PID del server per inviargli poi il segnale
server_pid=$!
# Un breve delay per assicurare che il server sia partito
sleep 1
# Setto il delay delle operazioni dei client a 200ms
delay=200

# se non presenti crea la directory save_reads e save_writes
if [ ! -d save_reads ]; then
    mkdir save_reads
fi
if [ ! -d save_writes ]; then
    mkdir save_writes
fi

# Lancio vari client in background che testano singolarmente le opzioni implementate
./client.out -h
./client.out -p -t $delay -f $1 -W testcases/inputs/input0.txt,testcases/inputs/input1.txt
./client.out -p -t $delay -f $1 -a "testcases/inputs/input1.txt:testcases/inputs/prova0.in,testcases/inputs/input1.txt:ciao mondo"
./client.out -p -t $delay -f $1 -r testcases/inputs/input1.txt -r testcases/pi.txt
./client.out -p -t $delay -f $1 -l testcases/inputs/input1.txt -u testcases/inputs/input1.txt &
./client.out -p -t $delay -f $1 -l testcases/inputs/input1.txt
./client.out -p -t $delay -f $1 -w testcases,0 -D save_writes
./client.out -p -t $delay -f $1 -R 0
./client.out -p -t $delay -f $1 -R 5 -d save_reads
./client.out -p -t $delay -f $1 -c testcases/inputs/input1.txt,testcases/inputs/input0.txt,testcases/zzzzzzzzzzz

# Quindi invia il segnale di terminazione lenta al server (SIGHUP)
printf "\n\n\n******* SIGHUP inviato a $(ps --no-headers -o command $server_pid) *******\n"
kill -1 $server_pid

# aspetta la terminazione del server e poi termina lo script
while [ "x"$(ps --no-headers -o comm $server_pid) != "x" ]; do
    sleep 1
done
echo "Test 1 terminato"
