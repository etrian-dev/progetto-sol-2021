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

// Apre il file con path pathname (se presente) per il client con le flag passate come parametro
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_openFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, int flags) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // file non trovato: se era stata specificata la flag di creazione lo creo
        if(flags & O_CREATEFILE) {
            // Se ho già raggiunto il massimo numero di file memorizzabili allora la open fallisce
            // perché sto tentando di creare un nuovo file
            if(ds->curr_files == ds->max_files) {
                reply = newreply(REPLY_NO, 0, NULL, NULL);
                success = -1;
            }
            // Se, invece, posso crearlo allora creo una nuova entry nella hashtable
            if(success == 0) {
                // inserisco un file vuoto (passando NULL come buffer) nella tabella
                if(insert_file(ds, pathname, NULL, 0, client_sock) == NULL) {
                    if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                        // errore allocazione risposta
                        puts("errore alloc risposta"); // TODO: log
                    }
                    if(logging(ds, 0, "openFile: impossibile creare il file") == -1) {
                        perror("openFile: impossibile creare il file");
                    }
                    success = -1;
                }
                else {
                    // Inserimento OK (num di file aperti aggiornato da insertFile)
                    if((reply = newreply(REPLY_YES, 0, NULL, NULL)) == NULL) {
                        // errore allocazione risposta
                        puts("errore alloc risposta"); // TODO: log
                    }
                    if(logging(ds, 0, "openFile: successo") == -1) {
                        perror("openFile: successo");
                    }
                }
            }
        }
        // Se un file non è presente nel fileserver e non deve essere creato allora restituisco errore
        else {
            if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            // log dell'evento
            if(logging(ds, 0, "openFile: file non trovato") == -1) {
                perror("openFile: file non trovato");
            }
            success = -1;
        }
    }
    else {
        // File trovato nella tabella: devo verificare se è stato già aperto da questo client
        size_t i;
        int isOpen = 0;
        while(!isOpen && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1;
            }
            i++;
        }
        if(isOpen) {
            // File già aperto da questo client: l'operazione di apertura fallisce
            if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(logging(ds, 0, "openFile: file già aperto dal client") == -1) {
                perror("openFile: file già aperto dal client");
            }
            success = -1;
        }
        else {
            // Il File non era aperto da questo client: lo apro
            // TODO: lock?
            int *newentry = realloc(file->openedBy, file->nopened + 1);
            if(!newentry) {
                // fallita allocazione nuovo spazio per fd
                if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(logging(ds, 0, "openFile: apertura file fallita") == -1) {
                    perror("openFile: apertura file fallita");
                }
                success = -1;

            }
            else {
                // Apertura OK
                newentry[file->nopened] = client_sock;
                file->nopened++;
                file->openedBy = newentry;
                if((reply = newreply(REPLY_YES, 0, NULL, NULL)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(logging(ds, 0, "openFile: successo") == -1) {
                    perror("openFile: successo");
                }
            }
        }
    }

    // Invio la risposta al client lungo il socket
    if(reply) {
        if(writen(client_sock, reply, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
            success = -1;
        }
        free(reply);
    }
    
    return success; // 0 se successo, -1 altrimenti
}

// Legge il file con path pathname (se presente) e lo invia al client client_sock
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella ht
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => ritorna errore
        if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
            // errore allocazione risposta
            puts("errore alloc risposta"); // TODO: log
        }
        if(logging(ds, 0, "readFile: file non trovato") == -1) {
            perror("readFile: file non trovato");
        }
        success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        // TODO: lock?
        int i, isOpen;
        i = isOpen = 0;
        while(!isOpen && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1; // aperto da questo client
            }
            i++;
        }
        // Se è stato aperto da questo client allora posso leggerlo
        if(isOpen) {
            // Nella risposta includo la dimensione del file da inviare, in modo da consentire al
            // client di preparare un buffer della dimensione giusta
            if((reply = newreply(REPLY_YES, 1, &file->size, NULL)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(logging(ds, 0, "readFile: successo") == -1) {
                perror("readFile: successo");
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(logging(ds, 0, "readFile: il file non è stato aperto") == -1) {
                perror("readFile: il file non è stato aperto");
            }
            success = -1;
        }
    }

    // Scrivo la risposta
    if(reply) {
        writen(client_sock, reply, sizeof(*reply));
        success = -1;
    }
    // Se la lettura è autorizzata allora invio il file sul socket
    if(success == 0) {
        size_t tot = file->size;
        int bytes;
        while(success != -1 && tot > 0) {
            if((bytes = writen(client_sock, file->data + file->size - tot, tot)) == -1) {
                perror("write error");
                success = -1;
            }
            else {
                tot -= bytes;
            }
        }
    }

    return success;
}

// Legge n file nel server (quelli meno recenti per come è implementata) e li invia al client
// Se n<=0 allora legge tutti i file presenti nel server
// Se ha successo ritorna 0, -1 altrimenti
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock) {
    int success = 0;

    // Utilizzo la coda di path per ottenere dei file da inviare. Di conseguenza invio
    // sempre al client i file meno recenti nel server (per una maggiore efficienza,
    // dato che è una lista concatenata con il solo forward pointer)

    // Determino il numero di file che invierò, in modo da renderlo noto alla API
    int num_sent = 0;
    struct node_t *curr = ds->cache_q->head; // l'indirizzo del nodo contenente il primo path
    while((n <= 0 || num_sent < n) && curr) {
        curr = curr->next;
        num_sent++;
    }

    // Alloco gli array contenenti rispettivamente:
    // puntatori ai file da inviare
    struct fs_filedata_t **files = malloc(num_sent * sizeof(struct fs_filedata_t *));
    // dimensioni dei suddetti file
    size_t *sizes = malloc(num_sent * sizeof(size_t));
    // path dei suddetti file
    char **paths = malloc(num_sent * sizeof(char*));
    
    char *all_paths = NULL; // conterrà tutti i path dei file concatenati
    
    struct reply_t *rep = NULL;
    
    if(!(files && sizes && paths)) {
        // una delle allocazioni fallita
        success = -1;
    }
    else {
        curr = ds->cache_q->head;
        int i;
        for(i = 0; i < num_sent; i++) {
            // Trovo il file indicato dal path in curr
            files[i] = find_file(ds, (char*)curr->data);
            // Di tale file devo avere la dimensione
            sizes[i] = files[i]->size;
            // E devo anche avere un puntatore al path
            paths[i] = (char*)curr->data;
            curr = curr->next;
        }
        
        // Alloco la risposta contenente il numero di file, la dimensione e la lunghezza dei path concatenati
        rep = newreply(REPLY_YES, num_sent, sizes, paths);
        if(!rep) {
            success = -1;
        }

        all_paths = calloc(rep->paths_sz, sizeof(char));
        if(!all_paths) {
            // errore alloc
            success = -1;
            // devo cambiare la risposta
            rep->status = REPLY_NO;
        }
        else {
            // devo quindi concatenare i num_sent path dei file in all_paths e separarli con '\n'
            size_t offt = 0;
            for(i = 0; i < num_sent; i++) {
                size_t plen = strlen(paths[i]) + 1;
                all_paths = strncat(all_paths, paths[i], plen);
                // inserisco il separatore (non devo farlo per l'ultimo path: all_paths termina con '\0')
                if(i < num_sent - 1) {
                    all_paths[offt + plen - 1] = '\n';
                    all_paths[offt + plen] = '\0';
                    offt += plen;
                }
            }
        }
        
        // scrivo la risposta e poi di seguito i path dei file ed i file stessi
        if(write(client_sock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
            // errore di scrittura
            success = -1;
        }
        if(rep) {
            if(write(client_sock, all_paths, rep->paths_sz) != rep->paths_sz) {
                // errore di scrittura
                success = -1;
            }
            // Infine invio sul socket tutti i file richiesti
            for(i = 0; i < num_sent; i++) {
                if(write(client_sock, files[i]->data, files[i]->size) == -1) {
                    // errore di scrittura file
                    success = -1;
                    break;
                }
            }
        }
    }
    
    // libero memoria
    if(files) free(files);
    if(sizes) free(sizes);
    if(paths) free(paths);
    if(all_paths) free(all_paths);
    if(rep->buflen) free(rep->buflen);
    if(rep) free(rep);
    
    return success;
}

// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const size_t size, char *buf) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;
    int nevicted = 0; // il numero di file espulsi
    struct Queue *evicted = NULL;
    struct Queue *evicted_paths = NULL;
    char *all_paths = NULL;
    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => restituire errore alla API
        if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
            // errore allocazione risposta
            if(logging(ds, 0, "api_appendToFile: file non trovato") == -1) {
                perror("api_appendToFile: file non trovato");
            }
        }
        success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        int i, isOpen;
        i = isOpen = 0;
        while(!isOpen && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1; // aperto da questo client
            }
            i++;
        }
        // Se è aperto da questo client allora posso modificarlo
        if(isOpen) {
            // Verifico se la scrittura provochi un capacity miss (per numero file o memoria)
            if(ds->curr_files == ds->max_files || ds->curr_mem + size > ds->max_mem) {
                // i file (eventualmente) espulsi sono messi all'interno delle code
                if((nevicted = cache_miss(ds, ds->curr_mem + size, &evicted_paths, &evicted)) == -1) {
                    // fallita l'espulsione dei file: l'operazione di apertura fallisce
                    reply = newreply(REPLY_NO, 0, NULL, NULL);
                    success = -1;
                }
                // Altrimenti l'espulsione ha avuto successo (e nevicted contiene il numero di file espulsi)
            }

            // espando l'area di memoria del file per contenere buf
            void *newptr = NULL;
            if(!(file->data)) {
                newptr = malloc(size); // il file era vuoto
            }
            else {
                newptr = realloc(file->data, file->size + size); // il file non era vuoto
            }
            if(!newptr) {
                // errore allocazione memoria
                if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                success = -1;
            }
            else { // Aggiorno il file con i nuovi dati
                file->data = newptr;
                // copio soltanto la porzione aggiuntiva di dati
                memcpy(file->data + file->size, buf, size);
                // aggiorno la size del file
                file->size += size;
                // posso liberare buf
                free(buf);

                // devo aggiornare anche la quantità di memoria occupata
                ds->curr_mem += size;
                // aggiorno se necessario anche la massima quantità di memoria occupata
                if(ds->curr_mem > ds->max_used_mem) {
                    ds->max_used_mem = ds->curr_mem;
                }

                // l'operazione ha avuto successo: devo inviare eventuali files espulsi
                if(nevicted > 0) {
                    struct node_t *str = evicted_paths->head;
                    struct node_t *fs = evicted->head;
                    size_t *sizes = malloc(nevicted * sizeof(size_t));
                    char **paths = malloc(nevicted * sizeof(char *));
                    if(!(sizes && paths)) {
                        // fallita allocazione
                        success = -1;
                    }
                    if(success == 0) {
                        // scrivo le dimensioni ed i puntatori ai path
                        while(str && fs) {
                            sizes[i] = ((struct fs_filedata_t*)fs->data)->size;
                            paths[i] = (char *)fs->data;
                            str = str->next;
                            fs = fs->next;
                        }
                        // utilizzando le dimensioni ed i path estratti costruisco la risposta
                        reply = newreply(REPLY_YES, nevicted, sizes, paths);
                        // devo quindi concatenare gli nevicted path dei file in all_paths e separarli con '\n'
                        char *all_paths = malloc(reply->paths_sz);
                        if(!all_paths) {
                            // fallita allocazione
                            success = -1;
                        }
                        
                        if(success == 0) {
                            size_t offt = 0;
                            for(i = 0; i < nevicted; i++) {
                                size_t plen = strlen(paths[i]) + 1;
                                all_paths = strncat(all_paths, paths[i], plen);
                                // inserisco il separatore (non devo farlo per l'ultimo path: all_paths termina con '\0')
                                if(i < nevicted - 1) {
                                    all_paths[offt + plen - 1] = '\n';
                                    all_paths[offt + plen] = '\0';
                                    offt += plen;
                                }
                            }
                            free(sizes);
                            free(paths);
                            // scrivo i file 
                            if(logging(ds, 0, "api_appendToFile: successo") == -1) {
                                perror("api_appendToFile: successo");
                            }
                        }
                    }
                }
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            if((reply = newreply(REPLY_NO, 0, NULL, NULL)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(logging(ds, 0, "api_appendToFile: il file non era aperto") == -1) {
                perror("api_appendToFile: il file non era aperto");
            }
            success = -1;
        }
    }

    // scrivo la risposta sul socket del client
    if(reply) {
        writen(client_sock, reply, sizeof(*reply));
    }
    if(all_paths) {
        writen(client_sock, all_paths, strlen(all_paths) + 1);
    }

    return success;
}

// Se l'operazione precedente del client client_sock (completata con successo) era stata
// openFile(pathname, O_CREATEFILE) allora il file pathname viene troncato (ritorna a dimensione nulla)
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_writeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock) {
    // TODO: scrivere la funzione e aggiungere alle strutture dati del server l'ultima operazione compiuta con successo da ciascun socket dei client
}
