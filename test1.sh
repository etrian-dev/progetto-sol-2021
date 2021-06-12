#!/bin/bash

# Un breve delay per assicurare che il server sia partito
sleep 1

# Script per testare il server. Il socket al quale i client si devono connettere è il
# primo parametro passato allo script

# Setto il delay dei client a 200ms
delay=200
# stampa il messaggio di uso (lancio il client con -p, ma -h ha la priorità)
./client.out -p -t $delay -h
# Scrive i file f1 e f2 (vuoti), poi concatena a f1 il file testcases/inputs/input0.txt
# e a f2 la stringa "ciao mondo"
./client.out -p -t $delay -f $1 -W f1,f2 -a "f1:testcases/inputs/input0.txt,f2:ciao mondo"
# Legge il file f2, che conterrà "ciao mondo"
./client.out -p -t $delay -f $1 -r f2
# Scrive nel server tutti i file presenti nella directory testcases, usando come directory
# per salvare eventuali file espulsi la directory save_writes creata
mkdir save_writes
./client.out -p -t $delay -f $1 -w testcases,0 -D save_writes
# Legge tutti i file presenti nel server e li salva nella directory save_reads
mkdir save_reads
./client.out -p -t $delay -f $1 -R 5 -d save_reads -r f2 -d save_reads

kill -1 $(pidof valgrind.bin)
