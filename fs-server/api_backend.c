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

    // Se il file non è presente ed è stata passata la flag per la creazione allora lo creo
    if(!file && (flags & O_CREATEFILE)) {
        // Se ho già raggiunto il massimo numero di file memorizzabili allora la open fallisce
        // perché sto tentando di creare un nuovo file
        if(ds->curr_files == ds->max_files) {
            reply = newreply(REPLY_NO, 0, NULL);
            success = -1;
        }
        // Altrimenti posso crearlo
        if(success == 0) {
            // inserisco un file vuoto (passando NULL come buffer) nella tabella
            // e le flags (solo O_LOCKFILE rilevante per settare lock)
            if(insert_file(ds, pathname, flags, NULL, 0, client_sock) == NULL) {
                reply = newreply(REPLY_NO, 0, NULL);
                if(logging(ds, 0, "openFile: impossibile creare il file") == -1) {
                    perror("openFile: impossibile creare il file");
                }
                success = -1;
            }
            else {
                // Inserimento OK (num di file aperti aggiornato da insertFile)
                reply = newreply(REPLY_YES, 0, NULL);
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
    // Se il file non è presente nel server e non è stata passata la flag per la creazione
    // oppure se il file è presente ed è stata passata la flag per la creazione
    // oppure se il file esiste ed è lockato da qualche altro client
    // allora l'operazione di apertura/creazione fallisce
    else if(    (!file && !(flags & O_CREATEFILE))
                || (file && (flags & O_CREATEFILE))
                || (file->lockedBy != -1))
    {
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "openFile: impossibile aprire/creare il file") == -1) {
            perror("openFile: impossibile aprire/creare il file");
        }
        success = -1;
    }
    // File trovato nella tabella: provo ad aprirlo
    else {
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
            reply = newreply(REPLY_NO, 0, NULL);
            if(logging(ds, 0, "openFile: file già aperto dal client") == -1) {
                perror("openFile: file già aperto dal client");
            }
            success = -1;
        }
        else {
            // Il File non era aperto da questo client: lo apro
            pthread_mutex_lock(&(file->mux_file));

            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            int *newentry = realloc(file->openedBy, sizeof(int) * (file->nopened + 1));
            if(!newentry) {
                // fallita allocazione nuovo spazio per fd
                reply = newreply(REPLY_NO, 0, NULL);
                if(logging(ds, 0, "openFile: apertura file non riuscita") == -1) {
                    perror("openFile: apertura file non riuscita");
                }
                success = -1;

            }
            else {
                // Apertura OK: setto il file come aperto dal socket
                file->openedBy = newentry;
                file->openedBy[file->nopened] = client_sock;
                file->nopened++;
                // Se necessario setto lock (non è in stato locked per nessun altro client)
                if(flags & O_LOCKFILE) {
                    file->lockedBy = client_sock;
                }

                reply = newreply(REPLY_YES, 0, NULL);

                // aggiorno info client
                if(update_client_op(ds, client_sock, OPEN_FILE, flags, pathname) == -1) {
                    // fallito aggiornamento stato client
                    if(logging(ds, 0, "Fallito aggiornamento stato client") == -1) {
                        perror("Fallito aggiornamento stato client");
                    }
                }
            }

            file->modifying = 0;
            pthread_cond_signal(&(file->mod_completed));
            pthread_mutex_unlock(&(file->mux_file));
        }
    }

    // Invio la risposta al client lungo il socket
    if(reply) {
        if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
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
    if(!file) {
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
   			// il client non aveva aperto questo file, quindi l'operazione fallisce
   			reply = newreply(REPLY_NO, 0, NULL);
   			success = -1;
   		}
        else {
            // altrimenti il file era aperto: lo chiudo per questo socket
            pthread_mutex_lock(&(file->mux_file));

            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            for(; i < file->nopened - 1; i++) {
                file->openedBy[i] = file->openedBy[i+1];
            }
            file->nopened--;
            // rialloco (restringo) l'array e se non è più aperto da alcun client lo dealloco
            if(file->nopened == 0) {
                free(file->openedBy);
                file->openedBy = NULL;
            }
            else {
                file->openedBy = realloc(file->openedBy, file->nopened * sizeof(int));
            }
            // rispondo positivamente alla api
            reply = newreply(REPLY_YES, 0, NULL);

            file->modifying = 0;
            pthread_cond_signal(&(file->mod_completed));
            pthread_mutex_unlock(&(file->mux_file));
        }
   	}

   	if(reply) {
   		if(writen(client_sock, reply, sizeof(*reply)) != sizeof(*reply)) {
   			// fallita scrittura sul socket
   			perror("Fallita scrittura sul socket");
            success = -1;
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
        return -1;
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
        // file non trovato
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "api_appendToFile: file non trovato") == -1) {
            perror("api_appendToFile: file non trovato");
        }
        success = -1;
    }
    // file trovato: se era lockato da un altro client allora non posso modificarlo
    else if(file->lockedBy != -1 && file->lockedBy != client_sock) {
        reply = newreply(REPLY_NO, 0, NULL);
        if(logging(ds, 0, "api_appendToFile: file lockato da un altro client") == -1) {
            perror("api_appendToFile: file lockato da un altro client");
        }
        success = -1;
    }
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
    // Se è aperto da questo client e non è lockato allora è possibile modificarlo
    if(success == 0 && isOpen) {
        // Se la scrittura provoca capacity miss (per quantità di memoria) allora chiamo l'algoritmo di rimpiazzamento
        if(ds->curr_mem + size > ds->max_mem) {
            // i file (eventualmente) espulsi sono messi all'interno delle code
            if((nevicted = cache_miss(ds, size, &evicted_paths, &evicted)) == -1) {
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
        if(insert_file(ds, pathname, 0, buf, size, client_sock)) {
            // L'inserimento ha avuto successo
            // Devo inviare eventuali files espulsi al client
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
    else {
        // Aggiornamento del file fallito
        reply = newreply(REPLY_NO, 0, NULL);
        success = -1;
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
    // è stata una openFile(pathname, O_CREATEFILE)
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
            reply = newreply(REPLY_NO, 0, NULL);
            if(logging(ds, 0, "api_writeFile: file non trovato") == -1) {
                perror("api_writeFile: file non trovato");
            }
            success = -1;
        }
        // file trovato: ne cancello il contenuto e resetto a zero la dimensione
        else {
            pthread_mutex_lock(&(file->mux_file));
            while(file->modifying == 1) {
                pthread_cond_wait(&(file->mod_completed), &(file->mux_file));
            }
            file->modifying = 1;

            if(file->data) {
                free(file->data);
            }
            file->data = NULL;
            size_t save_sz = file->size;
            file->size = 0;

            file->modifying = 0;
            pthread_mutex_unlock(&(file->mux_file));

            pthread_mutex_lock(&(ds->mux_mem));
            ds->curr_mem -= save_sz; // devo aggiornare anche la quantità di memoria occupata
            pthread_mutex_unlock(&(ds->mux_mem));

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

// Assegna, se possibile, la mutua esclusione sul file con path pathname al client client_sock
// Ritorna 0 se ha successo, -1 altrimenti
int api_lockFile(struct fs_ds_t*ds, const char *pathname, const int client_sock) {
    // La risposta del server
    struct reply_t *rep = NULL;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file) {
        // file non trovato
        rep = newreply(REPLY_NO, 0, 0);
    }
    else {
        // Se era lockato da questo client o era libero setto lock
        if(file->lockedBy == -1 || file->lockedBy == client_sock) {
            file->lockedBy = client_sock;
        }

        rep = newreply(REPLY_YES, 0, 0);
    }

    if(!rep) {
        // Se non posso inviare la risposta allora non setto neanche la lock
        file->lockedBy = -1;
        return -1;
    }
    else {
        update_client_op(ds, client_sock, LOCK_FILE, O_LOCKFILE, pathname);
        if(writen(client_sock, rep, sizeof(*rep) != sizeof(*rep))) {
            free(rep);
            return -1;
        }
        free(rep);
    }

    return 0;
}
// Toglie la mutua esclusione sul file pathname (solo se era lockato da client_sock)
// Ritorna 0 se ha successo, -1 altrimenti
int api_unlockFile(struct fs_ds_t *ds, const char *pathname, const int client_sock) {
    // La risposta del server
    struct reply_t *rep = NULL;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file || (file->lockedBy != -1 && file->lockedBy != client_sock)) {
        // file non trovato o lockato da qualche altro client
        rep = newreply(REPLY_NO, 0, 0);
    }
    else {
        // Se il file era lockato da questo client allora rimuovo la lock
        pthread_mutex_lock(&(file->mux_file));
        file->lockedBy = -1;
        pthread_mutex_lock(&(file->mux_file));

        rep = newreply(REPLY_YES, 0, 0);
        if(!rep) {
            // Se non posso inviare la risposta ripristino la lock
            pthread_mutex_lock(&(file->mux_file));
            file->lockedBy = client_sock;
            pthread_mutex_lock(&(file->mux_file));
            return -1;
        }
    }

    if(rep) {
        if(writen(client_sock, rep, sizeof(*rep) != sizeof(*rep))) {
            free(rep);
            return -1;
        }
        free(rep);
    }

    return 0;
}

// Rimuove dal server il file con path pathname, se presente e lockato da client_sock
// Ritorna 0 se ha successo, -1 altrimenti
int api_rmFile(struct fs_ds_t*ds, const char *pathname, const int client_sock) {
    // La risposta del server
    struct reply_t *rep = NULL;

    // Cerco il file nel fileserver
    struct fs_filedata_t *file = find_file(ds, pathname);
    if(!file || file->lockedBy != client_sock) {
        // file non trovato o non lockato da questo client
        rep = newreply(REPLY_NO, 0, 0);
    }
    else {
        // Se il file era lockato da questo client allora lo rimuovo
        pthread_mutex_lock(&(file->mux_file));
        int result = icl_hash_delete(ds->fs_table, pathname, free, free_file);
        pthread_mutex_unlock(&(file->mux_file));

        if(result == 0) {
            rep = newreply(REPLY_YES, 0, 0);
        }
        else {
            rep = newreply(REPLY_NO, 0, 0);
        }
    }

    if(!rep) {
        return -1;
    }
    else {
        if(writen(client_sock, rep, sizeof(*rep) != sizeof(*rep))) {
            free(rep);
            return -1;
        }
        free(rep);
    }

    return 0;
}
