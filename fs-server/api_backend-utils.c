// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
#include <icl_hash.h> // per hashtable
// header multithreading
#include <pthread.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
        oldfile->size = 0; // file vuoto
        oldfile->openedBy[0] = client; // setto il file come aperto da questo client
        oldfile->nopened = 1; // il numero di client che ha il file aperto
        oldfile->data = NULL; // non ho dati da inserire
        oldfile->lockedBy = -1; // nessun socket ha il lock

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
                free(path_cpy);
                return NULL;
            }
            if((newfile->data = malloc(size)) == NULL) {
                // errore di allocazione
                free_file(newfile);
                free(path_cpy);
                return NULL;
            }
            memmove(newfile, oldfile, sizeof(*oldfile)); // nella remota eventualità di sovrapposizioni
            // modifico il contenuto
            memmove(newfile->data, buf, size);
            // copio i socket che hanno il file aperto
            memmove(newfile->openedBy, oldfile->openedBy, oldfile->nopened);
        }
    }

    void *res = NULL;
    // Adesso accedo alla ht in mutua esclusione ed inserisco la entry aggiornata
    if(pthread_mutex_lock(&(ds->mux_ht)) == -1) {
        if(oldfile) free_file(oldfile);
        if(newfile) free_file(newfile);
        free(path_cpy);
        return NULL;
    }

    res = icl_hash_update_insert(ds->fs_table, path_cpy, newfile, (void**)&oldfile);

    if(pthread_mutex_unlock(&(ds->mux_ht)) == -1) {
        // Fallita operazione di unlock
        res = NULL;
    }

    if(oldfile) free_file(oldfile);
    if(!res) {
        free(path_cpy);
        if(newfile) free_file(newfile);
        return NULL;
    }

    return newfile; // ritorno il puntatore al file inserito
}

// Algoritmo di rimpiazzamento dei file: rimuove uno o più file per fare spazio ad un file di dimensione
// newsz byte, in modo tale da avere una occupazione in memoria inferiore a ds->max_mem - newsz al termine
// Ritorna il numero di file espulsi (>0) se il rimpiazzamento è avvenuto con successo, -1 altrimenti.
// Se la funzione ha successo inizializza e riempe con i file espulsi ed i loro path le due code passate come
// parametro alla funzione, altrimenti se l'algoritmo di rimpiazzamento fallisce
// esse non sono allocate e comunque lasciate in uno stato inconsistente
int cache_miss(struct fs_ds_t *ds, size_t newsz, struct Queue **paths, struct Queue **files) {
    int success = 0; // conterrà il valore di ritorno

    // È possibile che il file possa non entrare nel quantitativo di memoria
    // assegnato al server all'avvio. In tal caso l'algoritmo fallisce (e qualunque operazione collegata)
    if(newsz > ds->max_mem) {
        return -1;
    }

    // creo le tre code
    *paths = queue_init();
    if(!(*paths)) {
        // fallita alloc coda
        return -1;
    }
    *files = queue_init();
    if(!(*files)) {
        // fallita alloc coda
        free_Queue(*paths);
        return -1;
    }

    int errno_saved = 0;

    // Almeno un file deve essere espulso se chiamo l'algoritmo, perciò uso un do-while
    do {
        // Secondo la politica FIFO la vittima è la testa della coda cache_q
        if(pthread_mutex_lock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di lock
            free_Queue(*paths);
            free_Queue(*files);
            return -1;
        }

        // ottengo il path del file da togliere in victim->data
        struct node_t *victim = pop(ds->cache_q); // non dovrebbe ritornare NULL (ragionevolmente)

        if(pthread_mutex_unlock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di unlock
            free_Queue(*paths);
            free_Queue(*files);
            return -1;
        }
        if(!victim) {
            // pop ha ritornato NULL per qualche motivo
            free_Queue(*paths);
            free_Queue(*files);
            return -1;
        }

        // Rimuove il file con questo pathname dalla ht
        // prima devo cercarlo per sapere la sua size (mutex interna a find_file)
        struct fs_filedata_t *fptr = find_file(ds, victim->data);
        size_t sz = fptr->size;

        if(pthread_mutex_lock(&(ds->mux_ht)) == -1) {
            free(victim->data);
            free(victim);
            return -1;
        }

        success = 0;
        // Notare che la memoria occupata dal file non viene liberata subito e mantengo un puntatore al file
        // Libero invece la memoria occupata dalla chiave, dato che non è più utile
        if((success = icl_hash_delete(ds->fs_table, victim->data, free, NULL)) != -1) {
            // rimozione OK, decremento numero file nel server e quantità di memoria occupata
            ds->curr_files--;
            ds->curr_mem -= sz;
        }

        if(pthread_mutex_unlock(&(ds->mux_ht)) == -1) {
            success = -1;
        }
        if(success == -1) {
            free_Queue(*paths);
            free_Queue(*files);
            free_file(fptr);
            free(victim->data);
            free(victim);
            return -1;
        }

        // Aggiorno le code con il nuovo file espulso
        if(enqueue(*paths, victim->data, strlen((char*)victim->data) + 1, -1) == -1) {
            // fallito inserimento del path del file in coda
            success = -1;
            break;
        }
        if(enqueue(*files, fptr, sizeof(struct fs_filedata_t), -1) == -1) {
            // fallito inserimento del file in coda
            success = -1;
            break;
        }

        // posso liberare il nodo contenente il path
        free(victim->data);
        free(victim);

        success++; // ho espulso con successo un altro file, quindi incremento il contatore
    }
    while(ds->curr_mem + newsz >= ds->max_mem);

    // aggiorno il numero di chiamate (che hanno avuto successo) dell'algorimto di rimpiazzamento
    if(success > 0) ds->cache_triggered++;
    if(logging(ds, 0, "Algoritmo di rimpiazzamento chiamato") == -1) {
        perror("Algoritmo di rimpiazzamento chiamato");
    }

    errno = errno_saved;

    return success;
}
