// header file della coda sincronizzata

#ifndef QUEUE_H_INCLUDED
#define QUEUE_H_INCLUDED

#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// mutex to ensure ME access to the queue
extern pthread_mutex_t mux;
// cond variable to signal new elements in the queue
extern pthread_cond_t new_request;

// This is the queue's node: it holds a pointer to the data (the data itself in this case)
struct node_t {
    void *data_ptr;
    struct node_t *next;
};
// The Queue struct is just a wrapper to pass head and tail pointers toghether
struct Queue {
    struct node_t *head;
    struct node_t *tail;
};

// Inizializzo la coda e le variabili di condizione e di lock
int init_queue(Queue **q);
// This operation (in ME) adds at the tail of the queue
// the element containing data of the specified size
// Returns 0 if it succeeds, -1 otherwise
int enqueue(struct node_t **head, struct node_t **tail, const void *data, size_t size);
// This operation (in ME) returns the head element in the queue, and removes it from it
// If the queue is empty or some error occurred, returns NULL.
// The node_t* returned must be then freed by the caller
struct node_t *pop(struct node_t **head, struct node_t **tail);

#endif
