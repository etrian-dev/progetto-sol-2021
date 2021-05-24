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
    struct term_params_t *tp = (struct term_params_t *)params;

    // preparo maschera per apettare segnali SIGHUP, SIGINT, SIGQUIT
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

    // aspetto un segnale di terminazione con sigwait
    int signal = -1;
    if(sigwait(&mask_term, &signal) != 0) {
    	// Prendo ME sulla struttura dati per la terminazione del server
    	if(pthread_mutex_lock(&(tp->mux_term)) == -1) {
            perror("Fallita acquisizione ME su terminazione");
            return (void*)1;
        }

        // terminazione veloce: devono essere chiuse le connessioni esistenti
        if(signal == SIGINT || signal == SIGQUIT) {
            tp->fast_term = 1;
        }
        // terminazione lenta: le connessioni esistenti rimangono aperte
        // fino alla loro chiusura da parte del client
        else if(signal == SIGHUP) {
            tp->slow_term = 1;
        }

        if(pthread_mutex_unlock(&(tp->mux_term)) == -1) {
            perror("Fallito rilascio ME su terminazione");
            return (void*)1;
        }
    }

    return (void *)0;
}
