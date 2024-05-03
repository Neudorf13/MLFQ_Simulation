#define MAX_LINE_LENGTH 100
#define NANOS_PER_USEC 1000
#define USEC_PER_SEC   1000000
#define TIME_ALLOTMENT 200
#define TIME_SLICE 50

//structures
typedef struct {
    char name[50];
    int type;
    int length;
    int odds;
    int timeAllotment;
    int timeRemaining;
    int currPriority;
    struct timespec creationTime;
    struct timespec completionTime;
    struct timespec firstResponseTime;
    int respondedTo;
} Task;

typedef struct Node {
    Task *task;
    struct Node* next;
} Node;

typedef struct {
    Node* front;
    Node* end;
    int size;
} LinkedList;

typedef struct {
    LinkedList* p1;
    LinkedList* p2;
    LinkedList* p3;
    LinkedList* p4;
} Mlfq;

Node* createNode(Task *task);
void insertNodeAtEnd(LinkedList* list, Task *task);
Node* dequeue(LinkedList *list);
int queueEmpty(LinkedList *queue);
int mlfqEmpty(Mlfq *queue);
Task *pickNextTask(Mlfq *queue);
