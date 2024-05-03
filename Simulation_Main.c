/*
 * Description:
 * Simulation_Main.c is a simulation of a scheduler implementation that uses a multi-level feedback queue as the primary scheduler.
 * The program takes inputs for the number of worker cpu threads to run, a time in microseconds that represents how often
 * all the tasks inside the queue should be moved to the highest priority, as well as a file to read that contains lines
 * with tasks and lines with delays that tell the reading queue to wait for a set amount of time. In order to manage
 * concurrency issues, such as adding to specific lists/queues, the program uses a combination of pthread locks to ensure
 * the list/queue can only be added/removed from in one thread at a time, as well as conditional variables to signal when
 * to perform specific logic, thus synchronizing the threads. Once the simulation completes, the program prints interesting
 * information regarding Turn Around Time and Response Time.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "Queue_Support.h"

//locks
pthread_mutex_t addToDispatchQueueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t quantumMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t addToMLFQMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t completedTaskMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t readyToProcess = PTHREAD_MUTEX_INITIALIZER;

//conditions
pthread_cond_t nextShiftCon = PTHREAD_COND_INITIALIZER;        //next quantum shift
pthread_cond_t readyToProcessCon = PTHREAD_COND_INITIALIZER;

//condition variables
int quantumCondition = 0;

//global variables
int simulationComplete = 0;
int numberOfCPUs;
int timeS;
int fileDone = 0;
int delayWitnessed = 0;
int numTasks = 0;

//stats
int simulationDuration = 0;
int completedTasks = 0;

//queues
Mlfq *mlfq;
LinkedList *priority1;
LinkedList *priority2;
LinkedList *priority3;
LinkedList *priority4;
LinkedList *dispatchQueue;
LinkedList *completedTaskList;

//result variables
double *turnAroundTimes;
double *responseTimes;
int sizeOf0 = 0;
int sizeOf1 = 0;
int sizeOf2 = 0;
int sizeOf3 = 0;
double sumOfTurnAround0 = 0;
double sumOfTurnAround1 = 0;
double sumOfTurnAround2 = 0;
double sumOfTurnAround3 = 0;
double sumOfResponseTime0 = 0;
double sumOfResponseTime1 = 0;
double sumOfResponseTime2 = 0;
double sumOfResponseTime3 = 0;

//used for testing
time_t programStartTime;
void printTimeSinceStart() {
    time_t currentTime = time(NULL);
    double elapsedTime = difftime(currentTime, programStartTime);
    printf("Time since program start: %.2f seconds\n", elapsedTime);
}

//sleep for n microseconds
static void microsleep(unsigned int usecs)
{
    long seconds = usecs / USEC_PER_SEC;
    long nanos   = (usecs % USEC_PER_SEC) * NANOS_PER_USEC;
    struct timespec t = { .tv_sec = seconds, .tv_nsec = nanos };
    int ret;
    do
    {
        ret = nanosleep( &t, &t );
        // need to loop, `nanosleep` might return before sleeping
        // for the complete time (see `man nanosleep` for details)
    } while (ret == -1 && (t.tv_sec || t.tv_nsec));
}

//adds task to MLFQ based on task->currPriority attribute of Task
void addToMLFQ(Task *task) {

    if(task->currPriority == 1) {
        insertNodeAtEnd(priority1, task);
    }
    else if(task->currPriority == 2) {
        insertNodeAtEnd(priority2, task);
    }
    else if(task->currPriority == 3) {
        insertNodeAtEnd(priority3, task);
    }
    else{
        insertNodeAtEnd(priority4, task);
    }
}

//safely adds tasks to dispatchQueue
void addToDispatchQueue(Task *task) {
    pthread_mutex_lock(&addToDispatchQueueMutex);
    insertNodeAtEnd(dispatchQueue, task);
    pthread_mutex_unlock(&addToDispatchQueueMutex);
}

//add completed Task to completedList safely + set completedTime of Task
void addToCompletedList(Task *task) {
    pthread_mutex_lock(&completedTaskMutex);
    clock_gettime(CLOCK_REALTIME, &(task->completionTime));
    insertNodeAtEnd(completedTaskList, task);
    completedTasks++;
    pthread_mutex_unlock(&completedTaskMutex);

    printf("*****Task Completed for a total of: %d\n", completedTasks);
}

//process DELAY lines from input file
void processDelayLine(char *line) {
    int delay;
    sscanf(line, "DELAY %d", &delay);
    printf("Delay: %d\n", delay);

    //tasks start being processed after the delayWitnessed for first time
    delayWitnessed = 1;

    //wait for delay milliseconds
    microsleep(delay * 1000);
}

//process TASK lines from input file
Task* processTaskLine(char* line) {
    Task *newTask = (Task *)malloc(sizeof(Task));
    if (newTask == NULL) {
        perror("Memory allocation error");
        exit(1);
    }
    // All Tasks should be initialized with a time allotment of 200ms
    newTask->timeAllotment = TIME_ALLOTMENT;
    newTask->currPriority = 1;
    clock_gettime(CLOCK_REALTIME, &(newTask->creationTime));
    newTask->respondedTo = 0;



    // Parse data from the line and populate the Task struct
    sscanf(line, "%s %d %d %d", newTask->name, &newTask->type, &newTask->length, &newTask->odds);
    printf("NEW TASK READ FROM FILE: Task Name: %s, Type: %d, Length: %d, Odds: %d\n", newTask->name, newTask->type, newTask->length, newTask->odds);
    newTask->timeRemaining = newTask->length;

    return newTask; // Return the pointer to the newly created Task
}

//read file + call processDelayLine or processTaskLine based on the line type
void *readTasks(void* arg) {
    char* fileName = (char*)arg;
    FILE *tasks_file = fopen(fileName, "r");
    if (tasks_file == NULL) {
        perror("Error opening configuration file");
        exit(1);
    }
    char line[MAX_LINE_LENGTH];

    while (fgets(line, sizeof(line), tasks_file) != NULL) {
        if (strncmp(line, "DELAY", 5) == 0) {
            processDelayLine(line);
        }
        else {
            Task *currTask = processTaskLine(line);
            addToDispatchQueue(currTask);
            numTasks++;
        }
    }
    fclose(tasks_file);
    fileDone = 1;
    printf("File Done Reading!\n");

    return NULL;
}

//process a task
//if task is not completed, add back to dispatchQueue
//else add to completedList
void handleTask(Task *task) {
    int ioRoll;
    int ioTime;
    int cpuTime;

    //if this is the first time a task is being handled, set it's firstResponseTime + mark bool false
    if(task->respondedTo == 0) {
        clock_gettime(CLOCK_REALTIME, &(task->firstResponseTime));
        task->respondedTo = 1;
    }

    //bool
    int doIO = 1;
    int fullTimeSlice = 0;

    //roll 1-100 to see if IO happens or not
    ioRoll = rand() % 100 + 1;

    if (ioRoll > task->odds) {
        doIO = 0;   //Don't do IO
    }
    if(task->timeRemaining >= TIME_SLICE) {
        fullTimeSlice = 1;  //do full TIME_SLICE
    }

    if(!doIO) {
        if(fullTimeSlice) {
            microsleep(TIME_SLICE);
            task->timeRemaining = task->timeRemaining - TIME_SLICE;
            task->timeAllotment = task->timeAllotment - TIME_SLICE;
            printf("task %s ran for 50ms. Time remaining: %d\n", task->name, task->timeRemaining);
        }
        else {
            microsleep(task->timeRemaining);
            task->timeRemaining = 0;
            printf("task %s ran for %dms and is now done being processed. \n", task->name, task->timeRemaining);
        }
    }
    else {
        //roll 1-50 to see how long to do IO for
        ioTime = rand() % 50 + 1;

        if(ioTime >= task->timeRemaining || ioTime == TIME_SLICE) { //whole time slice is used for IO
            microsleep(ioTime);
            printf("task %s only performed IO.\n", task->name);
        }
        else {
            if(task->timeRemaining < TIME_SLICE) {
                cpuTime = task->timeRemaining - ioTime;
            }
            else {
                cpuTime = TIME_SLICE - ioTime;
            }
            microsleep(cpuTime);
            task->timeRemaining = task->timeRemaining - cpuTime;
            task->timeAllotment = task->timeAllotment - cpuTime;
            printf("task %s ran for %dms and spent the rest of it's time slice performing IO. Time remaining: %d\n", task->name, cpuTime, task->timeRemaining);
        }
    }

    if(task->timeRemaining > 0) {
        addToDispatchQueue(task);   //task not done, add to dispatchQueue
    }
    else {
        addToCompletedList(task);   //task done, add to completedList
    }

}

//used to return a timespec struct that represents the difference in time between 2 timespec structs
struct timespec diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

//calculate turnAroundTime + responseTime for completed tasks at the end of the simulation
void calculateStats() {
    Node *curr = completedTaskList->front;
    //allocate memory for result arrays for all 4 Task types
    turnAroundTimes = malloc(4 * sizeof(double));
    responseTimes = malloc(4 * sizeof(double));

    //loop through the completed list
    while(curr != NULL) {

        //calculate turnAroundTime + responseTime
        struct timespec turnAroundTimeStruct = diff(curr->task->creationTime, curr->task->completionTime);
        struct timespec responseTimeStruct = diff(curr->task->creationTime, curr->task->firstResponseTime);
        double turnAroundTime = turnAroundTimeStruct.tv_sec + turnAroundTimeStruct.tv_nsec / 1e6;
        double responseTime = responseTimeStruct.tv_sec + responseTimeStruct.tv_nsec / 1e6;

        //count instances of each Task type + add to the sum of turnAroundTime + responseTime for that Task type
        if(curr->task->type == 0) {
            sizeOf0++;
            sumOfTurnAround0 += turnAroundTime;
            sumOfResponseTime0 += responseTime;
        }
        if(curr->task->type == 1) {
            sizeOf1++;
            sumOfTurnAround1 += turnAroundTime;
            sumOfResponseTime1 += responseTime;
        }
        if(curr->task->type == 2) {
            sizeOf2++;
            sumOfTurnAround2 += turnAroundTime;
            sumOfResponseTime2 += responseTime;
        }
        if(curr->task->type == 3) {
            sizeOf3++;
            sumOfTurnAround3 += turnAroundTime;
            sumOfResponseTime3 += responseTime;
        }

        curr = curr->next;
    }

    //calculate average turnAroundTime + responseTime for each Task type
    //if there are no instances of a Task type, set average to 0
    if(sizeOf0 > 0) {
        turnAroundTimes[0] = sumOfTurnAround0 / sizeOf0;
        responseTimes[0] = sumOfResponseTime0 / sizeOf0;
    }
    else {
        turnAroundTimes[0] = 0;
        responseTimes[0] = 0;
    }
    if (sizeOf1 > 0) {
        turnAroundTimes[1] = sumOfTurnAround1 / sizeOf1;
        responseTimes[1] = sumOfResponseTime1 / sizeOf1;
    } else {
        turnAroundTimes[1] = 0;
        responseTimes[1] = 0;
    }

    if (sizeOf2 > 0) {
        turnAroundTimes[2] = sumOfTurnAround2 / sizeOf2;
        responseTimes[2] = sumOfResponseTime2 / sizeOf2;
    } else {
        turnAroundTimes[2] = 0;
        responseTimes[2] = 0 ;
    }

    if (sizeOf3 > 0) {
        turnAroundTimes[3] = sumOfTurnAround3 / sizeOf3;
        responseTimes[3] = sumOfResponseTime3 / sizeOf3;
    } else {
        turnAroundTimes[3] = 0;
        responseTimes[3] = 0;
    }

    //print results
    printf("Average turnaround time of type 0: %.0f usec\n", turnAroundTimes[0]);
    printf("Average turnaround time of type 1: %.0f usec\n", turnAroundTimes[1]);
    printf("Average turnaround time of type 2: %.0f usec\n", turnAroundTimes[2]);
    printf("Average turnaround time of type 3: %.0f usec\n", turnAroundTimes[3]);

    printf("Average response time time of type 0: %.0f usec\n", responseTimes[0]);
    printf("Average response time time of type 1: %.0f usec\n", responseTimes[1]);
    printf("Average response time time of type 2: %.0f usec\n", responseTimes[2]);
    printf("Average response time time of type 3: %.0f usec\n", responseTimes[3]);
}

void *cpuThread(void* arg) {
    int cpuThreadID = *((int *)arg);
    printf("CPU thread %d running.\n", cpuThreadID);

    while(!simulationComplete) {    //loop until simulation if completed

        //wait to be signaled that the next quantum shift has begun
        pthread_mutex_lock(&quantumMutex);
        while (!quantumCondition) {
            pthread_cond_wait(&nextShiftCon, &quantumMutex);
        }
        pthread_mutex_unlock(&quantumMutex);

        //wait to be signaled that it's time to begin processing Tasks
        pthread_mutex_lock(&readyToProcess);
        while(mlfqEmpty(mlfq)) {
            pthread_cond_wait(&readyToProcessCon, &readyToProcess);
        }
        pthread_mutex_unlock(&readyToProcess);

        if(simulationComplete) {
            return NULL;
        }

        //now we know tasks exist in MLFQ
        pthread_mutex_lock(&addToMLFQMutex); //lock mlfq before modifying
        Task *currTask = pickNextTask(mlfq);    //take next Task to process from MLFQ
        pthread_mutex_unlock(&addToMLFQMutex);

        //process Task
        if(currTask != NULL) {
            handleTask(currTask);   //process Task
        }

    }
    return NULL;
}


//all tasks in priority 2,3,4 moved to priority 1 with refreshed time allotment
//tasks in priority 1 also get their time allotment refreshed
void moveAllToHighestPriority() {
    printf("Moving all tasks back to highest priority.\n");

    //reset time allotment for tasks already in highest priority
    Node *curr = priority1->front;
    while (curr != NULL) {
        curr->task->timeAllotment = TIME_ALLOTMENT;
        curr = curr->next;
    }

    // Move tasks from lower priority queues to the highest priority queue
    for (int i = 2; i <= 4; ++i) {
        LinkedList *lowerQueue = NULL;
        switch (i) {
            case 2:
                lowerQueue = priority2;
                break;
            case 3:
                lowerQueue = priority3;
                break;
            case 4:
                lowerQueue = priority4;
                break;
        }

        while (lowerQueue->size > 0) {
            Task *task = dequeue(lowerQueue)->task;
            if(task != NULL) {
                task->currPriority = 1;
                task->timeAllotment = TIME_ALLOTMENT;
                addToMLFQ(task); // Add to the highest priority queue
            }
        }
    }
}


//initializes global queues at program start
void initializeQueues() {
    priority1 = malloc(sizeof(LinkedList));
    priority1->front = NULL;
    priority1->end = NULL;

    priority2 = malloc(sizeof(LinkedList));
    priority2->front = NULL;
    priority2->end = NULL;

    priority3 = malloc(sizeof(LinkedList));
    priority3->front = NULL;
    priority3->end = NULL;

    priority4 = malloc(sizeof(LinkedList));
    priority4->front = NULL;
    priority4->end = NULL;

    mlfq = malloc(sizeof(Mlfq));
    mlfq->p1 = priority1;
    mlfq->p2 = priority1;
    mlfq->p3 = priority1;
    mlfq->p4 = priority1;

    dispatchQueue = malloc(sizeof(LinkedList));
    dispatchQueue->front = NULL;
    dispatchQueue->end = NULL;

    completedTaskList = malloc(sizeof(LinkedList));
    completedTaskList->front = NULL;
    completedTaskList->end = NULL;
}

//move tasks in priority 1 queue down a priority level if they are out of time allotment
void adjustPriority1() {
    Node *curr = mlfq->p1->front;
    Node *prev = NULL;

    while (curr != NULL) {
        if (curr->task->timeAllotment <= 0) {
            // Store the next pointer before removing the current node
            Node *next = curr->next;

            // Remove the current node from priority 1 list
            if (prev == NULL) {
                // If the current node is the front of the list
                mlfq->p1->front = curr->next;
            } else {
                prev->next = curr->next;
            }

            // If the current node is the end of the list, update end pointer
            if (curr == mlfq->p1->end) {
                mlfq->p1->end = prev;
            }

            // Decrease the size of the priority 1 list
            mlfq->p1->size--;

            // Insert the removed node into priority 2 list
            insertNodeAtEnd(mlfq->p2, curr->task);

            // Update prev for the next iteration
            curr = next;
        } else {
            // Move to the next node
            prev = curr;
            curr = curr->next;
        }
    }
}

//move tasks in priority 2 queue down a priority level if they are out of time allotment
void adjustPriority2() {
    Node *curr = mlfq->p2->front;
    Node *prev = NULL;

    while (curr != NULL) {
        if (curr->task->timeAllotment <= 0) {
            // Store the next pointer before removing the current node
            Node *next = curr->next;

            // Remove the current node from priority 2 list
            if (prev == NULL) {
                // If the current node is the front of the list
                mlfq->p2->front = curr->next;
            } else {
                prev->next = curr->next;
            }

            // If the current node is the end of the list, update end pointer
            if (curr == mlfq->p2->end) {
                mlfq->p2->end = prev;
            }

            // Decrease the size of the priority 2 list
            mlfq->p2->size--;

            // Insert the removed node into priority 3 list
            insertNodeAtEnd(mlfq->p3, curr->task);

            // Update prev for the next iteration
            curr = next;
        } else {
            // Move to the next node
            prev = curr;
            curr = curr->next;
        }
    }
}

//move tasks in priority 3 queue down a priority level if they are out of time allotment
void adjustPriority3() {
    Node *curr = mlfq->p3->front;
    Node *prev = NULL;

    while (curr != NULL) {
        if (curr->task->timeAllotment <= 0) {
            // Store the next pointer before removing the current node
            Node *next = curr->next;

            // Remove the current node from priority 3 list
            if (prev == NULL) {
                // If the current node is the front of the list
                mlfq->p3->front = curr->next;
            } else {
                prev->next = curr->next;
            }

            // If the current node is the end of the list, update end pointer
            if (curr == mlfq->p3->end) {
                mlfq->p3->end = prev;
            }

            // Decrease the size of the priority 3 list
            mlfq->p3->size--;

            // Insert the removed node into priority 4 list
            insertNodeAtEnd(mlfq->p4, curr->task);

            // Update prev for the next iteration
            curr = next;
        } else {
            // Move to the next node
            prev = curr;
            curr = curr->next;
        }
    }
}

//if task is out of time allotment in the lowest priority, reset its time allotment
void adjustPriority4() {
    Node *curr = mlfq->p3->front;

    while(curr != NULL) {
        if(curr->task->timeAllotment <= 0) {
            curr->task->timeAllotment = TIME_ALLOTMENT;
        }
        curr = curr->next;
    }
}

//adjusts priorities in queues from bottom -> top queue
void handleAdjustPriority() {
    printf("Adjusting Priority Levels in MLFQ.\n");
    adjustPriority4();
    adjustPriority3();
    adjustPriority2();
    adjustPriority1();
}

//adjust priorityLevels + time allotments of Tasks in MLFQ
void handleRule4() {
    pthread_mutex_lock(&addToMLFQMutex);
    if(!mlfqEmpty(mlfq)) {
        handleAdjustPriority();
    }
    pthread_mutex_unlock(&addToMLFQMutex);
}

//move all Tasks to top priority if it's been time interval S
void handleRule5() {
    pthread_mutex_lock(&addToMLFQMutex);
    if(simulationDuration % timeS == 0) {
        moveAllToHighestPriority();
    }
    pthread_mutex_unlock(&addToMLFQMutex);
}


int main(int argc, char *argv[]) {
    //handle args
    if (argc != 4) {
        printf("Exactly 3 arguments are required.\n");
        return EXIT_FAILURE;
    }
    numberOfCPUs = atoi(argv[1]);
    timeS = atoi(argv[2]);
    char *fileName = strdup(argv[3]);
    printf("Number of CPUs: %d, S = %d, Filename: %s\n", numberOfCPUs, timeS, fileName);

    //init queues
    initializeQueues();
    programStartTime = time(NULL);

    pthread_t *cpuThreads;
    pthread_t readingThread;

    //request memory for the correct number of cpu threads
    cpuThreads = (pthread_t *) malloc(numberOfCPUs * sizeof(pthread_t));

    //create correct number of cpu threads
    int threadIDs[numberOfCPUs];
    for (int i = 0; i < numberOfCPUs; i++) {
        threadIDs[i] = i + 1;
        pthread_create(&cpuThreads[i], NULL, cpuThread, (void *) &threadIDs[i]);
    }

    //create reading thread
    pthread_create(&readingThread, NULL, readTasks, (void *) fileName);

    //wait till reading thread gets to the first delay to move on with simulation
    while (!delayWitnessed) {
        sleep(1);
    }


    //do dispatch/scheduler work while file is not done being read or not all tasks have been completed already
    while(!fileDone || completedTaskList->size < numTasks) {
        //need to move tasks from dispatchQueue to MLFQ
        //while dispatchQueue is not empty, transfer all tasks from dispatch queue to MLFQ
        int count = 0;
        while (!queueEmpty(dispatchQueue)) {
            pthread_mutex_lock(&addToDispatchQueueMutex);   //lock dispatchQueue
            Task *dequeued = dequeue(dispatchQueue)->task;
            pthread_mutex_unlock(&addToDispatchQueueMutex);
            if(dequeued != NULL) {
                count++;
                pthread_mutex_lock(&addToMLFQMutex);    //lock MLFQ
                addToMLFQ(dequeued);        //transfer task to MLFQ
                pthread_mutex_unlock(&addToMLFQMutex);
                printf("%d: added task %s to MLFQ at level: %d with %d timing remaining.\n", count, dequeued->name,dequeued->currPriority, dequeued->timeRemaining);
            }
        }


        //while MLFQ not empty, signal to cpu threads to start next shift and process MLFQ
        while (!mlfqEmpty(mlfq)) {
            //wake up cpu threads
            quantumCondition = 1;
            pthread_mutex_lock(&quantumMutex);
            pthread_cond_broadcast(&nextShiftCon);
            pthread_mutex_unlock(&quantumMutex);

            pthread_mutex_lock(&readyToProcess);
            printTimeSinceStart();
            pthread_cond_broadcast(&readyToProcessCon);
            pthread_mutex_unlock(&readyToProcess);


            //sleep for quantum time
            simulationDuration += TIME_SLICE;
            microsleep(TIME_SLICE);
            quantumCondition = 0;
            //check MLFQ rules

            //if were at a multiple of S, reset all tasks to the highest level in MLFQ
            handleRule5();

            //adjust Tasks in MLFQ
            //move down in Priority/reset Time allotment if necessary
            handleRule4();

        }
    }

    //tell CPU threads to quit
    for (int j = 0; j < numberOfCPUs; j++) {
        printf("cancel thread %d\n", j+1);
        pthread_cancel(cpuThreads[j]);
    }

    //produce simulation stats
    simulationComplete = 1;
    printf("Number of CPUs: %d, S = %d, Filename: %s\n", numberOfCPUs, timeS, fileName);
    calculateStats();
    printf("number of tasks completed: %d\n", completedTaskList->size);

    printTimeSinceStart();

    printf("Simulation Complete.\n");
    return 0;
}
