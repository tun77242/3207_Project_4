#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

void initiate_generator();
void initiate_handler();
void free_generated_memory();
void free_handler_memory();
void signal_generator();
void signal_handler(int i);
void signal_reporter();
void exit_parent();
void exit_handler();
void exit_generator();
void process_handler(int);
void signal_report_handler(int);

pid_t pid[8];

struct genstruct
{
    sig_atomic_t sig1;
    sig_atomic_t sig2;
    pthread_mutex_t lock1;
    pthread_mutexattr_t attr1;
    pthread_mutex_t lock2;
    pthread_mutexattr_t attr2;
};
struct genstruct * volatile generate;        //ptr for shared memory for generator
int sharedID;                           //ID for that

struct handlerstruct
{
    sig_atomic_t sig1;
    sig_atomic_t sig2;
    pthread_mutex_t sig1lock;
    pthread_mutexattr_t sig1attr;
    pthread_mutex_t sig2lock;
    pthread_mutexattr_t sig2attr;
};
struct handlerstruct * volatile handle;        //ptr for shared memory for handler

struct reporterstruct                           //reporter process structure
{
    sig_atomic_t reportcount;                   //tells reporter when 10 signals received

    long sig1avg;                               //avg time btwn signals
    long sig2avg;

    sig_atomic_t sig1rc;                        //signal counters
    sig_atomic_t sig2rc;

    long sig1avgtotal;                          //avg time for all reports
    long sig2avgtotal;

    sig_atomic_t numreports;                    //signal count to the reporter
};
struct reporterstruct volatile rep;

sigset_t mask;

struct timespec sig1time;                       //gets avg time btwn signal reception
struct timespec sig2time;

int isreporter = 0;

int main(int argc, char *argv[])
{
    initiate_generator();
    initiate_handler();

    struct sigaction action;                    //parent process that ignores SIGUSR1 and 2
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    int i;                                      //child process here
    for(i = 0; i < 8; i++)
    {
        pid[i] = fork();
        if(pid[i] == 0)
        {
            if(i < 3)
            {
                signal_generator();
            }
            else if(i == 7)
            {
                signal_reporter();
            }
            else
            {
                signal_handler(i);
            }
        }
    }
    
    struct sigaction exitaction;                    //setup of handler for SIGINT and SIGTERM to a function to deallocate the shared memory.
    exitaction.sa_handler = exit_parent;
    sigemptyset(&exitaction.sa_mask);
    exitaction.sa_flags = 0;
    sigaction(SIGINT, &exitaction, NULL);
    sigaction(SIGTERM, &exitaction, NULL);

    int status;
    for(i = 0; i < 8; i++)                          //waits for child process to finish
    {
        waitpid(pid[i], &status, 0);
    }

    free_generated_memory();
    free_handler_memory();
    return 0;
}

void initiate_generator()
{
    key_t key = ftok("key.bat", 1);
    sharedID = shmget(key,sizeof(struct genstruct *),0666|IPC_CREAT);
    generate = (struct genstruct *)shmat(sharedID,(void*)0,0);
    generate->sig1 = 0;
    generate->sig2 = 0;
   
    pthread_mutexattr_init(&(generate->attr1));
    pthread_mutexattr_setpshared(&(generate->attr1), PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(generate->lock1), &(generate->attr1));
    pthread_mutexattr_init(&(generate->attr2));
    pthread_mutexattr_setpshared(&(generate->attr2), PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(generate->lock2), &(generate->attr2));
}

void initiate_handler()
{
    key_t key = ftok("key.bat", 2);
    sharedID = shmget(key,sizeof(struct handlerstruct *),0666|IPC_CREAT);
    handle = (struct handlerstruct *)shmat(sharedID,(void*)0,0);
    pthread_mutexattr_init(&(handle->sig1attr));
    pthread_mutexattr_setpshared(&(handle->sig1attr), PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(handle->sig1lock), &(handle->sig1attr));
    pthread_mutexattr_init(&(handle->sig2attr));
    pthread_mutexattr_setpshared(&(handle->sig2attr), PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(handle->sig2lock), &(handle->sig2attr));
    handle->sig1 = 0;
    handle->sig2 = 0;
}

void signal_generator()                                     //each pid is seed for srand
{
    struct sigaction exitaction;
    exitaction.sa_handler = exit_generator;
    sigemptyset(&exitaction.sa_mask);
    exitaction.sa_flags = 0;
    sigaction(SIGINT, &exitaction, NULL);
    sigaction(SIGTERM, &exitaction, NULL);

    int pid = (int)getpid();
    srand(pid);
    int sig;
    int i = 0;
    useconds_t waitt;

    time_t start;                                           //loop for n seconds
    time_t current;
    float telapsed;
    float end = 30;
    time(&start);

    while(telapsed < end)
    {
        if((sig = rand())%2 == 0)                           //sends to SIGUSR1 if rand num is even
        {
            kill(0, SIGUSR1);
            pthread_mutex_lock(&(generate->lock1));
            (generate->sig1)++;
            pthread_mutex_unlock(&(generate->lock1));
        }
        else                                                //sends to SIGUSR2 otherwise
        {
            kill(0, SIGUSR2);
            pthread_mutex_lock(&(generate->lock2));
            (generate->sig2)++;
            pthread_mutex_unlock(&(generate->lock2));
        }

        i++;
        waitt = (useconds_t) ((rand()%(100000-10001)) + 10000);         //delays the next iteration by .01 to .1 seconds
        usleep(waitt);
        current = time(0);
        telapsed = difftime(current, start);
    }   
    
    kill(0, SIGTERM);
    exit(0);
}

void process_handler(int sig)                               //handles signal handlers
{
    if(sig == SIGUSR1)
    {
        pthread_mutex_lock(&(handle->sig1lock));
        handle->sig1++;
        pthread_mutex_unlock(&(handle->sig1lock));
    }
    else
    {
        pthread_mutex_lock(&(handle->sig2lock));
        handle->sig2++;
        pthread_mutex_unlock(&(handle->sig2lock));
    }
}

void signal_handler(int i)
{
    struct sigaction action;                            //signal structrure for process which is handled
    action.sa_handler = process_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaddset(&action.sa_mask, SIGUSR1);
    sigaddset(&action.sa_mask, SIGUSR2);

    struct sigaction ignaction;                         //signal structure for process which is ignored
    ignaction.sa_handler = SIG_IGN;
    sigemptyset(&ignaction.sa_mask);
    ignaction.sa_flags = 0;

    struct sigaction exitaction;
    exitaction.sa_handler = exit_handler;
    sigemptyset(&exitaction.sa_mask);
    exitaction.sa_flags = 0;
    sigaction(SIGINT, &exitaction, NULL);               //processes to handle SIGINT and SIGTERM
    sigaction(SIGTERM, &exitaction, NULL);

    if(i%2 == 0)                                        //processes to handle SIGUSR1 and 2
    {
        sigaction(SIGUSR1, &action, NULL);
        sigaction(SIGUSR2, &ignaction, NULL);
    }
    else
    {
        sigaction(SIGUSR1, &ignaction, NULL);
        sigaction(SIGUSR2, &action, NULL);
    }
   
    while(1)
    {
        pause();
    }
   
    exit(0);
}

void signal_reporter()
{
    isreporter = 1;
    rep.reportcount = 0;

    struct sigaction action;                                    //signal structrure for process which is handled
    action.sa_handler = signal_report_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaddset(&action.sa_mask, SIGUSR1);
    sigaddset(&action.sa_mask, SIGUSR2);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    sigaddset(&mask, SIGUSR1);                                  //mask that blocks signals while showing
    sigaddset(&mask, SIGUSR2);

    struct sigaction exitaction;                                //handler for SIGINT and SIGTERM
    exitaction.sa_handler = exit_handler;
    sigemptyset(&exitaction.sa_mask);
    exitaction.sa_flags = 0;
    sigaction(SIGINT, &exitaction, NULL);
    sigaction(SIGTERM, &exitaction, NULL);

    while(1)
    {
        pause();
    }
    
    exit(0);
}

void signal_report_handler(int sig)                         //reports the process
{
    time_t lastsec;                                         //determines current system time
    long lastnsec;

    if(sig == SIGUSR1)                                      //handles SIGUSR1
    {
        rep.sig1rc++;                                       //increments signal count and adds to avg time
        rep.reportcount++;
        if(rep.reportcount != 1)
        {
          lastsec = sig1time.tv_sec;
          lastnsec = sig1time.tv_nsec;
          clock_gettime(CLOCK_REALTIME, &sig1time);
          rep.sig1avg = (sig1time.tv_sec - lastsec) * 1000000 + (sig1time.tv_nsec - lastnsec)/1000;
        }
        else
        {
            clock_gettime(CLOCK_REALTIME, &sig1time);
        }
    }
    else                                                    //handles SIGUSR2
    {
        rep.sig2rc++;                                       //increments signal count and adds to avg time
        rep.reportcount++;
        if(rep.reportcount != 1)
        {
            lastsec = sig2time.tv_sec;
            lastnsec = sig2time.tv_nsec;
            clock_gettime(CLOCK_REALTIME, &sig2time);
            rep.sig2avg = (sig2time.tv_sec - lastsec) * 1000000 + (sig2time.tv_nsec - lastnsec)/1000;
        }
        else
        {
            clock_gettime(CLOCK_REALTIME, &sig2time);
        }
    }
    
    if(rep.reportcount == 10)                              //prints out the report 
    {
        rep.numreports++;
        struct timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        printf("System time: %lu\n", time.tv_sec);
        printf("\tSIGUSR1: %d\n\tSIGUSR2: %d\n", rep.sig1rc, rep.sig2rc);
        
        int i,j;                                           //finds avg time btwn signal reception
        if(rep.sig1rc == 1)
        {
            i = 0;
            j = (rep.sig2avg/(rep.sig2rc-1));
        }
        else if(rep.sig2rc == 1)
        {
            i = (rep.sig1avg/(rep.sig1rc-1));
            j = 0;
        }
        else
        {
            i = (rep.sig1avg/(rep.sig1rc-1));
            j = (rep.sig2avg/(rep.sig2rc-1));
        }
        
        rep.sig1avgtotal += i;
        rep.sig2avgtotal += j;
        printf("\tS1 avg time: %d μs\n\tS2 avg time: %d μs\n", i, j);
        pthread_mutex_lock(&(handle->sig1lock));
        printf("\tS1 total: %d\n", handle->sig1);
        pthread_mutex_unlock(&(handle->sig1lock));
        pthread_mutex_lock(&(handle->sig2lock));
        printf("\tS2 total: %d\n", handle->sig2);
        pthread_mutex_unlock(&(handle->sig2lock));
        rep.reportcount = 0;
        rep.sig1rc = 0;
        rep.sig2rc = 0;
        rep.sig1avg = 0;
        rep.sig2avg = 0;
    }
}

void free_generated_memory()                    //destroys locks, deallocates shared memory
{
    printf("SIGUSR1 sent: %d\nSIGUSR2 sent: %d\n", generate->sig1, generate->sig2);
    pthread_mutex_destroy(&(generate->lock1));
    pthread_mutexattr_destroy(&(generate->attr1));
    pthread_mutex_destroy(&(generate->lock2));
    pthread_mutexattr_destroy(&(generate->attr2));
    shmdt(generate);
    shmctl(sharedID,IPC_RMID,NULL);
}

void exit_generator()                           //detaches shared memory
{
    shmdt(generate);
    exit(0);
}

void free_handler_memory()                          //same as above
{
    pthread_mutexattr_destroy(&(handle->sig1attr));
    pthread_mutex_destroy(&(handle->sig1lock));
    pthread_mutexattr_destroy(&(handle->sig2attr));
    pthread_mutex_destroy(&(handle->sig2lock));
    shmdt(handle);
    shmctl(sharedID,IPC_RMID,NULL);
}

void exit_handler()
{
    if(isreporter)
    {
        long i = rep.sig1avgtotal/rep.numreports;
        long j = rep.sig2avgtotal/rep.numreports;
        printf("S1 avg total: %ld μs\nS2 avg total: %ld μs\n", i, j);       //total avg signal reception
        printf("Reports: %d\n", rep.numreports);
    }
    shmdt(handle);
    exit(0);
}

void exit_parent()
{
    int status;
    int i;
    for(i = 0; i < 8; i++)
    {
        waitpid(pid[i], &status, 0);
    }
    
    free_generated_memory();
    free_handler_memory();
    exit(0);
}