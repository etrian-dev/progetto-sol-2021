// header client
#include <utils.h>
#include <client.h>
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
            printf("Errore %d: %s\n", errno, strerror(errno));
        }
        // altrimenti è un errore specifico di sintassi delle opzioni, non di syscall o simili
    }

    // stampo le opzioni riconosciute
    printf("Help: %s\n", (options->help_on ? "ON" : "OFF"));
    printf("Stdout prints: %s\n", (options->prints_on ? "ON" : "OFF"));
    printf("Delay: %ld\n", options->rdelay);
    printf("Socket: %s\n", options->fs_socket);
    printf("Directory swapout: %s\n", options->dir_swapout);
    printf("Directory save reads: %s\n", options->dir_save_reads);
    printf("Directory for files to write: %s\n", options->dir_write);
    printf("Max #file read: %ld\n", options->max_read);

    free(options);

    return 0;
}
