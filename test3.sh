#!/bin/bash

# Lancio il server in background
./fs-server.out -f test3.conf &
# Un breve delay per assicurare che il server sia partito
sleep 1

# Script per testare il server. Il socket al quale i client si devono connettere è il
# primo parametro passato allo script

while


# Quindi invia il segnale di terminazione veloce al server
kill -2 $(pidof fs-server.out)

