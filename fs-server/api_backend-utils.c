// header progetto
#include <utils.h>
#include <icl_hash.h> // per hashtable
#include <server-utils.h>
#include <fs-api.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// Cerca il file con quel nome nel server: se lo trova ritorna un puntatore ad esso
// Altrimenti ritorna NULL
struct fs_filedata_t *find_file(struct fs_ds_t *ds, const char *fname) {
    // cerco il file nella tabella in ME
    if(pthread_mutex_lock(&(ds->mux_ht)) == -1) {
        // Fallita operazione di lock
        return NULL;
    }
    struct fs_filedata_t *file = icl_hash_find(ds->fs_table, (void *)fname);
    if(pthread_mutex_unlock(&(ds->mux_ht)) == -1) {
        // Fallita operazione di unlock
        return NULL;
    }
    return file;
}

// Inserisce nel fileserver il file path con contenuto buf, di dimensione size, proveniente dal socket client
// Se l'inserimento riesce allora ritorna un puntatore al file originale e rimpiazza i dati con buf
// Se buf == NULL allora crea un file vuoto, ritornando il file stesso
// Se l'operazione fallisce ritorna NULL
struct fs_filedata_t *insert_file(struct fs_ds_t *ds, const char *path, const void *buf, const size_t size, const int client) {
    // Duplico il path per usarlo come chiave
    char *path_cpy = strndup(path, strlen(path) + 1);
    if(!path_cpy) {
        // errore di allocazione
        return NULL;
    }

    // newfile conterrà il file aggiornato
    struct fs_filedata_t *newfile = NULL;
    // oldfile conterrà il file prima dell'aggiornamento
    struct fs_filedata_t *oldfile = NULL;

    // Se buf è NULL allora significa che sto inserendo un nuovo file (vuoto)
    if(!buf) {
        // Alloco la struttura del file
        if((oldfile = malloc(sizeof(struct fs_filedata_t))) == NULL) {
            // errore di allocazione
            free(path_cpy);
            return NULL;
        }
        if((oldfile->openedBy = malloc(sizeof(int))) == NULL) {
            // errore di allocazione: libero memoria
            free(path_cpy);
            free_file(oldfile);
            return NULL;
        }
        oldfile->size = 0;
        oldfile->openedBy[0] = client; // setto il file come aperto da questo client
        oldfile->nopened = 1; // il numero di client che ha il file aperto
        oldfile->data = NULL; // non ho dati da inserire

        // Incremento il numero di file aperti nel server
        ds->curr_files++;
        // Se necessario modifico il massimo numero di file aperti contemporaneamente
        // durante l'uptime del server
        if(ds->curr_files > ds->max_nfiles) {
            ds->max_nfiles = ds->curr_files;
        }

        // Inserisco il pathname del file anche nella coda della cache
        if(pthread_mutex_lock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di lock
            free_file(oldfile);
            free(path_cpy);
            return NULL;
        }

        int queued = -1;
        // non utilizzo il parametro socket, per cui lo posso settare ad un socket non valido (-1)
        queued = enqueue(ds->cache_q, path_cpy, strlen(path) + 1, -1);

        if(pthread_mutex_unlock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di unlock
            queued = -1;
        }

        if(queued == -1) {
            // fallito inserimento in coda
            free_file(oldfile);
            free(path_cpy);
            return NULL;
        }

        newfile = oldfile; // come da semantica
    }
    // Altrimenti devo troncare il file (sostituisco il buffer)
    else {
        // recupero il file
        oldfile = find_file(ds, path);
        if(oldfile) {
            // copio i vecchi dati
            if((newfile = malloc(sizeof(*oldfile))) == NULL) {
                // errore di allocazione
                return NULL;
            }
            memmove(newfile, oldfile, sizeof(*oldfile)); // nella remota eventualità di sovrapposizioni
            // modifico il contenuto
            free(newfile->data);
            if((newfile->data = malloc(size)) == NULL) {
                // errore di allocazione
                free_file(newfile);
                return NULL;
            }
            memmove(newfile->data, buf, size);
        }
    }

    void *res = NULL;
    // Adesso accedo alla ht in mutua esclusione ed inserisco la entry
    if(pthread_mutex_lock(&(ds->mux_ht)) == -1) {
        free(path_cpy);
        if(oldfile) free_file(oldfile);
        if(newfile) free_file(newfile);
        return NULL;
    }

    res = icl_hash_insert(ds->fs_table, path_cpy, newfile);

    if(pthread_mutex_unlock(&(ds->mux_ht)) == -1) {
        // Fallita operazione di unlock
        res = NULL;
    }
    if(!res) {
        free(path_cpy);
        if(oldfile) free_file(oldfile);
        if(newfile) free_file(newfile);
        return NULL;
    }

    return newfile; // ritorno il puntatore al file inserito
}

// Algoritmo di rimpiazzamento dei file: rimuove uno o più file per fare spazio ad un file di dimensione
// newsz byte, in modo tale da avere una occupazione in memoria inferiore a ds->max_mem - newsz al termine
// Se dirname non è NULL allora i file rimossi sono inviati in dirname, assegnando il path come nome del file
// Ritorna 0 se il rimpiazzamento è avvenuto con successo, -1 altrimenti.
int cache_miss(struct fs_ds_t *ds, const char *dirname, size_t newsz) {
    int res = 0; // conterrà il valore di ritorno

    // È possibile che il file non possa rientrare nel quantitativo di memoria del server
    // In tal caso l'algoritmo fallisce (e qualunque operazione collegata)
    if(newsz > ds->max_mem) {
        return -1;
    }

    res = 0;
    int errno_saved = 0;
    if(dirname) {
        int dir;
        if((dir = chdir(dirname)) == -1) {
            return -1;
        }
    }

    // Almeno un file deve essere espulso (se è per il numero di file presenti allora newsz == 0
    // perciò il ciclo viene eseguito una sola volta)
    do {
        // Secondo la politica FIFO la vittima è la testa della coda cache_q
        if(pthread_mutex_lock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di lock
            return -1;
        }

        // ottengo il path in victim->data
        struct node_t *victim = pop(ds->cache_q); // non dovrebbe ritornare NULL (ragionevolmente)

        if(pthread_mutex_unlock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di unlock
            return -1;
        }
        if(!victim) {
            // pop ha ritornato NULL per qualche motivo
            return -1;
        }

        // Rimuove il file con questo pathname dalla ht
        if(pthread_mutex_lock(&(ds->mux_ht)) == -1) {
            free(victim->data);
            free(victim);
            return -1;
        }

        // prima devo cercarlo per sapere la sua size
        struct fs_filedata_t *file = find_file(ds, victim->data);
        size_t sz = file->size;

        res = 0;
        // Notare che la memoria occupata dal file non viene liberata e mantengo un puntatore al file
        // Libero invece la memoria occupata dal path, dato che lo conosco già
        if((res = icl_hash_delete(ds->fs_table, victim->data, free, NULL)) != -1) {
            // rimozione OK, decremento numero file nel server e quantità di memoria occupata
            ds->curr_files--;
            ds->curr_mem -= sz;
        }

        if(pthread_mutex_unlock(&(ds->mux_ht)) == -1) {
            res = -1;
        }
        if(res == -1) {
            return -1;
        }

        free_file(file);
        free(victim->data);
        free(victim);


        // concateno il path della directory e del file
        size_t len = strlen((char*)victim->data) + strlen(dirname) + 1;
        char *complete_path = calloc(1, len * sizeof(char));
        int newfd;
        if(!complete_path) {
            // errore di allocazione
            errno_saved = errno;
            res = -1;
        }
        else {
            strncat(complete_path, dirname, len);
            strncat(complete_path, (char*)victim->data, len);
            if((newfd = creat(complete_path, PERMS_ALL_READ)) == -1) {
                // impossibile creare il nuovo file
                errno_saved = errno;
                res = -1;
            }
            else {
                // Se necessario scrivo il file nella directory dirname
                res = 0;
                size_t tot = file->size;

                while(tot > 0 && res != -1) {
                    int n = writen(newfd, file->data, tot);
                    if(n == -1) {
                        // impossibile scrivere il file
                        errno_saved = errno;
                        res = -1;
                    }
                    else {
                        tot -= n;
                    }
                }
            }
        }


    }
    while(ds->curr_mem >= ds->max_mem - newsz);

    // aggiorno il numero di chiamate (che hanno avuto successo) dell'algorimto di rimpiazzamento
    if(res == 0) ds->cache_triggered++;

    errno = errno_saved;

    return res;
}
