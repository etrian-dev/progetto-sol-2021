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
        if(flags & O_CREATE) {
            // se necessario viene espulso al più un file dalla cache (perchè non posso avere capacity misses)
            if(ds->curr_files == ds->max_files) {
                cache_miss(ds, NULL, 0); // In questo caso viene buttato
            }

            // inserisco un file vuoto (passando NULL come buffer) nella tabella
            if(insert_file(ds, pathname, NULL, 0, client_sock) == NULL) {
                if((reply = newreply('N', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(log(ds, 0, "openFile: impossibile creare il file") == -1) {
                    perror("openFile: impossibile creare il file");
                }
                success = -1;
            }
            else {
                // Inserimento OK (num di file aperti aggiornato da insertFile)
                if((reply = newreply('Y', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
            }
        }
        // Altrimenti restituisco un errore
        else {
            if((reply = newreply('N', 0)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            // log dell'evento
            if(log(ds, 0, "openFile: file non trovato") == -1) {
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
            if((reply = newreply('N', 0)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(log(ds, 0, "openFile: file già aperto dal client") == -1) {
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
                if((reply = newreply('N', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(log(ds, 0, "openFile: apertura file fallita") == -1) {
                    perror("openFile: apertura file fallita");
                }
                success = -1;

            }
            else {
                // Apertura OK
                newentry[file->nopened] = client_sock;
                file->nopened++;
                file->openedBy = newentry;
                if((reply = newreply('Y', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(log(ds, 0, "openFile: successo") == -1) {
                    perror("openFile: successo");
                }
            }
        }
    }

    if(writen(client_sock, reply, sizeof(struct reply_t)) < sizeof(struct reply_t)) {
        success = -1;
    }
    free(reply);

    return success;
}

// Legge il file con path pathname (se presente) per il client con le flag passate come parametro
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_readFile(struct fs_ds_t *ds, const char *pathname, const int client_sock) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => restituire errore alla API
        if((reply = newreply('N', 0)) == NULL) {
            // errore allocazione risposta
            puts("errore alloc risposta"); // TODO: log
        }
        if(log(ds, 0, "readFile: file non trovato") == -1) {
                perror("readFile: file non trovato");
        }
        success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        // if(lockedBy != -1 && lockedBy != client_sock) {
            // il file è lockato, ma non da questo client, quindi l'operazione fallisce
            // TODO: reply error
            // TODO: log fallimento
            //;
        // }
        int i, isOpen, nexit;
        i = isOpen = 0;
        nexit = 1;
        while(nexit && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1; // aperto da questo client
                nexit = 0;
            }
            i++;
        }
        // Se è aperto da questo client allora posso leggerlo
        if(isOpen) {
            // Ok, posso inviare il file al client lungo il socket
            if((reply = newreply('Y', file->size)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(log(ds, 0, "readFile: successo") == -1) {
                perror("readFile: successo");
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            if((reply = newreply('N', 0)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(log(ds, 0, "readFile: il file non è stato aperto") == -1) {
                perror("readFile: il file non è stato aperto");
            }
            success = -1;
        }
    }

    // Scrivo la risposta (header)
    writen(client_sock, reply, sizeof(*reply));
    // Se la lettura è autorizzata allora invio il file sul socket
    if(success == 0) {
        if(writen(client_sock, file->data, file->size) == -1) {
            perror("This error");
            success = -1;
        }
    }

    return success;
}

// Legge n file e li salva in dirname. Se n<=0 allora legge tutti i file presenti nel server
int api_readN(struct fs_ds_t *ds, const int n, const char *dirname, const int client_sock) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

   // TODO: implementare distinguendo(?) due casi e icl_hash_foreach(?)

    return success;
}

// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const size_t size, char *buf, const char *swpdir) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => restituire errore alla API
        if((reply = newreply('N', 0)) == NULL) {
            // errore allocazione risposta
            if(log(ds, 0, "appendToFile: file non trovato") == -1) {
                perror("appendToFile: file non trovato");
            }
        }
        success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        // if(lockedBy != -1 && lockedBy != client_sock) {
            // il file è lockato, ma non da questo client, quindi l'operazione fallisce
            // TODO: reply error
            // TODO: log fallimento
            //;
        // }
        int i, isOpen, nexit;
        i = isOpen = 0;
        nexit = 1;
        while(nexit && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1; // aperto da questo client
                nexit = 0;
            }
            i++;
        }
        // Se è aperto da questo client allora posso modificarlo
        if(isOpen) {
            // Verifico se provoca miss per numero di file o capacità: se sì effettuo swapout
            if(ds->curr_files == ds->max_files || ds->curr_mem + size > ds->max_mem) {
                cache_miss(ds, swpdir, ds->curr_mem + size);
            }

            // espando l'area di memoria del file per contenere buf
            void *newptr = NULL;
            if(!(file->data)) {
                newptr = malloc(size);
            }
            else {
                newptr = realloc(file->data, file->size + size);
            }
            if(!newptr) {
                // errore nella realloc
                if((reply = newreply('N', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                success = -1;
            }
            else {
                file->data = newptr;
                // riallocazione OK, concateno buf
                memcpy(file->data + file->size, buf, size);
                // aggiorno la size del file
                file->size += size;
                // posso liberare buf
                free(buf);

                // l'operazione ha avuto successo
                if((reply = newreply('Y', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(log(ds, 0, "appendToFile: successo") == -1) {
                    perror("appendToFile: successo");
                }

                // devo aggiornare anche la quantità di memoria occupata
                ds->curr_mem += size;
                // aggiorno se necessario anche il max
                if(ds->curr_mem > ds->max_used_mem) {
                    ds->max_used_mem = ds->curr_mem;
                }
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            if((reply = newreply('N', 0)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(log(ds, 0, "appendToFile: il file non era aperto") == -1) {
                    perror("appendToFile: il file non era aperto");
            }
            success = -1;
        }
    }

    writen(client_sock, reply, sizeof(*reply));

    return success;
}
