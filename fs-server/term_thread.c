/**
 * \file term_thread.c
 * \brief File contenente l'implementazione della funzione eseguita dal thread che gestisce la terminazione del server
 *
 * Il thread manager nel server crea un thread apposito per gestire la terminazione.
 * Tale thread ha il compito di ricevere i segnali dall'ambiente di esecuzione e reagire
 * in modo appropriato
 */

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

/**
 * \brief Funzione eseguita dal thread che gestisce la terminazione
 *
 * Questa funzione si occupa di gestire la terminazione del fileserver catturando tre segnali:
 * - Terminazione lenta
 *  - SIGHUP (1)
 * - Terminazione veloce
 *  - SIGINT (2)
 *  - SIGQUIT (3)
 *
 * Il segnale di terminazione lenta provoca soltanto la chiusura del socket su cui vengono
 * accettate nuove connessioni, per cui il server terminerà soltanto quando tutti i client
 * già connessi effettueranno la disconnessione. Invece, se viene ricevuto uno dei due segnali
 * per la terminazione veloce, sono inoltre disconnessi immediatamente tutti i client
 * ed il server termina appena possibile. Per ricevere i segnali sopracitati viene usata
 * la syscall sigwait (man 2 sigwait)
 * \param [in] params Un puntatore generico che è in realtà un puntatore alla struttura dati del server (fs_ds_t)
 * \return Ritorna 0 se il thread termina normalmente (dopo la ricezione di uno dei segnali gestiti), -1 altrimenti
 */
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
        return (void*)-1;
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
