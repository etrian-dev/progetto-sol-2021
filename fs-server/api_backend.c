// header server
#include <server-utils.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
#include <icl_hash.h> // per hashtable
// header multithreading
#include <pthread.h>
// system call headers
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Costruisce una stringa contenente i num pathname in paths (str deve essere già allocata)
void build_pathstr(char **str, const char **paths, const int num) {
    size_t offt = 0; // offset nella stringa di paths
    int i;
    for(i = 0; i < num; i++) {
        size_t plen = strlen(paths[i]) + 1; // dimensione dell'i-esimo path
        *str = strncat(*str, paths[i], plen);
        // inserisco il separatore (non devo farlo per l'ultimo path: str termina con '\0')
        if(i < num - 1) {
            *str[offt + plen - 1] = '\n';
            *str[offt + plen] = '\0';
            offt += plen;
        }
    }
}

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
                reply = newreply(REPLY_NO, 0, NULL);
                success = -1;
            }
            // Se, invece, posso crearlo allora creo una nuova entry nella hashtable
            if(success == 0) {
                // inserisco un file vuoto (passando NULL come buffer) nella tabella
                if(insert_file(ds, pathname, NULL, 0, client_sock) == NULL) {
                    if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
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
                    if((reply = newreply(REPLY_YES, 0, NULL)) == NULL) {
                        // errore allocazione risposta
                        puts("errore alloc risposta"); // TODO: log
                    }
                    // aggiorno info client
                    if(update_client_op(ds, client_sock, OPEN_FILE, flags, pathname) == -1) {
                        // fallito aggiornamento stato client
                        if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                            perror("Fallito aggiornamento stato client");
                        }
                    }
                }
            }
        }
        // Se un file non è presente nel fileserver e non deve essere creato allora restituisco errore
        else {
            if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
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
        size_t i = 0;
        int isOpen = 0;
        while(!isOpen && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1;
            }
            i++;
        }
        if(isOpen) {
            // File già aperto da questo client: l'operazione di apertura fallisce
            if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
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
            int *newentry = realloc(file->openedBy, sizeof(int) * (file->nopened + 1));
            if(!newentry) {
                // fallita allocazione nuovo spazio per fd
                if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
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
                file->openedBy = newentry;
                file->nopened++;
                if((reply = newreply(REPLY_YES, 0, NULL)) == NULL) {
                    // errore allocazione risposta
                    puts("errore alloc risposta"); // TODO: log
                }
                // aggiorno info client
                if(update_client_op(ds, client_sock, OPEN_FILE, flags, pathname) == -1) {
                    // fallito aggiornamento stato client
                    if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                        perror("Fallito aggiornamento stato client");
                    }
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

// Chiude il file con path pathname (se presente) per il socket passato come parametro
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_closeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock) {
	// conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
    	// il file non è presente nel server (quindi non posso chiuderlo)
    	reply = newreply(REPLY_NO, 0, NULL);
    	success = -1;
    }
    else {
   		// file presente: devo controllare se il client che richiede la sua chiusura abbia aperto il file
   		size_t i = 0;
   		while(i < file->nopened && file->openedBy[i] != client_sock) {
   			i++;
   		}
   		if(i == file->nopened) {
   			// non risulta che il file fosse aperto da questo client
            // quando è stata inviata la richiesta di chiusura
   			// quindi l'operazione fallisce
   			reply = newreply(REPLY_NO, 0, NULL);
   			success = -1;
   		}
        else {
            // altrimenti il file era aperto: lo chiudo per questo socket
            file->openedBy[i] = -1;
            file->nopened--;
            // rialloco (restringo) l'array
            file->openedBy = realloc(file->openedBy, file->nopened * sizeof(int));
            // rispondo positivamente alla api
            reply = newreply(REPLY_YES, 0, NULL);
        }
   	}

   	if(reply) {
   		if(writen(client_sock, reply, sizeof(*reply)) == -1) {
   			// fallita scrittura sul socket
   			perror("Fallita scrittura sul socket");
   		}
   		free(reply);
   	}

   	return success;
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
        if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
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
            if((reply = newreply(REPLY_YES, 1, NULL)) == NULL) {
                // errore allocazione risposta
                puts("errore alloc risposta"); // TODO: log
            }
            // setto manualmente la dimensione del file al posto della lunghezza dei path
            reply->paths_sz = file->size;

            // aggiorno info client
            if(update_client_op(ds, client_sock, READ_FILE, 0, pathname) == -1) {
                // fallito aggiornamento stato client
                if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                    perror("Fallito aggiornamento stato client");
                }
            }
        }
        else {
            // Operazione negata: il client non aveva aperto il file
            if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
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
    }
    else {
        success = -1;
    }
    // Se la lettura è autorizzata allora invio il file sul socket
    if(success == 0 && reply->status == REPLY_YES) {
        writen(client_sock, file->data, file->size);
    }
    if(reply) {
        free(reply);
    }

    return success;
}

// Legge n file nel server (quelli meno recenti per come è implementata) e li invia al client
// Se n<=0 allora legge tutti i file presenti nel server
// Se ha successo ritorna il numero di file letti, -1 altrimenti
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

        // Alloco la risposta contenente il numero di file e la lunghezza dei path concatenati
        rep = newreply(REPLY_YES, num_sent, paths);
        if(!rep) {
            success = -1;
        }

        all_paths = calloc(rep->paths_sz, sizeof(char));
        if(!all_paths) {
            // errore alloc
            success = -1;
            // devo cambiare la risposta
            if(rep) {
                rep->status = REPLY_NO;
            }
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


        if(rep) {
            // scrivo la risposta
            if(writen(client_sock, rep, sizeof(struct reply_t)) != sizeof(struct reply_t)) {
                // errore di scrittura
                success = -1;
            }
            // scrivo le dimensioni dei file
            if(success == 0 && rep->status == REPLY_YES) {
                if(writen(client_sock, sizes, rep->nbuffers * sizeof(size_t)) != rep->nbuffers * sizeof(size_t)) {
                    success = -1;
                }
            }
            if(success == 0 && rep->status == REPLY_YES) {
                if(writen(client_sock, all_paths, rep->paths_sz) != rep->paths_sz) {
                    // errore di scrittura
                    success = -1;
                }
            }
            if(success == 0) {
                // Infine invio sul socket tutti i file richiesti
                for(i = 0; i < num_sent; i++) {
                    if(writen(client_sock, files[i]->data, files[i]->size) != files[i]->size) {
                        // errore di scrittura file
                        success = -1;
                        break;
                    }
                }
            }
            if(success == 0) {
                // aggiorno info client
                if(update_client_op(ds, client_sock, READ_N_FILES, 0, NULL) == -1) {
                    // fallito aggiornamento stato client
                    if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                        perror("Fallito aggiornamento stato client");
                    }
                }
            }
        }
    }

    // libero memoria
    if(files) free(files);
    if(sizes) free(sizes);
    if(paths) free(paths);
    if(all_paths) free(all_paths);
    if(rep) free(rep);

    if(success == -1) {
        return success;
    }
    return num_sent;
}

// Scrive in append al file con path pathname (se presente) il buffer buf di lunghezza size
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_appendToFile(struct fs_ds_t *ds, const char *pathname, const int client_sock, const size_t size, void *buf) {
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    int success = 0;

    // variabili usate nella logica di rimpiazzamento
    struct Queue *evicted = NULL;
    struct Queue *evicted_paths = NULL;
    size_t *sizes = NULL;
    char **paths = NULL;
    char *all_paths = NULL;

    int i;
    int isOpen = 0;
    // Cerco il file nella tabella
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(file == NULL) {
        // chiave non trovata => restituire errore alla API
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "api_appendToFile: file non trovato") == -1) {
            perror("api_appendToFile: file non trovato");
        }
        success = -1;
    }
    // file trovato: guardo se era stato aperto da questo client
    else {
        i = 0;
        isOpen = 0; // flag per determinare se il file era aperto
        while(!isOpen && i < file->nopened) {
            if(file->openedBy[i] == client_sock) {
                isOpen = 1; // aperto da questo client
            }
            i++;
        }
    }

    int nevicted = 0; // numero di file espulsi
    // Se è aperto da questo client allora posso modificarlo
    if(success == 0 && isOpen) {
        // Se la scrittura provoca capacity miss (per quantità di memoria) allora chiamo l'algoritmo di rimpiazzamento
        if(ds->curr_mem + size > ds->max_mem) {
            // i file (eventualmente) espulsi sono messi all'interno delle code
            if((nevicted = cache_miss(ds, ds->curr_mem + size, &evicted_paths, &evicted)) == -1) {
                // fallita l'espulsione dei file: l'operazione di apertura fallisce
                reply = newreply(REPLY_NO, 0, NULL);
                success = -1;
            }
            // Altrimenti l'espulsione ha avuto successo (e nevicted contiene il numero di file espulsi)
        }
    }
    // Operazione negata: il client non aveva aperto il file
    else {
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "api_appendToFile: il file non era aperto") == -1) {
            perror("api_appendToFile: il file non era aperto");
        }
        success = -1;
    }

    // Se il file era aperto allora lo modifico
    if(success == 0 && isOpen) {
        // espando l'area di memoria puntata dal campo dati del file per contenere buf
        void *newptr = NULL;
        if(!(file->data)) {
            newptr = malloc(size); // il file era vuoto (dopo una write)
        }
        else {
            newptr = realloc(file->data, file->size + size); // il file non era vuoto
        }

        if(!newptr) {
            // errore allocazione memoria
            reply = newreply(REPLY_NO, 0, NULL);
            if(logging(ds, 0, "api_appendToFile: errore nel concatenare i dati") == -1) {
                perror("api_appendToFile: errore nel concatenare i dati");
            }
            success = -1;
        }
        // Aggiorno il file con i nuovi dati (se l'allocazione precedente ha avuto successo
        else {
            file->data = newptr; // aggiorno il puntatore con il nuovo
            // copio buf in append
            memcpy(file->data + file->size, buf, size);
            // aggiorno la dimensione del file
            file->size += size;

            // devo aggiornare anche la quantità di memoria occupata nel server
            ds->curr_mem += size;
            // aggiorno se necessario anche la massima quantità di memoria occupata
            if(ds->curr_mem > ds->max_used_mem) {
                ds->max_used_mem = ds->curr_mem;
            }

            // Devo inviare eventuali files espulsi
            if(nevicted > 0) {
                struct node_t *str = evicted_paths->head; // coda dei path dei file espulsi
                struct node_t *fs = evicted->head; // coda dei dati dei file espulsi
                sizes = malloc(nevicted * sizeof(size_t));
                paths = malloc(nevicted * sizeof(char *));

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
                    if((reply = newreply(REPLY_YES, nevicted, paths)) == NULL) {
                        // fallita allocazione risposta: fallisce anche l'invio dei file
                        success = -1;
                    }
                    // devo quindi concatenare gli nevicted path dei file in all_paths e separarli con '\n'
                    if(success == 0) {
                        all_paths = malloc(reply->paths_sz);
                        if(!all_paths) {
                            // fallita allocazione
                            success = -1;
                        }
                    }
                }

                //
                if(success == 0) {
                    // assegno la stringa a all_paths
                    build_pathstr(&all_paths, paths, nevicted);

                    // aggiorno info client
                    if(update_client_op(ds, client_sock, APPEND_FILE, 0, pathname) == -1) {
                        // fallito aggiornamento stato client
                        if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                            perror("Fallito aggiornamento stato client");
                        }
                    }
                }
            }
            else {
                // file non rimpiazzati, ma è comunque necessario inviare la risposta alla API
                reply = newreply(REPLY_YES, 0, NULL);
            }
        }
    }

    // scrivo la risposta sul socket del client
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
            // fallita scrittura
            success = -1;
        }
        // scrivo le dimensioni dei file espulsi
        if(success == 0 && reply->status == REPLY_YES && reply->nbuffers > 0 && sizes) {
            if(writen(client_sock, sizes, reply->nbuffers * sizeof(size_t)) != reply->nbuffers * sizeof(size_t)) {
                // fallita scrittura
                success = -1;
            }
        }
        // scrivo i path dei file espulsi
        if(success == 0 && reply->status == REPLY_YES && reply->nbuffers > 0 && all_paths) {
            if(writen(client_sock, all_paths, reply->paths_sz) != reply->paths_sz) {
                // fallita scrittura
                success = -1;
            }
        }
        // scrivo i file espulsi
        if(success == 0 && reply->status == REPLY_YES && reply->nbuffers > 0 && evicted) {
            struct node_t *file = evicted->head;
            i = 0;
            while(file) {
                if(writen(client_sock, file, sizes[i]) != sizes[i]) {
                    // fallita scrittura
                    success = -1;
                    break;
                }
                file = file->next;
                i++;
            }
        }
        free(reply);
    }

    // posso quindi liberare gli array di supporto e le code
    if(evicted_paths) free_Queue(evicted_paths);
    if(evicted) free_Queue(evicted);
    if(sizes) free(sizes);
    if(paths) free(paths);
    if(all_paths) free(all_paths);

    return success;
}

// Se l'operazione precedente del client client_sock (completata con successo) era stata
// openFile(pathname, O_CREATEFILE) allora il file pathname viene troncato (ritorna a dimensione nulla)
// Se l'operazione ha successo ritorna 0, -1 altrimenti
int api_writeFile(struct fs_ds_t *ds, const char *pathname, const int client_sock) {
    int success = 0;
    // conterrà la risposta del server
    struct reply_t *reply = NULL;
    // controllo se l'ultima operazione di questo client (completata con successo)
    // sia stata una openFile(pathname, O_CREATEFILE)
    size_t pos = 0;
    while(  pos < ds->connected_clients
            && !(ds->active_clients[pos].socket == client_sock
                && ds->active_clients[pos].last_op == OPEN_FILE
                && ds->active_clients[pos].last_op_flags == O_CREATEFILE
                && strcmp(ds->active_clients[pos].last_op_path, pathname) == 0))
    { pos++; }
    if(pos == ds->connected_clients) {
        // il client non rispetta almeno una delle condizioni sopra, quindi l'operazione è negata
        reply = newreply(REPLY_NO, 0, NULL);
        success = -1;
    }

    if(success == 0) {
        // Cerco il file nella tabella
        struct fs_filedata_t *file = find_file(ds, pathname);
        if(file == NULL) {
            // chiave non trovata => restituire errore alla API
            if((reply = newreply(REPLY_NO, 0, NULL)) == NULL) {
                // errore allocazione risposta
                if(logging(ds, 0, "errore allocazione risposta") == -1) {
                    perror("errore allocazione risposta");
                }
            }
            if(logging(ds, 0, "api_writeFile: file non trovato") == -1) {
                perror("api_writeFile: file non trovato");
            }
            success = -1;
        }
        // file trovato: ne cancello il contenuto e resetto a zero la dimensione
        else {
            if(file->data) {
                free(file->data);
            }
            file->data = NULL;

            ds->curr_mem -= file->size; // devo aggiornare anche la quantità di memoria occupata
            file->size = 0;

            // operazione terminata con successo
            reply = newreply(REPLY_YES, 0, NULL);

            // aggiorno info client
            if(update_client_op(ds, client_sock, WRITE_FILE, 0, pathname) == -1) {
                // fallito aggiornamento stato client
                if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                    perror("Fallito aggiornamento stato client");
                }
            }
        }
    }

    // scrivo la risposta sul socket del client
    if(reply) {
        writen(client_sock, reply, sizeof(*reply));
        free(reply);
    }
    else {
        success = -1;
    }

    return success;
}
