#!/bin/bash

# Questo script genera un file di configurazione per il server nel formato opportuno.
# I valori da inserire sono contenuti nei primi cinque parametri passati allo script
# Mentre il sesto parametro è il path del file di configurazione da creare

# $1: dimensione della threadpool
# $2: quantità massima di memoria (in Mbytes)
# $3: massimo numero di files
# $4: path del socket
# $5: path del log file
# $6: path del file di configurazione da creare
echo -e "tpool\t$1\nmaxmem\t$2\nmaxfiles\t$3\nsock_path\t$4\nlog_path\t$5" > $6
