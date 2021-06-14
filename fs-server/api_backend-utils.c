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
    struct fs_filedata_t *file = icl_hash_find(ds->fs_table, (void *)fname);
    return file;
}

// Inserisce nel fileserver il file path con contenuto buf, di dimensione size, proveniente dal socket client
// Se buf == NULL allora crea un file vuoto
// Se l'operazione fallisce ritorna NULL, altrimenti ritorna un puntatore al file creato/modificato
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

        pthread_mutex_lock(&(ds->mux_files)); // TODO: error checking

        // Incremento il numero di file aperti nel server
        ds->curr_files++;
        // Se necessario modifico il massimo numero di file aperti contemporaneamente
        // durante l'uptime del server
        if(ds->curr_files > ds->max_nfiles) {
            ds->max_nfiles = ds->curr_files;
        }

        pthread_mutex_unlock(&(ds->mux_files)); // TODO: error checking

        // Inserisco il pathname del file anche nella coda della cache
        if(pthread_mutex_lock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di lock
            free(key);
            free_file(file);
            return NULL;
        }

        int queued = -1;
        // non utilizzo il parametro socket, per cui lo posso settare ad un socket non valido (-1)
        queued = enqueue(ds->cache_q, key, strlen(path) + 1, -1);

        if(pthread_mutex_unlock(&(ds->mux_cacheq)) == -1) {
            // Fallita operazione di unlock
            queued = -1;
        }

        if(queued == -1) {
            // fallito inserimento in coda
            free(key);
            free_file(file);
            return NULL;
        }

        res = icl_hash_insert(ds->fs_table, key, file);
    }
    // Altrimenti devo aggiornare il file (in seguito ad una appendToFile)
    else {
        // recupero il file
        struct fs_filedata_t *oldfile = find_file(ds, path);
        if(oldfile) {
            pthread_mutex_lock(&(oldfile->mux_file));
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
            pthread_mutex_unlock(&(oldfile->mux_file));

            pthread_mutex_lock(&(ds->mux_mem));
            // devo aggiornare anche la quantità di memoria occupata nel server
            ds->curr_mem += size;
            // aggiorno se necessario anche la massima quantità di memoria occupata
            if(ds->curr_mem > ds->max_used_mem) {
                ds->max_used_mem = ds->curr_mem;
            }
            pthread_mutex_unlock(&(ds->mux_mem));
        }

        file = oldfile;
        res = oldfile;
    }

    // NOTA: non assicuro la consistenza della coda di file con la hash table, ma a patto di controllare
    // sempre il risultato di find_file non ho problemi per file nella coda che
    // non siano anche effettivamente nella hashtable
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
    // È possibile che il file possa non entrare nel quantitativo di memoria
    // assegnato al server all'avvio. In tal caso l'algoritmo fallisce (e qualunque operazione collegata)
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
    pthread_mutex_lock(&(ds->mux_mem));
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

        if(pthread_mutex_lock(&(fptr->mux_file)) == -1) {
            free(victim->data);
            free(victim);
            return -1;
        }

        // Notare che la memoria occupata dal file non viene liberata subito e mantengo un puntatore al file
        // Libero invece la memoria occupata dalla chiave, dato che non è più utile
        if((success = icl_hash_delete(ds->fs_table, victim->data, free, NULL)) != -1) {
            // rimozione OK, decremento numero file nel server e quantità di memoria occupata
            pthread_mutex_lock(&(ds->mux_files));
            ds->curr_files--;
            pthread_mutex_unlock(&(ds->mux_files));

            ds->curr_mem -= sz;
        }
        else {
            errno_saved = errno; // salvo errore
        }

        if(pthread_mutex_unlock(&(fptr->mux_file)) == -1) {
            success = -1;
        }
        if(success == -1) {
            free_Queue(*paths);
            free_Queue(*files);
            // Per mantenere la cache consistente è necessario reinserire i path estratti nella coda
            if(pthread_mutex_lock(&(ds->mux_cacheq)) == -1) {
                // Fallita operazione di lock
                return -1;
            }

            if(enqueue(ds->cache_q, victim->data, victim->data_sz, -1) == -1) {
                // Consistenza cache compromessa: terminare il server
                perror("Consistenza cache compromessa: terminazione del server");
                pthread_kill(pthread_self(), SIGKILL);
            }

            if(pthread_mutex_unlock(&(ds->mux_cacheq)) == -1) {
                // Fallita operazione di unlock
                return -1;
            }
            // Consistenza della cache ripristinata
            return -1;
        }

        // Aggiorno le code con il nuovo file espulso
        if(enqueue(*paths, victim->data, strlen((char*)victim->data) + 1, -1) == -1) {
            // fallito inserimento del path del file in coda
            errno_saved = errno;
            success = -1;
            break;
        }
        if(enqueue(*files, fptr, sizeof(struct fs_filedata_t), -1) == -1) {
            // fallito inserimento del file in coda
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
    pthread_mutex_unlock(&(ds->mux_mem));

    // aggiorno il numero di chiamate (che hanno avuto successo) dell'algorimto di rimpiazzamento
    if(success > 0) ds->cache_triggered++;
    if(logging(ds, 0, "Capacity miss: espulsi i seguenti file") == -1) {
        perror("Capacity miss: espulsi i seguenti file");
    }
    struct node_t *victim = (*paths)->head;
    struct node_t *vdata = (*files)->head;
    char msg[BUF_BASESZ];
    while(victim) {
        snprintf(msg, BUF_BASESZ, "\"%s\" di %lu bytes", (char*)victim->data, ((struct fs_filedata_t *)vdata->data)->size);
        if(logging(ds, 0, msg) == -1) {
            perror(msg);
        }
        victim = victim->next;
        vdata = vdata->next;
    }

    errno = errno_saved;

    if(num_evicted > 0) {
        ds->cache_triggered++;
    }

    return num_evicted;
}
