// header client
#include <client.h>
// header API
#include <fs-api.h>
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
    printf("Help: %s\n", (options->help_on ? "ON" : "OFF"));
    printf("Stdout prints: %s\n", (options->prints_on ? "ON" : "OFF"));
    printf("Delay: %ld\n", options->rdelay);
    printf("Socket: %s\n", options->fs_socket);
    printf("Directory swapout: %s\n", options->dir_swapout);
    printf("Directory with files to write: %s\n", options->dir_write);
    printf("Max #file written from %s: %ld\n", options->dir_write, options->max_write);
    printf("Directory save reads: %s\n", options->dir_save_reads);
    printf("Max #file read: %ld\n", options->max_read);

    // Se è stato richiesto il messaggio di help stampa quello e poi esce
    if(options->help_on) {
        printf(HELP_MSG, argv[0]);
        return 0;
    }

    printf("Lista di file da scrivere\n");
    int i = 0;
    while(options->write_list[i]) {
        printf("%s, ", options->write_list[i]);
        i++;
    }
    printf("\nLista di file da leggere\n");
    i = 0;
    while(options->read_list[i]) {
        printf("%s, ", options->read_list[i]);
        i++;
    }



    // libera la struttra dati contenente le opzioni del client
    free_client_opt(options);

    return 0;
}
