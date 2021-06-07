#!/bin/bash

# Questo script genera un file di configurazione per il server nel formato opportuno.
# I valori da inserire sono contenuti nei primi cinque parametri passati allo script
# Mentre il sesto parametro è il path del file di configurazione da creare

echo -e "tpool\t$1\nmaxmem\t$2\nmaxfiles\t$3\nsock_path\t$4\nlog_path\t$5" > $6
