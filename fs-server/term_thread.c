// header progetto
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
// headers libreria standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

void *term_thread(void *params) {
    struct term_params_t *term_params = (struct term_params_t *)params;

    // preparo maschera per ascoltare segnali SIGHUP, SIGINT, SIGQUIT
    sigset_t mask_term;
    if( sigemptyset(&mask_term)
        || sigaddset(&mask_term, SIGHUP) != 0
        || sigaddset(&mask_term, SIGINT) != 0
        || sigaddset(&mask_term, SIGQUIT) != 0)
    {
        // Fallito settaggio della maschera
        perror("Fallito settaggio della maschera");
        return (void*)1;
    }

    // aspetto segnale di terminazione con sigwait
    int signal = -1;
    if(sigwait(&mask_term, &signal) != 0) {
        // terminazione veloce: devono essere chiuse le connessioni esistenti
        if(signal == SIGINT || signal == SIGQUIT) {
            term_params->fast_term = 1;
        }
        // terminazione lenta: le connessioni esistenti rimangono aperte
        else if(signal == SIGHUP) {
            term_params->slow_term = 1;
        }
    }

    return (void *)0;
}
