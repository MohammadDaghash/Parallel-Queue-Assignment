#include <stddef.h>
#include <stdbool.h>
#include <threads.h>
#include <stdlib.h>
#include "queue.h"  // Include the header file for function prototypes

// Define the structure for the queue node
typedef struct QueueNode {
    void* data;
    struct QueueNode* next;
} QueueNode;

// Define the structure for the CV queue node to order sleeping threads
typedef struct ConditionVariables {
    cnd_t cv;
    struct ConditionVariables* next;
} ConditionVariables;

// Define the structure for the queue
typedef struct Queue {
    QueueNode* front;               // Front node of the queue
    QueueNode* rear;                // Rear node of the queue
    ConditionVariables* cv_front;   // Front node of the condition variables queue
    ConditionVariables* cv_rear;    // Rear node of the condition variables queue
    size_t size;                    // Current number of items in the queue
    size_t visitedCount;            // Number of items that have been inserted and removed from the queue
    mtx_t lock;                     // Mutex for protecting concurrent access to the queue
    size_t waitingCount;            // Number of threads waiting for the queue to fill
} Queue;

// Declare a static queue
static Queue queue;

void initQueue(void) {
    queue.front = NULL;
    queue.rear = NULL;
    queue.cv_front = NULL;
    queue.cv_rear = NULL;
    queue.size = 0;
    queue.visitedCount = 0;
    queue.waitingCount = 0;
    mtx_init(&queue.lock, mtx_plain);
}

void destroyQueue(void) {
    mtx_lock(&queue.lock);

    // Free any remaining nodes in the queue
    QueueNode* current = queue.front;
    while (current != NULL) {
        QueueNode* next = current->next;
        free(current);
        current = next;
    }
    queue.front = NULL;
    queue.rear = NULL;
    queue.size = 0;
    mtx_unlock(&queue.lock);

    // Free any remaining nodes in the CV queue
    ConditionVariables* curr = queue.cv_front;
    while (curr != NULL) {
        ConditionVariables* next = curr->next;
        free(curr);
        curr = next;
    }
    queue.cv_front = NULL;
    queue.cv_rear = NULL;

    mtx_destroy(&queue.lock);
}

void enqueue(void* item) {
    QueueNode* newNode = (QueueNode*)malloc(sizeof(QueueNode));
    newNode->data = item;
    newNode->next = NULL;

    mtx_lock(&queue.lock);

    if (queue.rear == NULL) {
        // Empty queue, add the first node
        queue.front = newNode;
        queue.rear = newNode;
    } else {
        // Add the new node to the rear
        queue.rear->next = newNode;
        queue.rear = newNode;
    }

    queue.size++;

    ConditionVariables* cv = queue.cv_front;
    if (cv != NULL) {
        // Wake up a thread waiting on the queue
        queue.cv_front = cv->next;
	    if (cv->next == NULL) {
            queue.cv_rear = NULL;
	    }
        cnd_signal(&cv->cv);
    }

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);

    // Check if the queue is empty
    while (queue.front == NULL) {
        // No items in the queue, wait for it to become non-empty
        queue.waitingCount++;
        ConditionVariables* cv = (ConditionVariables*)malloc(sizeof(ConditionVariables));
	    cv->next = NULL;
	    cnd_init(&cv->cv);
	    if (queue.cv_front == NULL) {
	        queue.cv_front = cv;
	        queue.cv_rear = cv;
	    } else {
	        queue.cv_rear->next = cv;
	        queue.cv_rear = cv;
	    }
        cnd_wait(&cv->cv, &queue.lock);
        queue.waitingCount--;
        cnd_destroy(&cv->cv);
	    free(cv);
    }

    // Remove the front node from the queue
    QueueNode* dequeuedNode = queue.front;
    queue.front = queue.front->next;
    if (queue.front == NULL) {
        // Last node in the queue
        queue.rear = NULL;
    }

    void* dequeuedItem = dequeuedNode->data;
    free(dequeuedNode);
    queue.size--;
    queue.visitedCount++;

    mtx_unlock(&queue.lock);
    return dequeuedItem;
}

bool tryDequeue(void** item) {
    mtx_lock(&queue.lock);

    if (queue.front == NULL) {
        // Queue is empty
        mtx_unlock(&queue.lock);
        return false;
    }

    // Remove the front node from the queue
    QueueNode* dequeuedNode = queue.front;
    queue.front = queue.front->next;
    if (queue.front == NULL) {
        // Last node in the queue
        queue.rear = NULL;
    }

    void* dequeuedItem = dequeuedNode->data;
    free(dequeuedNode);
    queue.size--;
    queue.visitedCount++;

    mtx_unlock(&queue.lock);

    *item = dequeuedItem;
    return true;
}

size_t size(void) {
    // Return the current number of items in the queue
    return queue.size;
}

size_t waiting(void) {
    // Return the number of threads waiting for the queue to fill
    return queue.waitingCount;
}

size_t visited(void) {
    // Return the number of items that have been inserted and removed from the queue
    return queue.visitedCount;
}



