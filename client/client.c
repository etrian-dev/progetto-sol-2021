// header client
#include <client.h>
// header API
#include <fs-api.h>
// header utilità
#include <utils.h>
// system call headers
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stddef.h>

// macro usata per eseguire i comandi cmd solo se la flag è diversa da 0
#define PRINT(flag, cmd) \
    if((flag)) {cmd}

// Funzione main del processo client
int main(int argc, char **argv) {
    // processing degli argomenti riceuti da riga di comando
    // riempe i campi della struttura se ha successo
    struct client_opts *options = malloc(sizeof(struct client_opts));
    memset(options, 0, sizeof(struct client_opts)); // azzero per sicurezza

    errno = 0; // resetto errno per distinguere tra errori da syscall a errori nelle opzioni
    if(get_client_options(argc, argv, options) == -1) {
        // errore nel processare la lista di argomenti
        if(errno != 0) {
            fprintf(stderr, "Errore %d: %s\n", errno, strerror(errno));
        }
        // altrimenti è un errore specifico di sintassi delle opzioni, non di syscall o simili
        else {
            fprintf(stderr, "Errore di sintassi delle opzioni\n");
        }
        return 1;
    }

    // stampo le opzioni riconosciute
    PRINT(options->prints_on,
        printf("Help: %s\n", (options->help_on ? "ON" : "OFF"));
        printf("Stdout prints: %s\n", (options->prints_on ? "ON" : "OFF"));
        printf("Delay: %ldms\n", options->rdelay);
        printf("Socket: %s\n", options->fs_socket);
        printf("Directory swapout: %s\n", options->dir_swapout);
        printf("Directory with files to write: %s\n", options->dir_write);
        printf("Directory save reads: %s\n", options->dir_save_reads);
    )
    // Se è stato richiesto il messaggio di help stampa quello e poi esce
    if(options->help_on) {
        printf(HELP_MSG, argv[0]);
    }
    else {
        // Il client tenta di connettersi al server, tentando ogni rdelay millisecondi
        // Dopo 5 secondi di tentativi falliti esce con errore e termina il client
        struct timespec fivesec;
        clock_gettime(CLOCK_REALTIME, &fivesec);
        fivesec.tv_sec += 5;
        if(openConnection(options->fs_socket,options->rdelay, fivesec) == -1) {
            perror("Impossible connettersi al server");
            free_client_opt(options);
            return 1;
        }
        PRINT(options->prints_on, printf("[%d] Connesso al server\n", getpid());)

        // Invio le richieste nell'ordine in cui sono state date al client
        struct node_t *req = NULL;
        struct node_t *filename = NULL;
        while((req = pop(options->oplist)) != NULL) {
            char oper = ((struct operation *)req)->type;
            while((filename = pop(((struct operation *)req)->flist)) != NULL) {
                switch(oper) {
                    case READ_FILE: { // per leggere il file prima deve essere aperto
                        if(openFile((char*)filename, 0) == -1) {
                            // errore di apertura: log su stderr
                            fprintf("[CLIENT %d]: Impossibile aprire il file \"%s\"\n", getpid(), (char*)filename);
                            perror("openFile");
                        }
                        // file aperto, ora lo leggo
                        void *buf = NULL;
                        size_t file_sz = 0;
                        if(readFile((char*)filename, &buf, &file_sz) == -1) {
                            // errore di lettura
                            fprintf("[CLIENT %d]: Impossibile leggere il file \"%s\"\n", getpid(), (char*)filename);
                            perror("readFile");
                        }
                        if(closeFile((char*)filename) == -1) {
                            // errore di chiusura: log su stderr
                            fprintf("[CLIENT %d]: Impossibile chiudere il file \"%s\"\n", getpid(), (char*)filename);
                            perror("closeFile");
                        }
                        break;
                    }
                    case

                }
            }
        }

        // La connessione con il server viene chiusa
        if(closeConnection(options->fs_socket) == -1) {
            perror("Impossible disconnettersi dal server");
            return 1;
        }
        PRINT(options->prints_on, printf("[%d] Disconnesso dal server\n", getpid());)
    }
    // libera la struttra dati contenente le opzioni del client
    free_client_opt(options);

    return 0;
}
