/*
 *
 * Description:
 * Queue_Support.c contains supporting functions for Simulation_Main.c that mainly revolve around modifying lists/queues, as
 * well as creating nodes for the data structures as well as functions to check if the lists/queues are empty or not.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "Queue_Support.h"

//creates + returns Node pointer
Node* createNode(Task *task) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) {
        printf("Memory allocation failed!\n");
        exit(1);
    }
    newNode->task = task;
    newNode->next = NULL;
    return newNode;
}

void insertNodeAtEnd(LinkedList* list, Task *task) {
    Node* newNode = createNode(task);

    // If the list is empty
    if (list->front == NULL) {
        list->front = newNode;
        list->end = newNode;
    } else {
        list->end->next = newNode;
        list->end = newNode;
    }
    newNode->next = NULL;
    list->size++;
}

//removes Node from front of List
//returns Node or null if List was empty
Node* dequeue(LinkedList *list) {
    // Check if the list is empty
    if (list->front == NULL) {
        printf("List is empty\n");
        return NULL;
    }

    // Get the front node
    Node* removedNode = list->front;

    // Update the front pointer to the next node
    list->front = list->front->next;

    // If the list becomes empty after removing the node, update the end pointer as well
    if (list->front == NULL) {
        list->end = NULL;
    }

    // Detach the removed node from the list
    removedNode->next = NULL;

    list->size--;
    // Return the removed node
    return removedNode;
}

int queueEmpty(LinkedList *queue) {
    if(queue->front == NULL && queue->end == NULL) {
        return 1;
    }
    else {
        return 0;
    }
}

//returns:
// MLFQ empty -> 1
// MLFQ not empty -> 0
int mlfqEmpty(Mlfq *queue) {
    if(queueEmpty(queue->p1) && queueEmpty(queue->p2) && queueEmpty(queue->p3) && queueEmpty(queue->p4)) {
        return 1;
    }
    else {
        return 0;
    }
}


Task *pickNextTask(Mlfq *queue) {
    Node *removed;
    //p1 not empty
    if(!queueEmpty(queue->p1)) {
        removed = dequeue(queue->p1);
        return removed->task;
    }
        //p2 not empty
    else if(!queueEmpty(queue->p2)) {
        removed = dequeue(queue->p2);
        return removed->task;
    }
        //p3 not empty
    else if(!queueEmpty(queue->p3)) {
        removed = dequeue(queue->p3);
        return removed->task;
    }
        //p4 not empty
    else if(!queueEmpty(queue->p4)) {
        removed = dequeue(queue->p4);
        return removed->task;
    }
        //else MLFQ is empty
    else {
        return NULL;
    }
}






