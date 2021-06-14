#!/bin/bash

# Script per testare il funzionamento delle opzioni del client e la relativa implementazione
# Il server viene eseguito con valgrind per catturare eventuali memory leak
# Il socket del server al quale i client si devono connettere è il primo parametro passato allo script
# ed è specificato nel file test1.conf generato dal target test1 del Makefile

# Lancio il server in background con valgrind
valgrind --track-origins=yes --leak-check=full --show-leak-kinds=all ./fs-server.out -f test1.conf &
# Un breve delay per assicurare che il server sia partito
sleep 1
# Setto il delay delle operazioni dei client a 200ms
delay=200
# Scrive i file f1 e f2 (vuoti)
# Poi concatena a f1 il file testcases/inputs/input0.txt (42 bytes)
# ed a f2 la stringa "ciao mondo" (11 bytes)
./client.out -p -t $delay -f $1 -W f1,f2 -a "f1:testcases/inputs/input0.txt,f2:ciao mondo"
# Il client legge il file f2, che conterrà "ciao mondo" e lo scarta
./client.out -p -t $delay -f $1 -r f2
# Scrive nel server tutti i file presenti nella directory testcases (e ricorsivamente nelle sottodirectory)
# Eventuali file espulsi per capacity misses vengono buttati via dal client
# (anche se in questo caso non succede: vi sono 50 file per un totale di 17 Mbytes,
# per cui rientra nei limiti imposti dal fileserver)
./client.out -p -t $delay -f $1 -w testcases,0
# Legge tutti i file presenti nel server (tutti quelli in testcases) e li salva nella directory save_reads
mkdir save_reads
./client.out -p -t $delay -f $1 -R 0 -d save_reads

# Ottengo il PID del server
# In questo caso il server sta eseguendo con valgrind, perciò il pid è quello di valgrind.bin
server_pid=$(pidof valgrind.bin)
# Quindi invia il segnale di terminazione lenta al server (SIGHUP)
kill -2 $server_pid
# aspetta la terminazione del server e poi termina lo script
wait $server_pid
echo "Test 1 terminato"
