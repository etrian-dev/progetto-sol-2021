#!/bin/bash

# Script per il parsing del file di log
# Prende come parametro il file di log da analizzare

fn='api_openFile'
regex='([^)]*)..OK'
nopen=$(grep "$fn$regex" $1 | wc -l)

fn='api_openFile'
nclose=$(grep $fn$regex $1 | wc -l)

regex='([^|]*|O_LOCKFILE)..OK'
nopen_locked=$(grep "$fn$regex" $1 | wc -l)

fn='api_readFile'
regex='([^)]*)..OK'
nread=$(grep "$fn$regex" $1 | wc -l)

fn='api_writeFile'
regex='([^)]*)[^)]*)..OK'
nwrite=$(grep "$fn$regex" $1 | wc -l)
# prima conto il numero di write completate con successo
# poi estraggo da esse la somma delle size
sum=0
grep "api_writeFile([^)]*)[^)]*)..OK" $1 | cut -d '(' -f 3 | cut -d ')' -f 1 | cut -d ' ' -f 1 > sizes
for sz in $(cat sizes); do
    (( sum += $sz ))
done
rm sizes
# calcolo media con bc
avg_write=$(echo "$sum / $nwrite" | bc -q)

fn='api_appendToFile'
regex='([^)]*)[^)]*)..OK'
nappend=$(grep "$fn$regex" $1 | wc -l)

fn='api_lockFile'
regex='([^)]*)..OK'
nlock=$(grep "$fn$regex" $1 | wc -l)

fn='api_unlockFile'
nunlock=$(grep "$fn$regex" $1 | wc -l)

fn='api_rmFile'
nrm=$(grep "$fn$regex" $1 | wc -l)

echo "Numero di openFile riuscite: $nopen"
echo "Numero di openFile(O_LOCKFILE) riuscite: $nopen_locked"
echo "Numero di closeFile riuscite: $nclose"
echo "Numero di readFile riuscite: $nread"
echo "Numero di writeFile riuscite: $nwrite (dimensione media $avg_write bytes)"
echo "Numero di appendToFile riuscite: $nappend"
echo "Numero di lockFile riuscite: $nlock"
echo "Numero di unlockFile riuscite: $nunlock"
echo "Numero di removeFile riuscite: $nrm"

