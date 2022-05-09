#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>

sigset_t mask;
pthread_t tid[8];

void signal_int_term();
void initiate_generator();
void *signal_generator(void *);
void signal_to_threads(int);
void des_gen_lock();
void initiate_handler();
void thread_handler(int);
void *signal_handler(void *);
void *signal_reporter(void *);
void signal_report();
void signal_average();
void des_handler_lock();

struct genstruct
{
    int sig1;
    int sig2;
    pthread_mutex_t lock1;
    pthread_mutexattr_t attr1;
    pthread_mutex_t lock2;
    pthread_mutexattr_t attr2;
};
struct genstruct generate;

struct handlerstruct
{
    sig_atomic_t sig1;                      //signal counters
    sig_atomic_t sig2;
    pthread_mutex_t sig1lock;
    pthread_mutex_t sig2lock;
};
struct handlerstruct handle;

struct reporterstruct
{
    sig_atomic_t reportcount;   //tells reporter when 10 signals are received

    long sig1avg;               //avg time btwn signals
    long sig2avg;

    sig_atomic_t sig1rc;        //signal counters
    sig_atomic_t sig2rc;

    long sig1avgtotal;          //avg time for all reports
    long sig2avgtotal;

    sig_atomic_t numreports;    //signal count to the reporter
};
struct reporterstruct rep;

struct timespec sig1time;       //gets avg time btwn signal reception
struct timespec sig2time;

pthread_mutex_t handlerlock;      //lock for handlers to ignore SIGUSR1 n 2
int i;
pthread_t sigid[4];

int main(int argc, char *argv[])
{
    initiate_generator();
    initiate_handler();
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);

    struct sigaction action;            //parent process that ignores SIGUSR1 and 2
    action.sa_handler = thread_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    struct sigaction quitaction;            //handler for SIGINT n SIGTERM
    quitaction.sa_handler = signal_int_term;
    sigemptyset(&quitaction.sa_mask);
    quitaction.sa_flags = 0;
    sigaction(SIGINT, &quitaction, NULL);
    sigaction(SIGTERM, &quitaction, NULL);

    int i;
    for(i = 0; i < 8; i++)                  //child process
    {
        if(i < 3)
        {
            pthread_create(&tid[i], NULL, signal_generator, NULL);
        }
        else if(i == 7)
        {
            pthread_create(&tid[i], NULL, signal_reporter, NULL);
        }
        else
        {
            pthread_create(&tid[i], NULL, signal_handler, &i);
        }
    }
    
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    for(i = 0; i < 8; i++)                  //waits for child to finish
    {
        pthread_join(tid[i], NULL);
    }

    return 0;
}

void initiate_generator()
{
    generate.sig1 = 0;
    generate.sig2 = 0;
    pthread_mutexattr_init(&generate.attr1);
    pthread_mutex_init(&generate.lock1, &generate.attr1);
    pthread_mutexattr_init(&generate.attr2);
    pthread_mutex_init(&generate.lock2, &generate.attr2);
}

void * signal_generator(void * args)                //each pid is seed for srand
{
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    int pid = (int)getpid();
    srand(pid);
    int sig;
    int i = 0;
    useconds_t waittime;
    waittime = (useconds_t) ((rand()%(100000-10001)) + 10000);
    usleep(waittime);

    time_t start;               //loop for n seconds
    time_t curr;
    float timeelapsed;
    float end = 30;
    time(&start);

    while(timeelapsed < end)
    {
        if((sig = rand())%2 == 0)           //sends to SIGUSR1 if rand num is even
        {
            pthread_mutex_lock(&generate.lock1);
            signal_to_threads(SIGUSR1);
            generate.sig1++;
            pthread_mutex_unlock(&generate.lock1);
        }
        else                                ////sends to SIGUSR2 otherwise
        {
            pthread_mutex_lock(&generate.lock2);
            signal_to_threads(SIGUSR2);
            generate.sig2++;
            pthread_mutex_unlock(&generate.lock2);
        }
        
        i++;
        waittime = (useconds_t) ((rand()%(100000-10001)) + 10000);      //delays the next iteration by .01 to .1 seconds
        usleep(waittime);
        curr = time(0);
        timeelapsed = difftime(curr, start);
    }
    
    sleep(1);
    kill(0, SIGTERM);
    pthread_exit(0);
}

void signal_to_threads(int sig)             //sends signals to handler threads and reporter thread
{
    int i;
    for(i = 3; i < 8; i++)
    {
        pthread_kill(tid[i], sig);
    }
}

void des_gen_lock()                         //destroys locks
{
    printf("SIGUSR1 sent: %d\nSIGUSR2 sent: %d\n", generate.sig1, generate.sig2);
    pthread_mutex_destroy(&generate.lock1);
    pthread_mutexattr_destroy(&generate.attr1);
    pthread_mutex_destroy(&generate.lock2);
    pthread_mutexattr_destroy(&generate.attr2);
}

void initiate_handler()
{
    handle.sig1 = 0;
    handle.sig2 = 0;
    pthread_mutex_init(&handlerlock, NULL);
    pthread_mutex_init(&handle.sig1lock, NULL);
    pthread_mutex_init(&handle.sig2lock, NULL);
    i = 0;
}

void thread_handler(int sig)            //handles signal handlers
{
    //if the thread is the reporter thread
    if(tid[7] == pthread_self())
    {
        time_t lastsec;
        long lastnsec;
        if(sig == SIGUSR1)
        {
            rep.sig1rc++;
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
        else
        {
            rep.sig2rc++;
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
    
        if(rep.reportcount == 10)
        {
            signal_report();
        }
    }
    
    else if(sig == SIGUSR1)         //handler threads
    {
        pthread_mutex_lock(&handle.sig1lock);
        handle.sig1++;
        pthread_mutex_unlock(&handle.sig1lock);
    }
    else
    {
        pthread_mutex_lock(&handle.sig2lock);
        handle.sig2++;
        pthread_mutex_unlock(&handle.sig2lock);
    }
}

void * signal_handler(void * arg)
{
    sigset_t handlemask;
    sigemptyset(&handlemask);
    pthread_mutex_lock(&handlerlock);           //lock for each threads to ignore SIGUSR1 n 2
    sigid[i] = pthread_self();
    
    if(i < 2)
    {
        sigaddset(&handlemask, SIGUSR1);
    }
    else
    {
        sigaddset(&handlemask, SIGUSR2);
    }
    
    i++;
    pthread_mutex_unlock(&handlerlock);
    pthread_sigmask(SIG_BLOCK, &handlemask, NULL);

    while(1)
    {
        pause();
    }
    
    exit(0);
}

void * signal_reporter(void * args)
{
    rep.reportcount = 0;
    while(1)
    {
        pause();
    }
    exit(0);
}

void signal_report()                //finds averages and report
{
    rep.numreports++;
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    printf("System time: %lu\n", time.tv_sec);
    printf("\tSIGUSR1: %d\n\tSIGUSR2: %d\n", rep.sig1rc, rep.sig2rc);
    int i,j;
    
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
    pthread_mutex_lock(&handle.sig1lock);
    printf("\tS1 total: %d\n", handle.sig1);
    pthread_mutex_unlock(&handle.sig1lock);

    pthread_mutex_lock(&handle.sig2lock);
    printf("\tS2 total: %d\n", handle.sig2);
    pthread_mutex_unlock(&handle.sig2lock);
    rep.reportcount = 0;
    rep.sig1rc = 0;
    rep.sig2rc = 0;
    rep.sig1avg = 0;
    rep.sig2avg = 0;
}

void signal_average()           //prints total signal avg reception
{
    long i = rep.sig1avgtotal/rep.numreports;
    long j = rep.sig2avgtotal/rep.numreports;
    printf("S1 avg total: %ld μs\nS2 avg total: %ld μs\n", i, j);
    printf("Reports: %d\n", rep.numreports);
}

void des_handler_lock()             //destroys locks
{
    pthread_mutex_destroy(&handle.sig1lock);
    pthread_mutex_destroy(&handle.sig2lock);
    pthread_mutex_destroy(&handlerlock);
}

void signal_int_term()
{
    signal_average();
    des_gen_lock();
    des_handler_lock();
    exit(0);
}