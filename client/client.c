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

#define PRINT(flag, cmd) \
    if((flag)) { cmd }

// Funzione main del processo client
int main(int argc, char **argv) {
    // processing degli argomenti riceuti da riga di comando
    // riempe i campi della struttura se ha successo
    struct client_opts *options = malloc(sizeof(struct client_opts));
    memset(options, 0, sizeof(struct client_opts)); // azzero per sicurezza

    errno = 0; // resetto errno per distinguere tra errori da syscall a errori logici
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
        printf("Max #file written from %s: %ld\n", options->dir_write, options->max_write);
        printf("Directory save reads: %s\n", options->dir_save_reads);
        printf("Max #file read: %ld\n", options->max_read);)

    // Se è stato richiesto il messaggio di help stampa quello e poi esce
    if(options->help_on) {
        printf(HELP_MSG, argv[0]);
        return 0;
    }

    struct timespec fivesec;
    clock_gettime(CLOCK_REALTIME, &fivesec);
    fivesec.tv_sec += 5;
    // In primo luogo il client apre la connessione con il server
    if(openConnection(options->fs_socket,options->rdelay, fivesec) == -1) {
        perror("Impossible connettersi al server");
        return 1;
    }
    PRINT(options->prints_on, printf("[%d] Connesso al server\n", getpid());)

    // Apro i file richiesti in lettura o scrittura
    int i = 0;
    while(options->read_list[i]) {
        if(openFile(options->read_list[i], O_CREATE) == -1) {
            PRINT(options->prints_on, printf("Impossibile aprire il file %s\n", options->read_list[i]);)
        }
        else {
            PRINT(options->prints_on, printf("File %s aperto\n", options->read_list[i]);)
        }
        i++;
    }
    i = 0;
    char *file; size_t fsz;
    while(options->read_list[i]) {
        if(readFile(options->read_list[i], (void**)&file, &fsz) == -1) {
            PRINT(options->prints_on, printf("Impossibile leggere il file %s\n", options->read_list[i]);)
        }
        else {
            PRINT(options->prints_on,
                printf("File %s letto\n", options->read_list[i]);
                printf("size:%lu\nfile\n=======\n%s\n", fsz, file);)
            free(file);
            file = NULL;
        }
        i++;
    }

    // La connessione con il server viene chiusa
    if(closeConnection(options->fs_socket) == -1) {
        perror("Impossible disconnettersi dal server");
        return 1;
    }
    PRINT(options->prints_on, printf("[%d] Disconnesso dal server\n", getpid());)

    // libera la struttra dati contenente le opzioni del client
    free_client_opt(options);

    return 0;
}
