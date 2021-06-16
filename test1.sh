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
# Scrive i file testcases/inputs/input0.txt (42 bytes) e testcases/inputs/input1.txt (16 bytes)
# Poi concatena a testcases/inputs/input0.txt il file testcases/inputs/prova0.in (27 bytes)
# ed a testcases/inputs/input1.txt la stringa "ciao mondo" (11 bytes)
./client.out -p -t $delay -f $1 -W testcases/inputs/input0.txt,testcases/inputs/input1.txt \
-a "testcases/inputs/input1.txt:testcases/inputs/prova0.in,testcases/inputs/input1.txt:ciao mondo"
# Il client legge il file testcases/inputs/input1.txt e lo scarta
./client.out -p -t $delay -f $1 -r testcases/inputs/input1.txt
# Il client effettua lock ed unlock dello stesso file a 200ms di distanza (in background)
./client.out -p -t $delay -f $1 -l testcases/inputs/input1.txt -u testcases/inputs/input1.txt &
# Il client apre e poi tenta di rimuovere dal server i file inseriti ed uno non presente (la cui apertura/rimozione deve fallire)
# Siccome input1.txt è lockato dal client mandato in esecuzione in precedenza è probabile dover attendere che la mutua esclusione sia
# rilasciata prima di poter aprire e rimuovere quel file
./client.out -p -t $delay -f $1 -l testcases/inputs/input1.txt \
-a "testcases/inputs/input1.txt:test\ntest\ntest" -u testcases/inputs/input1.txt \
-c testcases/inputs/input1.txt,testcases/inputs/input0.txt,testcases/zzzzzzzzzzz
# Scrive nel server tutti i file presenti nella directory testcases (e ricorsivamente nelle sottodirectory)
# Eventuali file espulsi per capacity misses vengono buttati via dal client
# (anche se in questo caso non succede: vi sono 50 file per un totale di 17 Mbytes,
# per cui rientrano nei limiti della configurazione del fileserver per il test1)
./client.out -p -t $delay -f $1 -w testcases,0
# Legge tutti i file presenti nel server (tutti quelli in testcases) e li salva nella directory save_reads
mkdir save_reads
#./client.out -p -t $delay -f $1 -R 0 -d save_reads

# Ottengo il PID del server
# In questo caso il server sta eseguendo con valgrind, perciò il pid è quello di valgrind.bin
server_pid=$(pidof valgrind.bin)
# Quindi invia il segnale di terminazione lenta al server (SIGHUP)
kill -2 $server_pid
echo "SIGHUP inviato a $server_pid"
# aspetta la terminazione del server e poi termina lo script
while [ "x"$(ps --no-headers -o comm $server_pid) != "x" ]; do
    sleep 1
done
echo "Test 1 terminato"
