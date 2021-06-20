// header server
#include <server-utils.h>
// multithreading headers
#include <pthread.h>
// system call headers
#include <unistd.h>
#include <signal.h>
// headers libreria standard
#include <stdio.h>
#include <errno.h>

void *term_thread(void *params) {
    struct fs_ds_t *ds = (struct fs_ds_t *)params;

    // preparo maschera per apettare segnali SIGHUP, SIGINT, SIGQUIT
    sigset_t mask_term;
    if( sigemptyset(&mask_term)
        || sigaddset(&mask_term, SIGHUP) != 0
        || sigaddset(&mask_term, SIGINT) != 0
        || sigaddset(&mask_term, SIGQUIT) != 0)
    {
        // Fallito settaggio della maschera
        perror("[SERVER] Fallito settaggio della maschera");
        return (void*)1;
    }

    // aspetto un segnale di terminazione con sigwait
    int signal = 0;
    if(sigwait(&mask_term, &signal) == 0) {
        int term = 0;
        // terminazione veloce: devono essere chiuse le connessioni esistenti
        if(signal == SIGINT || signal == SIGQUIT) {
            // per segnalare terminazione veloce scrivo la richiesta seguente sulla coda
            term = FAST_TERM;
            write(ds->termination[1], &term, sizeof(term));
        }
        // terminazione lenta: le connessioni esistenti rimangono aperte
        // fino alla loro chiusura da parte del client
        else if(signal == SIGHUP) {
            // per segnalare terminazione lenta scrivo 2 sulla pipe
            term = SLOW_TERM;
            ds->slow_term = 1;
            write(ds->termination[1], &term, sizeof(term));
        }
    }

    return (void *)0;
}
