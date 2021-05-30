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
                cache_miss(ds, NULL, 0); // In questo caso il file viene buttato via
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
        // Se un file non è presente nel fileserver e non deve essere creato allora restituisco errore
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

    // Invio la risposta al client lungo il socket
    if(writen(client_sock, reply, sizeof(struct reply_t)) < sizeof(struct reply_t)) {
        success = -1;
    }
    free(reply);

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

// Legge n file e li invia al client. Se n<=0 allora legge tutti i file presenti nel server
int api_readN(struct fs_ds_t *ds, const int n, const int client_sock) {
    int success = 0;

   // Utilizzo la coda della cache per avere i path dei file
   // Quindi leggo i file meno recenti nel server per rendere l'operazione efficiente (non è una lista doppia)
    int i = 0;
    struct node_t *curr = ds->cache_q->head;
    struct fs_filedata_t *file = NULL;
    while(i < n && curr) { // Se n<=0 allora leggo tutti i file perché i < n sempre vero se incremento i
        file = find_file(ds, (char*)curr->data);

        // Unione di client_sock al set di socket che hanno aperto questo file
        // per permetterne la lettura usando api_readFile
        file->nopened++;
        realloc(file->openedBy, file->nopened);
        file->openedBy[file->nopened - 1] = client_sock;

        if(api_readFile(ds, (char *)curr->data, client_sock) == -1) {
            if(log(ds, errno, "api_readN: Fallita lettura") == -1) {
                perror("api_readN: Fallita lettura");
            }
            success = -1;
            break; // al primo insuccesso nella lettura di un file esce
        }
        // ripristino lo stato originale del file
        file->nopened--;
        realloc(file->openedBy, file->nopened);

        curr = curr->next;
        i++;
    }
    if(success == 0) {
        if(log(ds, errno, "api_readN: Lettura OK") == -1) {
            perror("api_readN: Lettura OK");
        }
    }

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
            if(log(ds, 0, "api_appendToFile: file non trovato") == -1) {
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
                cache_miss(ds, swpdir, ds->curr_mem + size); // i file espulsi sono inviati in swpdir
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
                if((reply = newreply('N', 0)) == NULL) {
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

                // l'operazione ha avuto successo
                if((reply = newreply('Y', 0)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                if(log(ds, 0, "api_appendToFile: successo") == -1) {
                    perror("api_appendToFile: successo");
                }
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            if((reply = newreply('N', 0)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            if(log(ds, 0, "api_appendToFile: il file non era aperto") == -1) {
                    perror("api_appendToFile: il file non era aperto");
            }
            success = -1;
        }
    }

    // scrivo la risposta sul socket del client
    writen(client_sock, reply, sizeof(*reply));

    return success;
}
