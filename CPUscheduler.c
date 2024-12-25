#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <limits.h>

/*defining the process struct and its attributes*/
typedef struct PCB
{
    int *cpuBurst, *ioBurst;
    int cpuIndex, ioIndex, priority, numCpuBurst, 
    numCPUBurst, numIOBurst, PID;
    struct PCB *prev, *next;
    struct timespec ts_begin, ts_end, wt_begin, wt_end;
}PCB;

/*declaring functions*/
void* readFileThread(void* arg);
void* cpuSchedulerThread(void* arg);
void* ioSystemThread(void* arg);
void printInfo(char* alg, char* inputFile);

/*declaring and initializing global variables for use throughout runtime*/
struct PCB* cpuHead = NULL, *cpuTail = NULL, *ioHead = NULL, *ioTail = NULL;
int fileReadDone, cpuSchDone, ioSysDone, cpuBusy, ioBusy, quantumTime, numProcesses = 0;
sem_t semCPU, semIO, readyQ, ioQ, semTime;
double processRuntime = 0, totalTurnaroundTime = 0, totalWaitingTime = 0, cpuTime = 0;
struct timespec mainBegin, mainEnd, cpu_begin, cpu_end;

int main(int argc, char* argv[])
{
    /*declare variables for main*/
    char *inputFile = "", *alg = "", *quantum = "", *temp;
    int i;
    pthread_t readThread, cpuThread, ioThread;

    /*initialize semaphores*/
    sem_init(&semCPU, 0, 0);
    sem_init(&semIO, 0, 0);
    sem_init(&readyQ, 0, 1);
    sem_init(&ioQ, 0, 1);
    sem_init(&semTime, 0, 1);

    /*start clock to track total runtime*/
    clock_gettime(CLOCK_MONOTONIC, &mainBegin);

    /*collect all relevant arguments and store them in variables*/
    for(i = 0; i < argc; i++)
    {
        if(strcmp(argv[i], "-alg") == 0){alg = argv[i+1];}
        if(strcmp(argv[i], "-quantum") == 0){quantum = argv[i+1];}
        if(strcmp(argv[i], "-input") == 0){inputFile = argv[i+1];}
    }
    /*store given quantum time*/
    temp = quantum;
    sscanf(temp, "%d", &quantumTime);

    /*create threads*/
    if(pthread_create(&readThread, NULL, readFileThread, (void*)inputFile) != 0)
    {
        printf("ERROR with creating thread\n");
        return 1;
    }
    if(pthread_create(&cpuThread, NULL, cpuSchedulerThread, (void*)alg) != 0)
    {
        printf("ERROR with creating thread\n");
        return 1;
    }
    if(pthread_create(&ioThread, NULL, ioSystemThread, NULL))
    {
        printf("ERROR with creating thread\n");
        return 1;
    }
    /*join threads*/
    if(pthread_join(readThread, NULL) != 0)
    {
        printf("ERROR with Join\n");
    }
    if(pthread_join(cpuThread, NULL) != 0)
    {
        printf("ERROR with Join\n");
    }
    if(pthread_join(ioThread, NULL) != 0)
    {
        printf("ERROR with Join\n");
    }
    /*end clock for tracking total runtime*/
    clock_gettime(CLOCK_MONOTONIC, &mainEnd);

    /*calculations for runtime*/
    double elapsed = mainEnd.tv_sec - mainBegin.tv_sec;
    elapsed += (mainEnd.tv_nsec - mainBegin.tv_nsec) / 1000000000.0;
    processRuntime += elapsed*1000;

    /*function for printing metrics*/
    printInfo(alg, inputFile);
    
    /*destroy semaphores*/
    sem_destroy(&semCPU);
    sem_destroy(&semIO);
    sem_destroy(&readyQ);
    sem_destroy(&ioQ);
    sem_destroy(&semTime);
}

void* readFileThread(void* arg)
{
    /*declare and initialize variables*/
    FILE* file;
    char buffer[1000], *inputFile = (char*)arg, *cmd, *temp;
    int priority, sleepMS, burstArrSZ, burst, PID = 0;

    /*open file, and if NULL print error and exit*/
    file = fopen(inputFile, "r");
    if(file == NULL)
    {
        printf("ERROR with opening file\n");
        exit(1);
    }

    /*loop through each line in file and read it*/
    while(fgets(buffer, 1000, file))
    { 
        /*read <proc | sleep | stop> and process appropriately*/
        cmd = strtok(buffer, " "); 
        /*if the command was a process*/
        if(strcmp(cmd, "proc") == 0)
        {
            /*increment the number of processes found and allocate memory for a PCB*/
            numProcesses++;
            PCB* proc = (PCB*)malloc(sizeof(PCB));

            /*if malloc failed*/
            if(proc == NULL)
            {
                printf("ERROR with malloc for process\n"); 
                pthread_exit((void*)1);
            }

            /*read the tokens for priority and number of cpu/io bursts*/
            temp = strtok(NULL, " ");
            sscanf(temp, "%d", &priority);
            temp = strtok(NULL, " ");
            sscanf(temp, "%d", &burstArrSZ);

            /*assign process values and allocate memory*/
            proc->priority = priority;
            proc->cpuBurst = (int*)malloc(sizeof(int) * ((burstArrSZ/2) + 1));
            proc->ioBurst = (int*)malloc(sizeof(int) * ((burstArrSZ/2)));
            proc->numCPUBurst = burstArrSZ/2 + 1;
            proc->numIOBurst = burstArrSZ/2;
            proc->PID = PID++;
            proc->cpuIndex = 0;
            proc->ioIndex = 0;

            /*if error with previous mallocs*/
            if(proc->cpuBurst == NULL || proc->ioBurst == NULL)
            {
                printf("ERROR with malloc for cpuBurst or ioBurst\n");
                pthread_exit((void*)1);
            }

            /*loop through each burst and assign it to relevant array*/
            int i;
            for(i = 0; i < burstArrSZ/2; i++)
            {
                temp = strtok(NULL, " ");
                sscanf(temp, "%d", &burst);
                proc->cpuBurst[i] = burst;
                temp = strtok(NULL, " ");
                sscanf(temp, "%d", &burst);
                proc->ioBurst[i] = burst;
            }

            temp = strtok(NULL, " ");
            sscanf(temp, "%d", &burst);
            proc->cpuBurst[burstArrSZ/2] = burst;

            /*start timers for turnaround and waiting time*/
            clock_gettime(CLOCK_MONOTONIC, &proc->ts_begin);
            clock_gettime(CLOCK_MONOTONIC, &proc->wt_begin);

            /*wait on ready queue*/
            sem_wait(&readyQ);

            /*if ready queue is empty assign current process as new head*/
            if(cpuHead == NULL)
            {
                proc->next = NULL;
                proc->prev = NULL;
                cpuHead = proc;
            }
            /*if ready queue is not empty go to end of list and assign current process to the end*/
            else
            {
                struct PCB* temp = cpuHead;
                while(temp->next != NULL)
                {
                    temp = temp->next;
                }
                temp->next = proc;
                proc->prev = temp;
                proc->next = NULL;
            }
            /*post ready queue and allow CPU thread to run*/
            sem_post(&readyQ);
            sem_post(&semCPU);
        }
        /*if command is sleep, sleep for given time*/
        else if(strcmp(cmd, "sleep") == 0)
        {
            temp = strtok(NULL, " ");
            sscanf(temp, "%d", &sleepMS);
            sleep((double)sleepMS/1000);
        }
        /*if command is stop, close file and stop thread*/
        else if(strcmp(cmd, "stop") == 0)
        {
            fclose(file);
            fileReadDone = 1;
            pthread_exit((void*)0);
        }
        /*if unrecognized command*/
        else
        {
            printf("Unrecognized command from input file\n");
            pthread_exit((void*)1);
        }
    }
}

void* cpuSchedulerThread(void* arg)
{
    /*declare and initialize variables*/
    char buffer[1000], *algorithm = (char*)arg;
    PCB* currProc;

    /*loop for CPU scheduler*/
    while(1)
    {
        /*if variables are true, stop loop and post IO queue*/
        if(fileReadDone == 1 && cpuHead == NULL && ioHead == NULL && !cpuBusy && !ioBusy)
        {
            sem_post(&semIO);
            break;
        }
        /*if given algorithm is FIFO*/
        if(strcmp(algorithm, "FIFO") == 0)
        {
            /*wait for access to ready queue and wait for process to arrive, start CPU non-idle timer*/
            sem_wait(&semCPU);
            cpuBusy = 1;
            clock_gettime(CLOCK_MONOTONIC, &cpu_begin);
            sem_wait(&readyQ);

            /*if ready queue is empty, break*/
            if(cpuHead == NULL)
            {
                break;
            }
            /*if ready queue has 1 element*/
            else if(cpuHead->next == NULL){
                currProc = cpuHead;
                cpuHead = NULL;
            }
            /*if ready queue has multiple elements*/
            else
            {
                currProc = cpuHead;
                cpuHead = cpuHead->next;
                cpuHead->prev = NULL;
            }
            /*post ready queue*/
            sem_post(&readyQ);

            /*perform time claculations for waiting time*/
            clock_gettime(CLOCK_MONOTONIC, &currProc->wt_end);
            double elapsed = currProc->wt_end.tv_sec - currProc->wt_begin.tv_sec;
            elapsed += (currProc->wt_end.tv_nsec - currProc->wt_begin.tv_nsec) / 1000000000.0;
            totalWaitingTime += elapsed*1000;

            /*sleep for given time*/
            sleep((double)currProc->cpuBurst[currProc->cpuIndex++]/1000);

            /*if last CPU burst, terminate process and free data and calculate turnaround*/
            if(currProc->cpuIndex >= currProc->numCPUBurst)
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->ts_end);
                double elapsed1 = currProc->ts_end.tv_sec - currProc->ts_begin.tv_sec;
                elapsed1 += (currProc->ts_end.tv_nsec - currProc->ts_begin.tv_nsec) / 1000000000.0;
                totalTurnaroundTime += elapsed1*1000;

                free(currProc->cpuBurst);
                free(currProc->ioBurst);
                free(currProc);
                cpuBusy = 0;

                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;
            }
            /*if not last CPU burst, put into IO queue*/
            else
            {
                /*start waiting timer again*/
                clock_gettime(CLOCK_MONOTONIC, &currProc->wt_begin);
                /*wait for IO queue access*/
                sem_wait(&ioQ);

                /*if IO list is empty*/
                if(ioHead == NULL)
                {
                    currProc->next = NULL;
                    currProc->prev = NULL;
                    ioHead = currProc;
                }
                /*if IO list is non-empty, place at end*/
                else
                {
                    struct PCB* temp = ioHead;
                    while(temp->next != NULL)
                    {
                        temp = temp->next;
                    }
                    temp->next = currProc;
                    currProc->prev = temp;
                    currProc->next = NULL;
                }
                /*post IO queue*/
                sem_post(&ioQ);
                cpuBusy = 0;

                /*end timer for CPU non-idle time and calculate non-idle time*/
                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;

                /*post IO thread*/
                sem_post(&semIO);
            }
        }
        /*if algorithm is SJF*/
        else if(strcmp(algorithm, "SJF") == 0)
        {
            /*set clock, wait on ready queue and process to arrive*/
            int min = INT_MAX;
            sem_wait(&semCPU);
            cpuBusy = 1;
            clock_gettime(CLOCK_MONOTONIC, &cpu_begin);
            sem_wait(&readyQ);

            /*if ready queue is empty*/
            if(cpuHead == NULL)
            {
                break;
            }
            /*if there is one element in list*/
            else if(cpuHead->next == NULL){
                currProc = cpuHead;
                cpuHead = NULL;
            }
            /*if there are multiple elements in list*/
            else
            {
                PCB* temp = cpuHead;
                while(temp != NULL)
                {
                    if(temp->cpuBurst[temp->cpuIndex] < min)
                    {
                        min = temp->cpuBurst[temp->cpuIndex];
                        currProc = temp;
                    }
                    temp = temp->next;
                }
                /*if we are removing the head*/
                if(currProc->prev == NULL)
                {
                    cpuHead = cpuHead->next;
                    cpuHead->prev = NULL;
                }
                /*if we are removing the tail*/
                else if(currProc->next == NULL)
                {
                    currProc->prev->next = NULL;
                }
                /*if removing an inner node*/
                else
                {
                    currProc->prev->next = currProc->next;
                    currProc->next->prev = currProc->prev;
                }
            }
            /*release ready queue*/
            sem_post(&readyQ);

            /*stop clock and calculate time*/
            clock_gettime(CLOCK_MONOTONIC, &currProc->wt_end);
            double elapsed = currProc->wt_end.tv_sec - currProc->wt_begin.tv_sec;
            elapsed += (currProc->wt_end.tv_nsec - currProc->wt_begin.tv_nsec) / 1000000000.0;
            totalWaitingTime += elapsed*1000;

            /*sleep for given time*/
            sleep((double)currProc->cpuBurst[currProc->cpuIndex++]/1000);

            /*if last cpu burst, terminate and free*/
            if(currProc->cpuIndex >= currProc->numCPUBurst)
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->ts_end);
                double elapsed1 = currProc->ts_end.tv_sec - currProc->ts_begin.tv_sec;
                elapsed1 += (currProc->ts_end.tv_nsec - currProc->ts_begin.tv_nsec) / 1000000000.0;
                totalTurnaroundTime += elapsed1*1000;

                free(currProc->cpuBurst);
                free(currProc->ioBurst);
                free(currProc);
                cpuBusy = 0;

                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;
            }
            /*if not last cpu burst put into IO queue*/
            else
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->wt_begin);

                sem_wait(&ioQ);
                
                if(ioHead == NULL)
                {
                    currProc->next = NULL;
                    currProc->prev = NULL;
                     ioHead = currProc;
                }
                else
                {
                    struct PCB* temp = ioHead;
                    while(temp->next != NULL)
                    {
                        temp = temp->next;
                    }
                    temp->next = currProc;
                    currProc->prev = temp;
                    currProc->next = NULL;
                }
                /*release IO queue*/
                sem_post(&ioQ);
                cpuBusy = 0;

                /*stop timer*/
                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;

                /*post IO thread about a new process*/
                sem_post(&semIO);
            }
        }
        /*if algorithm is PR*/
        else if(strcmp(algorithm, "PR") == 0)
        {
            /*wait on ready queue access and process to arrive and start timer*/
            int max = INT_MIN;
            sem_wait(&semCPU);
            cpuBusy = 1;
            clock_gettime(CLOCK_MONOTONIC, &cpu_begin);
            sem_wait(&readyQ);

            /*if list is empty*/
            if(cpuHead == NULL)
            {
                break;
            }
            /*if one element in list*/
            else if(cpuHead->next == NULL)
            {
                currProc = cpuHead;
                cpuHead = NULL;
            }
            /*if more than one element in list*/
            else
            {
                PCB* temp = cpuHead;
                /*traverse through list*/
                while(temp != NULL)
                {
                    /*if new max priority found, update currProc*/
                    if(temp->priority > max)
                    {
                        max = temp->priority;
                        currProc = temp;
                    }
                    temp = temp->next;
                }
                /*if removing head*/
                if(currProc->prev == NULL)
                {
                    cpuHead = cpuHead->next;
                    cpuHead->prev = NULL;
                }
                /*if removing tail*/
                else if(currProc->next == NULL)
                {
                    currProc->prev->next = NULL;
                }
                /*if removing inner node*/
                else
                {
                    currProc->prev->next = currProc->next;
                    currProc->next->prev = currProc->prev;
                }
            }
            /*release ready queue*/
            sem_post(&readyQ);

            /*stop timer*/
            clock_gettime(CLOCK_MONOTONIC, &currProc->wt_end);
            double elapsed = currProc->wt_end.tv_sec - currProc->wt_begin.tv_sec;
            elapsed += (currProc->wt_end.tv_nsec - currProc->wt_begin.tv_nsec) / 1000000000.0;
            totalWaitingTime += elapsed*1000;

            /*sleep for given time*/
            sleep((double)currProc->cpuBurst[currProc->cpuIndex++]/1000);

            /*if last CPU burst, terminate and free process*/
            if(currProc->cpuIndex >= currProc->numCPUBurst)
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->ts_end);
                double elapsed1 = currProc->ts_end.tv_sec - currProc->ts_begin.tv_sec;
                elapsed1 += (currProc->ts_end.tv_nsec - currProc->ts_begin.tv_nsec) / 1000000000.0;
                totalTurnaroundTime += elapsed1*1000;

                free(currProc->cpuBurst);
                free(currProc->ioBurst);
                free(currProc);
                cpuBusy = 0;

                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;
            }
            /*if not last CPU burst, put into IO queue*/
            else
            {
                /*start wait timer*/
                clock_gettime(CLOCK_MONOTONIC, &currProc->wt_begin);

                /*lock IO queue*/
                sem_wait(&ioQ);

                /*if list is empty*/
                if(ioHead == NULL)
                {
                    currProc->next = NULL;
                    currProc->prev = NULL;
                    ioHead = currProc;
                }
                /*if list is not empty*/
                else
                {
                    struct PCB* temp = ioHead;
                    while(temp->next != NULL)
                    {
                        temp = temp->next;
                    }
                    temp->next = currProc;
                    currProc->prev = temp;
                    currProc->next = NULL;
                }
                /*release IO queue*/
                sem_post(&ioQ);
                cpuBusy = 0;

                /*stop timer*/
                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;

                /*post IO about new process*/
                sem_post(&semIO);
            }
        }
        /*if algorithm is RR*/
        else if(strcmp(algorithm, "RR") == 0)
        {
            /*wait on ready queue access and for processs to arrive, start timer*/
            sem_wait(&semCPU);
            cpuBusy = 1;
            clock_gettime(CLOCK_MONOTONIC, &cpu_begin);
            sem_wait(&readyQ);

            /*if list is empty*/
            if(cpuHead == NULL)
            {
                break;
            }
            /*if one element in list*/
            else if(cpuHead->next == NULL)
            {
                currProc = cpuHead;
                cpuHead = NULL;
            }
            /*if more than one element in list*/
            else
            {
                currProc = cpuHead;
                cpuHead = cpuHead->next;
                cpuHead->prev = NULL;
            }
            /*release ready queue*/
            sem_post(&readyQ);

            /*stop wait timer*/
            clock_gettime(CLOCK_MONOTONIC, &currProc->wt_end);
            double elapsed = currProc->wt_end.tv_sec - currProc->wt_begin.tv_sec;
            elapsed += (currProc->wt_end.tv_nsec - currProc->wt_begin.tv_nsec) / 1000000000.0;
            totalWaitingTime += elapsed*1000;

            /*if quantum time is longer than the cpu burst, just sleep for the cpu burst time*/
            if(quantumTime >= currProc->cpuBurst[currProc->cpuIndex])
            {
                sleep((double)currProc->cpuBurst[currProc->cpuIndex++]/1000);
            }
            /*if quantum time is less than cpu burst, sleep for quantum time and keep track of remaining time*/
            else 
            {
                currProc->cpuBurst[currProc->cpuIndex] = currProc->cpuBurst[currProc->cpuIndex] - quantumTime;
                sleep((double)currProc->cpuBurst[currProc->cpuIndex]/1000);
            }
            /*if last cpu burst, terminate and free process*/
            if(currProc->cpuIndex >= currProc->numCPUBurst)
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->ts_end);
                double elapsed1 = currProc->ts_end.tv_sec - currProc->ts_begin.tv_sec;
                elapsed1 += (currProc->ts_end.tv_nsec - currProc->ts_begin.tv_nsec) / 1000000000.0;
                totalTurnaroundTime += elapsed1*1000;

                free(currProc->cpuBurst);
                free(currProc->ioBurst);
                free(currProc);
                cpuBusy = 0;

                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;
            }
            /*if there are no more IO bursts, put back into ready queue*/
            else if(currProc->ioIndex >= currProc->numIOBurst)
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->wt_begin);

                sem_wait(&readyQ);

                if(cpuHead == NULL)
                {
                    currProc->next = NULL;
                    currProc->prev = NULL;
                    cpuHead = currProc;
                }
                else
                {
                    struct PCB* temp = cpuHead;
                    while(temp->next != NULL)
                    {
                        temp = temp->next;
                    }
                    temp->next = currProc;
                    currProc->prev = temp;
                    currProc->next = NULL;
                }
                sem_post(&readyQ);

                cpuBusy = 0;

                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;

                sem_post(&semCPU);
            }
            /*if not last cpu Burst, and it has more IO bursts, put into IO queue*/
            else
            {
                clock_gettime(CLOCK_MONOTONIC, &currProc->wt_begin);

                sem_wait(&ioQ);

                if(ioHead == NULL)
                {
                    currProc->next = NULL;
                    currProc->prev = NULL;
                    ioHead = currProc;
                }
                else
                {
                    struct PCB* temp = ioHead;
                    while(temp->next != NULL)
                    {
                        temp = temp->next;
                    }
                    temp->next = currProc;
                    currProc->prev = temp;
                    currProc->next = NULL;
                }
                sem_post(&ioQ);
                cpuBusy = 0;

                clock_gettime(CLOCK_MONOTONIC, &cpu_end);
                double elapsed2 = cpu_end.tv_sec - cpu_begin.tv_sec;
                elapsed2 += (cpu_end.tv_nsec - cpu_begin.tv_nsec) / 1000000000.0;
                cpuTime += elapsed2*1000;

                sem_post(&semIO);
            }
        }
        /*If somehow could not find algorithm*/
        else
        {
            printf("ERROR unable to read algorithm\n");
            exit(1);
        }
    }
    cpuSchDone = 1;
}

void* ioSystemThread(void* arg)
{
    /*the current process to keep track of*/
    PCB* currProc;

    /*the loop for IO*/
    while(1)
    {
        /*if variables are true, stop loop and post CPU thread*/
        if(fileReadDone == 1 && cpuHead == NULL && ioHead == NULL && !cpuBusy)
        {
            sem_post(&semCPU);
            break;
        }

        /*wait on process to enter IO queue and wait for exclusive access to IO queue*/
        sem_wait(&semIO);
        ioBusy = 1;
        sem_wait(&ioQ);

        /*if IO list is empty, break*/
        if(ioHead == NULL)
        {
            break;
        }
        /*if IO list has only 1 element*/
        else if(ioHead->next == NULL){
            currProc = ioHead;
            ioHead = NULL;
        }
        /*if IO list has multiple elements*/
        else
        {
            currProc = ioHead;
            ioHead = ioHead->next;
            ioHead->prev = NULL;
        }
        /*post IO queue, sleep for given time, and wait on ready queue access*/
        sem_post(&ioQ);
        sleep((double)currProc->ioBurst[currProc->ioIndex++]/1000);
        sem_wait(&readyQ);

        /*if ready queue is empty*/
        if(cpuHead == NULL)
        {
            currProc->next = NULL;
            currProc->prev = NULL;
            cpuHead = currProc;
        }
        /*if ready queue is not empty, put process at end*/
        else
        {
            struct PCB* temp = cpuHead;
            while(temp->next != NULL)
            {
                temp = temp->next;
            }
            temp->next = currProc;
            currProc->prev = temp;
            currProc->next = NULL;
        }
        /*post ready queue and CPU thread*/
        sem_post(&readyQ);
        ioBusy = 0;
        sem_post(&semCPU);
    }
    ioSysDone = 1;
}

void printInfo(char* alg, char* inputFile)
{
    /*prints metrics and info for program*/
    printf("-------------------------------------------------------\n");
    printf("Input File Name\t\t\t: %s\n", inputFile);
    if(strcmp(alg, "RR") == 0)
    {
        printf("CPU Scheduling Algorithm\t: %s Quantum: %d\n", alg, quantumTime);
    }
    else
    {
        printf("CPU Scheduling Algorithm\t: %s\n", alg);
    }
    printf("CPU Utilization\t\t\t: %0.2f%%\n", (double)(cpuTime/processRuntime)*100);
    printf("Throughput\t\t\t: %0.2f / ms\n", numProcesses/processRuntime);
    printf("Avg. Turnaround Time\t\t: %0.2f ms\n", totalTurnaroundTime/numProcesses);
    printf("Avg. Waiting Time in R Queue\t: %0.2f ms\n", totalWaitingTime/numProcesses);
    printf("-------------------------------------------------------\n");
}