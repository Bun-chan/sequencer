#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>

#define TRUE (1)
#define FALSE (0)
#define NANOSEC_PER_SEC (1000000000)
#define MY_CLOCK_TYPE CLOCK_MONOTONIC_RAW
#define NUMBER_OF_THREADS (5) //sequencer + S1 + S2 + S3 + S4 = 5
#define ASSIGNMENT_NUMBER 4
#define COURSE_NUMBER 2

//Function declarations.
void *sequencer(void *arg);
double realtime(struct timespec *tsptr);
void *Service_1(void *threadp);
void *Service_2(void *threadp);
void *Service_3(void *threadp);
void *Service_4(void *threadp);
void delayFor(float time);
void printUname(void);


//Global Variables.
int first_iteration = TRUE;
int abortTest = FALSE;
int abortS1 = FALSE, abortS2 = FALSE, abortS3 = FALSE, abortS4 = FALSE;
sem_t semS1, semS2, semS3, semS4;
struct timespec start_time_val;
double start_realtime;
const float delay_time = 9.5;

typedef struct
{
    int threadIdx;
    unsigned long long sequencePeriods;
} threadParams_t;


int main(void) {

	cpu_set_t threadcpu; //A set of cores.
	int rc; //the return code of function calls and inits.
	pid_t mainpid; //main process ID.
	int max_priority; //Maximum priority for SCHED_FIFO scheduling policy.
	struct sched_param main_param;
	pthread_attr_t rt_sched_attr[NUMBER_OF_THREADS];
	struct sched_param rt_param[NUMBER_OF_THREADS];
	threadParams_t threadParams[NUMBER_OF_THREADS];
	pthread_t threads[NUMBER_OF_THREADS];

	printUname();
	//Initialize the semaphores.
	rc = (sem_init (&semS1, 0, 0));
	if (rc != 0) {
		printf("error sem_init for semS1. %d\n", rc);
	}
	rc = (sem_init (&semS2, 0, 0));
	if (rc != 0) {
		printf("error sem_init for semS2. %d\n", rc);
	}
	rc = (sem_init (&semS3, 0, 0));
	if (rc != 0) {
		printf("error sem_init for semS3. %d\n", rc);
	}
	rc = (sem_init (&semS4, 0, 0));
	if (rc != 0) {
		printf("error sem_init for semS4. %d\n", rc);
	}

	//Get the process ID of the main thread.
	mainpid = getpid();

	//Get the max priority of SCHED_FIFO.
	max_priority = sched_get_priority_max(SCHED_FIFO);

	//Get the scheduling parameters of the main thread.
	rc = sched_getparam(mainpid, &main_param);

	//Set the main thread to the highest priority
    	main_param.sched_priority=max_priority;

	//Set the scheduling policy and the parameters for the main thread.
	rc = sched_setscheduler(getpid(), SCHED_FIFO, &main_param);
    	if (rc < 0) { perror("main_param"); exit(EXIT_FAILURE); }

	for (int i=0; i <NUMBER_OF_THREADS; i++) {
		//Set all threads to run on the same core.
		CPU_ZERO(&threadcpu);
      		CPU_SET(1, &threadcpu);

      		rc = pthread_attr_init(&rt_sched_attr[i]);
		if (rc != 0) {
			printf("error pthread_attr_init %d\n", rc);
		}	
      		rc = pthread_attr_setinheritsched(&rt_sched_attr[i], PTHREAD_EXPLICIT_SCHED);
		if (rc != 0) {
			printf("error pthread_attr_setinheritsched %d\n", rc);
		}
      		rc = pthread_attr_setschedpolicy(&rt_sched_attr[i], SCHED_FIFO);
		if (rc != 0) {
			printf("error pthread_attr_setschedpolicy %d\n", rc);
		}
      		rc = pthread_attr_setaffinity_np(&rt_sched_attr[i], sizeof(cpu_set_t), &threadcpu);
		if (rc != 0) {
			printf("error pthread_attr_setaffinity %d\n", rc);
		}
		
		//Decrease the priority by 1 for each successive thread. sequencer = 99, S1 = 98, S2 = 97 and so on.
      		rt_param[i].sched_priority = max_priority - i;
		printf("rt_param[%d].sched_priority %d\n", i, rt_param[i].sched_priority);
      		pthread_attr_setschedparam(&rt_sched_attr[i], &rt_param[i]);

      		threadParams[i].threadIdx=i;
	}

	//Set up service 1
	rc = pthread_create(&threads[1], &rt_sched_attr[1], Service_1, (void *)&(threadParams[1]));
    	if (rc != 0) {
		printf("error pthread_create S1 %d\n", rc);
	}

	//Set up service 2
    	rc = pthread_create(&threads[2], &rt_sched_attr[2], Service_2, (void *)&(threadParams[2]));
    	if (rc != 0) {
		printf("error pthread_create S2 %d\n", rc);
	}

	//Set up service 3.
    	rc = pthread_create(&threads[3], &rt_sched_attr[3], Service_3, (void *)&(threadParams[3]));
    	if (rc != 0) {
		printf("error pthread_create S3 %d\n", rc);
	}

	//Set up service 4.
    	rc = pthread_create(&threads[4], &rt_sched_attr[4], Service_4, (void *)&(threadParams[4]));
    	if (rc != 0) {
		printf("error pthread_create S4 %d\n", rc);
	}
	

	//Set up the sequencer.
	threadParams[0].sequencePeriods = 14; //LCM is 910
	//Set the sequencer to run on a different thread than the services.
	CPU_ZERO(&threadcpu);
      	CPU_SET(2, &threadcpu);
      	rc = pthread_attr_setaffinity_np(&rt_sched_attr[0], sizeof(cpu_set_t), &threadcpu);
	if (rc != 0) {
		printf("error pthread_attr_setaffinity sequencer %d\n", rc);
	}
    	rc = pthread_create(&threads[0], &rt_sched_attr[0], sequencer, (void *)&(threadParams[0]));
    	if (rc != 0) {
		printf("error pthread_create sequencer %d\n", rc);
	}


    	for (int i=0; i<NUMBER_OF_THREADS; i++) {
        	pthread_join(threads[i], NULL);
	}

    	printf("\ndone!\n");
}



void *sequencer(void *arg) {

	int seqCnt = 0;
	struct timespec remaining_time;
	struct timespec delay_time = {0,10000000}; // delay for 10  msec, 100 Hz
	int rc, delay_cnt=0;
	double residual;
	threadParams_t *threadParams = (threadParams_t *)arg;
        //syslog(LOG_INFO, "sequencer [COURSE:2][ASSIGNMENT:3]: threadParams->threadIdx %d  on core %d\n", threadParams->threadIdx,  sched_getcpu());

	do
	{
		delay_cnt = 0;
		residual = 0.0;
		do
		{
			rc = nanosleep(&delay_time, &remaining_time);
			if (rc != 0) {
				printf("error nanosleep %d\n", rc);
			}
			
			if(rc == EINTR)
			{
				residual = remaining_time.tv_sec + ((double)remaining_time.tv_nsec / (double)NANOSEC_PER_SEC);
				if(residual > 0.0) printf("residual=%lf, sec=%d, nsec=%d\n", residual, (int)remaining_time.tv_sec, (int)remaining_time.tv_nsec);
				delay_cnt++;
			}	
		} while((residual > 0.0) && (delay_cnt < 100));
		if (first_iteration == TRUE) {
			first_iteration = FALSE;
			clock_gettime(MY_CLOCK_TYPE, &start_time_val);
			start_realtime = realtime(&start_time_val);
		}
		// Release each service at a sub-rate of the generic sequencer rate
		// Service_1  50  Hz
		if ((seqCnt % 2) == 0) {
			//syslog(LOG_CRIT, "sem_post S1\n");
			sem_post(&semS1); 
		} 
		// Service_2 20 Hz
       		if ((seqCnt % 5) == 0) {
			//syslog(LOG_CRIT, "sem_post S2\n");
			sem_post(&semS2);
		}
        	// Service_3 14.3 Hz
		if ((seqCnt % 7)  == 0) {
			//syslog(LOG_CRIT, "sem_post S3\n");			
			sem_post(&semS3);	
		}
        	// Service_4 7.7 Hz
		if ((seqCnt % 13)  == 0) {
			//syslog(LOG_CRIT, "sem_post S4\n");			
			sem_post(&semS4);	
		}
		
        	seqCnt++;
	} while (!abortTest && (seqCnt < threadParams->sequencePeriods));
	
    	sem_post(&semS1);
	sem_post(&semS2);
	sem_post(&semS3);
	sem_post(&semS4);
    	abortS1 = TRUE;
	abortS2 = TRUE;
	abortS3 = TRUE; 
	abortS4 = TRUE; 
	pthread_exit((void *)0);
}

void *Service_1(void *threadp) {
	struct timespec current_time_val;
    	double current_realtime;
    	int S1Cnt = 0;
    	threadParams_t *threadParams = (threadParams_t *)threadp;

    	while(!abortS1) {

        	sem_wait(&semS1);        // sem_wait, will wait here until the semaphore is unlock, by sem_post, in the sequencer.
        	if (abortS1) { break; }
        	S1Cnt++;
        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	syslog(LOG_INFO, "[COURSE:%d][ASSIGNMENT:%d]: Thread %d start %d @ %6.9lf on core %d\n", COURSE_NUMBER, ASSIGNMENT_NUMBER, threadParams->threadIdx, S1Cnt, current_realtime-start_realtime, sched_getcpu());

		//Fake some work.
        	delayFor(delay_time); 

        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	//syslog(LOG_INFO, "[COURSE:2][ASSIGNMENT:3]: Thread %d end %d @ %6.9lf on core %d\n", threadParams->threadIdx, S1Cnt, current_realtime-start_realtime, sched_getcpu());
	}
	pthread_exit((void *)0);
}

void *Service_2(void *threadp)
{
	struct timespec current_time_val;
    	double current_realtime;
    	int S2Cnt = 0;
    	threadParams_t *threadParams = (threadParams_t *)threadp;

    	while(!abortS2) {

		sem_wait(&semS2);
        	if (abortS2) { break; }
        	S2Cnt++;
        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	syslog(LOG_INFO, "[COURSE:%d][ASSIGNMENT:%d]: Thread %d start %d @ %6.9lf on core %d\n",COURSE_NUMBER, ASSIGNMENT_NUMBER, threadParams->threadIdx, S2Cnt, current_realtime-start_realtime, sched_getcpu());

        	//Fake some work
        	delayFor(delay_time);

        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	//syslog(LOG_INFO, "[COURSE:2][ASSIGNMENT:3]: Thread %d end %d @ %6.9lf on core %d\n", threadParams->threadIdx, S2Cnt, current_realtime-start_realtime, sched_getcpu());
	}
    	pthread_exit((void *)0);
}


void *Service_3(void *threadp)
{
	//syslog(LOG_CRIT, "entering service3\n");
	struct timespec current_time_val;
    	double current_realtime;
    	int S3Cnt = 0;
    	threadParams_t *threadParams = (threadParams_t *)threadp;

    	while(!abortS3) {
        	sem_wait(&semS3);
        	if (abortS3) { break; }
        	S3Cnt++;

        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);

        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	syslog(LOG_INFO, "[COURSE:%d][ASSIGNMENT:%d]: Thread %d start %d @ %6.9lf on core %d\n",COURSE_NUMBER, ASSIGNMENT_NUMBER, threadParams->threadIdx, S3Cnt, current_realtime-start_realtime, sched_getcpu());


        	//Fake some work.
       		delayFor(delay_time);
		

        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	//syslog(LOG_INFO, "[COURSE:2][ASSIGNMENT:3]: Thread %d end %d @ %6.9lf on core %d\n", threadParams->threadIdx, S3Cnt, current_realtime-start_realtime, sched_getcpu());
	}

    pthread_exit((void *)0);
}




void *Service_4(void *threadp)
{
	struct timespec current_time_val;
    	double current_realtime;
    	int S4Cnt = 0;
    	threadParams_t *threadParams = (threadParams_t *)threadp;

    	while(!abortS4) {

		sem_wait(&semS4);
        	if (abortS4) { break; }
        	S4Cnt++;
        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	syslog(LOG_INFO, "[COURSE:%d][ASSIGNMENT:%d]: Thread %d start %d @ %6.9lf on core %d\n",COURSE_NUMBER, ASSIGNMENT_NUMBER, threadParams->threadIdx, S4Cnt, current_realtime-start_realtime, sched_getcpu());

        	//Fake some work
        	delayFor(delay_time);
		delayFor(delay_time);

        	clock_gettime(MY_CLOCK_TYPE, &current_time_val);
		current_realtime = realtime(&current_time_val);
        	//syslog(LOG_INFO, "[COURSE:2][ASSIGNMENT:3]: Thread %d end %d @ %6.9lf on core %d\n", threadParams->threadIdx, S2Cnt, current_realtime-start_realtime, sched_getcpu());
	}
    	pthread_exit((void *)0);
}





double realtime(struct timespec *tsptr)
{
    return ((double)(tsptr->tv_sec) + (((double)tsptr->tv_nsec)/1000000000.0));
}

void delayFor(float time)
{
	struct timespec start_time, current_time;
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &current_time);
		unsigned long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + (current_time.tv_nsec - start_time.tv_nsec) / 1000000;
		if (elapsed_ms >= time) {
            	break;
		}	
	}
}


//This function will run the command "uname -a" and then syslog the result.
void printUname(void) {
	FILE *cmd = popen("uname -a", "r");
	char result[999] = {0x0};
	while (fgets(result, sizeof(result), cmd) != NULL) {
		syslog(LOG_CRIT, "[COURSE:%d][ASSIGNMENT:%d] %s",COURSE_NUMBER, ASSIGNMENT_NUMBER, result);	
	}
}
