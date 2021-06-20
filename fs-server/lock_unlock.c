// header server
#include <server-utils.h>
// header multithreading
#include <pthread.h>
// header segnali
#include <signal.h>
// std lib headers
#include <stdio.h>

// Funzioni per lock/unlock con error checking

// Funzioni per tentare di fare lock/unlock e riportare l'errore prima di terminare il server se falliscono
// Se obj fosse NULL e la lock fallisce allora ho deallocato obj contenente la variabile di lock
// (questo avviene per i file rimossi su cui altri thread stavano aspettando di avere lock)
void LOCK_OR_KILL(struct fs_ds_t *ds, pthread_mutex_t *mutex, void *obj) {
    int e_code = 0;
    if((e_code = pthread_mutex_lock(mutex)) != 0) {
        if(obj) {
            if(logging((ds), e_code, "Lock fallita") == -1) {
                perror("Lock fallita");
            }
            free_serv_ds(ds);
            pthread_kill(pthread_self(), SIGKILL);
        }
    }
}
void UNLOCK_OR_KILL(struct fs_ds_t *ds, pthread_mutex_t *mutex) {
    int e_code = 0;
    if((e_code = pthread_mutex_unlock(mutex)) != 0) {
        if(logging((ds), e_code, "Unlock fallita") == -1) {
            perror("Unlock fallita");
        }
        free_serv_ds(ds);
        pthread_kill(pthread_self(), SIGKILL);
    }
}