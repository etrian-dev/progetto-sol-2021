#!/bin/bash

# Script per eseguire i comandi del target test1

workers=1
        mem=128
            nfiles=10000
                   socket="./test1.sock"
                          log="./test1.log"
                              echo -e "tpool\t$workers\nmaxmem\t$mem\nmaxfiles\t$nfiles\nsock_path\t$socket\nlog_path\t$log" \
                              > test1.conf

                              valgrind --leak-check=full --show-leak-kinds=all ./fs-server.out -f test1.conf

# un breve delay per assicurare che il server sia pronto a ricevere connessioni

#delay=200
#echo $socket
#bash -c "./client.out -p -t $delay -f $socket -r file1"
