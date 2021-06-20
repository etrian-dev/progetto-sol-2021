#!/bin/bash

# Script per il parsing del file di log
# Prende come parametro il file di log da analizzare

# ottengo numero di api_openFile eseguite con successo
fn='api_openFile'
regex='([^)]*)..OK'
nopen=$(grep "$fn$regex" $1 | wc -l)
# ottengo numero di api_closeFile eseguite con successo
fn='api_closeFile'
nclose=$(grep $fn$regex $1 | wc -l)
# ottengo numero di api_readFile eseguite con successo
fn='api_readFile'
nread=$(grep "$fn$regex" $1 | wc -l)
# ottengo numero di api_openFile(O_LOCKFILE) eseguite con successo
fn='api_openFile'
regex='([^|]*|O_LOCKFILE)..OK'
nopen_locked=$(grep "$fn$regex" $1 | wc -l)
# ottengo numero di api_writeFile eseguite con successo
fn='api_writeFile'
regex='([^)]*)[^)]*)..OK'
nwrite=$(grep "$fn$regex" $1 | wc -l)
# nelle linee che seguono prima conto il numero di write completate con successo
# poi estraggo da esse la somma delle size
sum=0
grep "api_writeFile([^)]*)[^)]*)..OK" $1 | cut -d '(' -f 3 | cut -d ')' -f 1 | cut -d ' ' -f 1 > sizes
for sz in $(cat sizes); do
    (( sum += $sz ))
done
rm sizes
# calcolo media con bc
avg_write=$(echo "$sum / $nwrite" | bc -q)

# ottengo numero di api_appendToFile eseguite con successo
fn='api_appendToFile'
regex='([^)]*)[^)]*)..OK'
nappend=$(grep "$fn$regex" $1 | wc -l)
# ottengo numero di api_lockFile eseguite con successo
fn='api_lockFile'
regex='([^)]*)..OK'
nlock=$(grep "$fn$regex" $1 | wc -l)
# ottengo numero di api_unlockFile eseguite con successo
fn='api_unlockFile'
nunlock=$(grep "$fn$regex" $1 | wc -l)
# ottengo numero di api_rmFile eseguite con successo
fn='api_rmFile'
nrm=$(grep "$fn$regex" $1 | wc -l)
# ottengo il numero di richieste servite da ciascun worker e le salvo in un array
regex="\[WORKER [^\]*\]:"
grep "$regex" $1 | cut -d '[' -f 3 | cut -d ']' -f 1 | cut -d ' ' -f 2 > workers
grep "$regex" $1 | cut -d ']' -f 3 | grep -o [[:digit:]]* > requests
j=0
for i in $(cat workers); do
    work_threads[$j]=$i
    # per avere il contenuto della j+1-esima riga del file requests
    (( k = $j + 1 ))
    served[$j]=$(head -n k requests | tail -n 1)
    ((j++))
done
rm workers
rm requests
# ottengo il numero di chiamate all'algoritmo di rimpiazzamento dei file
nmiss=$(grep "Capacity misses" $1 | cut -d ']' -f 3 | cut -d ':' -f 3 | tr -d ' ')
# ottengo la massima occupazione di memoria dei file nel server
# ottengo il massimo numero di file presenti contemporaneamente nel server
# ottengo il massimo numero di connessioni contemporanee

echo "Numero di openFile riuscite: $nopen"
echo "Numero di openFile(O_LOCKFILE) riuscite: $nopen_locked"
echo "Numero di closeFile riuscite: $nclose"
echo "Numero di readFile riuscite: $nread"
echo "Numero di writeFile riuscite: $nwrite (dimensione media $avg_write bytes)"
echo "Numero di appendToFile riuscite: $nappend"
echo "Numero di lockFile riuscite: $nlock"
echo "Numero di unlockFile riuscite: $nunlock"
echo "Numero di removeFile riuscite: $nrm"
echo "Numero di capacity misses (chiamate all'algoritmo di rimpiazzamento): $nmiss"
echo "Dimensione della threadpool: $j"
for (( i=0; i < j; i++ )); do
    printf "Worker %d ha servito %d richieste\n" ${work_threads[$i]} ${served[$i]}
done
