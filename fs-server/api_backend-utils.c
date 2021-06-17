// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
#include <icl_hash.h> // per hashtable
// header multithreading
#include <pthread.h>
// syscall headers
#include <signal.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Funzione che crea un nuovo file con i parametri specificati
// Ritorna un puntatore se ha successo, NULL altrimenti
struct fs_filedata_t *newfile(const int client, const int flags) {
    struct fs_filedata_t *file = NULL;
    // Alloco ed inizializzo il file
    if((file = malloc(sizeof(struct fs_filedata_t))) == NULL) {
        // errore di allocazione
        return NULL;
    }
    if((file->openedBy = malloc(sizeof(int))) == NULL) {
        // errore di allocazione: libero memoria
        free_file(file);
        return NULL;
    }
    file->openedBy[0] = client; // setto il file come aperto da questo client
    file->nopened = 1; // il numero di client che ha il file aperto
    file->data = NULL;
    file->size = 0;
    pthread_mutex_init(&(file->mux_file), NULL);
    pthread_cond_init(&(file->mod_completed), NULL);
    file->modifying = 0;
    // Se devo settare mutua esclusione da parte del client che lo crea lo faccio, altrimenti -1
    if(flags & O_LOCKFILE) {
        file->lockedBy = client;
    }
    else {
        file->lockedBy = -1;
    }
    return file;
}

// Cerca il file con quel nome nel server: se lo trova ritorna un puntatore ad esso
// Altrimenti ritorna NULL
struct fs_filedata_t *find_file(struct fs_ds_t *ds, const char *fname) {
    // cerco il file nella hash table
    struct fs_filedata_t *file = NULL;
    file = icl_hash_find(ds->fs_table, (void *)fname);
    return file;
}

// Se buf == NULL crea un nuovo file vuoto aperto dal client passato come parametro
// Se buf != NULL concatena buf, di dimensione size, al file con pathname path già presente nel server
// In entrambi i casi ritorna un puntatore al file se ha successo, NULL altrimenti
struct fs_filedata_t *insert_file(
    struct fs_ds_t *ds,
    const char *path,
    const int flags,
    const void *buf,
    const size_t size,
    const int client)
    {
    // file conterrà il file aggiornato
    struct fs_filedata_t *file = NULL;
    void *res = NULL;
    // Se buf è NULL allora significa che sto inserendo un nuovo file (vuoto)
    if(!buf) {
        // Duplico il path per usarlo come chiave
        char *key = strndup(path, strlen(path) + 1);
        if(!key) {
            // errore di allocazione
            return NULL;
        }
        // Creo un nuovo file, opzionalmente settando O_LOCKFILE
        file = newfile(client, flags);
        if(!file) {
            // fallita creazione file
            free(key);
            return NULL;
        }

        // Aggiorno il numero di files presenti nel server
        LOCK_OR_KILL(ds, ds->mux_files, ds);
        ds->curr_files++;
        if(ds->curr_files > ds->max_nfiles) {
            ds->max_nfiles = ds->curr_files;
        }
        UNLOCK_OR_KILL(ds, ds->mux_files);

        // Inserisco il pathname del file nella coda di file
        LOCK_OR_KILL(ds, ds->mux_cacheq, ds->cache_q);
        // non utilizzo il parametro socket, per cui lo posso settare ad un socket non valido (-1)
        int queued = -1;
        queued = enqueue(ds->cache_q, key, strlen(path) + 1, -1);
        UNLOCK_OR_KILL(ds, ds->mux_cacheq);

        if(queued == -1) {
            // Inserimento in coda fallito
            free(key);
            free(file);
            return NULL;
        }
        else {
            // Inserimento in coda riuscito: inserisco il file nella tabella hash
            res = icl_hash_insert(ds->fs_table, key, file);
        }
    }
    // Altrimenti devo aggiornare i dati del file
    else {
        // recupero il file
        struct fs_filedata_t *oldfile = find_file(ds, path);
        if(oldfile) {
            LOCK_OR_KILL(ds, oldfile->mux_file, file);
            while(oldfile->modifying == 1) {
                pthread_cond_wait(&(oldfile->mod_completed), &(oldfile->mux_file));
            }
            oldfile->modifying = 1;

            void *newdata = NULL;
            if(oldfile->size > 0) {
                newdata = realloc(oldfile->data, oldfile->size + size);
            }
            else {
                newdata = malloc(size);
            }
            if(newdata) {
                memmove(newdata + oldfile->size, buf, size);
                oldfile->data = newdata;
            }
            // aggiorno la dimensione del file
            oldfile->size += size;

            oldfile->modifying = 0;
            UNLOCK_OR_KILL(ds, oldfile->mux_file);

            // devo aggiornare anche la quantità di memoria occupata nel server
            LOCK_OR_KILL(ds, ds->mux_mem, ds);
            ds->curr_mem += size;
            // aggiorno se necessario anche la massima quantità di memoria occupata
            if(ds->curr_mem > ds->max_used_mem) {
                ds->max_used_mem = ds->curr_mem;
            }
            UNLOCK_OR_KILL(ds, ds->mux_mem);
        }

        file = res = oldfile; // se ho avuto successo allora saranno != NULL
    }
    // NOTA: non controllo la consistenza della coda di file con la hash table,
    // ma a patto di controllare sempre il risultato di find_file non ho problemi
    // per path di file presenti nella coda che non siano anche nella hashtable
    if(!res) {
        free_file(file);
        return NULL;
    }
    return file; // ritorno il puntatore al file inserito
}

// Algoritmo di rimpiazzamento dei file: rimuove uno o più file per fare spazio ad un file di dimensione
// newsz byte, in modo tale da avere una occupazione in memoria inferiore a ds->max_mem - newsz al termine
// Ritorna il numero di file espulsi (>0) se il rimpiazzamento è avvenuto con successo, -1 altrimenti.
// Se la funzione ha successo inizializza e riempe con i file espulsi ed i loro path le due code passate come
// parametro alla funzione, altrimenti se l'algoritmo di rimpiazzamento fallisce
// esse non sono allocate e comunque lasciate in uno stato inconsistente
int cache_miss(struct fs_ds_t *ds, size_t newsz, struct Queue **paths, struct Queue **files) {
    // È possibile che il file sia più grande del quantitativo di memoria massimo fissato all'avvio.
    // In tal caso l'algoritmo fallisce (e qualunque operazione collegata)
    if(newsz > ds->max_mem) {
        return -1;
    }
    // creo le due code
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

    int success = 0;
    int num_evicted = 0;
    // Almeno un file deve essere espulso se chiamo l'algoritmo, perciò uso un do-while
    int errno_saved = 0;

    // siccome il test sulla memoria occupata è nella guardia del while ed anche nel corpo
    // modifico la memoria eseguo tutto il ciclo in mutua esclusione sulle variabili
    // che controllano l'occupazione di memoria all'interno del server
    LOCK_OR_KILL(ds, ds->mux_mem, ds);
    do {
        // Secondo la politica FIFO la vittima è la testa della coda cache_q
        LOCK_OR_KILL(ds, ds->mux_cacheq, ds->cache_q);
        // non dovrebbe ritornare NULL (ragionevolmente) in quanto ho chiamato l'algoritmo perché avevo
        // file che occupavano la memoria del server
        struct node_t *victim = pop(ds->cache_q);
        UNLOCK_OR_KILL(ds, ds->mux_cacheq);

        if(!victim) {
            // pop ha ritornato NULL per qualche motivo
            free_Queue(*paths);
            free_Queue(*files);
            return -1;
        }

        // Rimuovo il file con questo pathname dalla ht
        struct fs_filedata_t *fptr = find_file(ds, victim->data);

        LOCK_OR_KILL(ds, fptr->mux_file, fptr);
        while(fptr->modifying == -1) {
            pthread_cond_wait(&(fptr->mod_completed), &(fptr->mux_file));
        }
        size_t sz = fptr->size;

        // NOTA: la memoria occupata dal file non viene liberata subito e mantengo un puntatore al file
        // Libero invece la memoria occupata dalla chiave, dato che non è più utile
        if((success = icl_hash_delete(ds->fs_table, victim->data, free, NULL)) != -1) {
            // rimozione OK, decremento numero file nel server e quantità di memoria occupata
            LOCK_OR_KILL(ds, ds->mux_files, ds);
            ds->curr_files--;
            UNLOCK_OR_KILL(ds, ds->mux_files);

            ds->curr_mem -= sz; // già in ME sulla memoria
        }
        else {
            errno_saved = errno; // salvo errore
            success = -1;
            UNLOCK_OR_KILL(ds, fptr->mux_file);
            break;
        }

        // Inserisco nelle code il file espulso
        if(enqueue(*files, fptr, sizeof(struct fs_filedata_t), -1) == -1) {
            // fallito inserimento del file in coda
            errno_saved = errno;
            success = -1;
            break;
        }
        UNLOCK_OR_KILL(ds, fptr->mux_file);
        if(enqueue(*paths, victim->data, strlen((char*)victim->data) + 1, -1) == -1) {
            // fallito inserimento del path del file in coda
            errno_saved = errno;
            success = -1;
            break;
        }

        // posso liberare il nodo contenente il path
        free(victim->data);
        free(victim);

        num_evicted++; // ho espulso con successo un altro file, quindi incremento il contatore
    }
    while(ds->curr_mem + newsz >= ds->max_mem);
    UNLOCK_OR_KILL(ds, ds->mux_mem);

    // aggiorno il numero di chiamate (che hanno avuto successo) dell'algorimto di rimpiazzamento
    if(success != -1) {
        if(num_evicted > 0) ds->cache_triggered++;
        // Effettuo il logging dell'operazione stampando il messaggio ed i file espulsi
        if(logging(ds, 0, "[SERVER] Capacity miss: espulsi i seguenti file") == -1) {
            perror("[SERVER] Capacity miss: espulsi i seguenti file");
        }
        struct node_t *victim = (*paths)->head;
        struct node_t *vdata = (*files)->head;
        char msg[BUF_BASESZ]; // buffer di dimensione fissa: dovrebbe avere una capacita sufficiente per i path
        while(victim) {
            snprintf(msg, BUF_BASESZ, "\"%s\" (%lu bytes)", (char*)victim->data, ((struct fs_filedata_t *)vdata->data)->size);
            if(logging(ds, 0, msg) == -1) {
                perror(msg);
            }
            victim = victim->next;
            vdata = vdata->next;
        }
    }
    else {
        errno = errno_saved; // ripristino errno
        if(logging(ds, errno, "[SERVER] Capacity miss: Errore nell'espulsione di file") == -1) {
            perror("[SERVER] Capacity miss: Errore nell'espulsione di file");
        }
    }

    return num_evicted; // ritorno al chiamante il numero di file espulsi dalla cache
}
