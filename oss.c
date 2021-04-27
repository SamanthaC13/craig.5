/*Samantha Craig
 * CS4760
 * Assignment 5*/
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<sys/time.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<time.h>
#include<semaphore.h>
#include<stdbool.h>

#define PERM (S_IRUSR|S_IWUSR)
#define SEM_PERMS (mode_t)(S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define SEM_FLAGS (O_CREAT|O_EXCL)
#define maxLogLen 10000
#define RESOURCES 10
#define CONCURRENTTASKS 5
#define TOTALTASKS 15

typedef struct{
	unsigned int sec;
	unsigned int nsec;
}systemclock_t;

typedef struct{
	int resourceid;
	int total;
	int available;
	int allocated;
	int shareable;
	int procalloc[18];
	int procreq[18];
}resourcedesc_t;

typedef struct{
    int pid;
    int reqres;
    int reqnumres;
}proc_t;

typedef struct{
	long mtype;
	char mtext[100];
}mymsg_t;

//global variables
pid_t childpid;
systemclock_t *sc;
resourcedesc_t *res;
int scid, resid, msgid;
FILE *logfile;
int loglen=0;
sem_t *clockaccess;
proc_t proctable[18];
struct Queue* deadlockq;
int deadlockprocs=0;
int numgrantednow=0;
int numgrantedlater=0;
int numtermsuccess=0;
int numtermdeadlock=0;
int numdeadlockrun=0;
int numdeadlocks=0;
int numdeadlockprocs=0;

int timespec2str(char buf[], struct timespec ts)
{
	const int bufsize=31;
	struct tm tm;
	localtime_r(&ts.tv_sec, &tm);
	strftime(buf,bufsize,"%Y-%m-%d %H:%M:%S.", &tm);
	sprintf(buf,"%s%09luZ", buf, ts.tv_nsec);
}

int randomint(int lower, int upper)
{
	int num=(rand() % (upper-lower +1))+lower;
	return num;
}

void advanceclock(systemclock_t *sc,int addsec,int addnsec)
{
	sc->sec=(sc->sec+addsec)+((sc->nsec+addnsec)/1000000);
	sc->nsec=(sc->nsec+addnsec)%1000000;
}

int isPast(systemclock_t *first,systemclock_t *second)
{
	if(second->sec > first->sec)
	{
		//isPast is true
		return 1;
	}
	if((second->sec > first->sec) && (second->nsec > first->nsec))
	{
		//isPast is true
		return 1;
	}
	//isPast is false
	return 0;
}

int findnextpid()
{
    int i=0;
    for(i=0;i<CONCURRENTTASKS;i++)
    {
        if(proctable[i].pid==-1)
        {
            return i;
        }
    }
    return -1;
}

void printresources()
{
    int i=0;
    int j=0;
    fprintf(logfile,"     ");
    for(i=0;i<RESOURCES;i++)
    {
        fprintf(logfile,"   R%02d",i);
    }
    fprintf(logfile,"\n");
    loglen++;
    fprintf(logfile,"Total");
    for(i=0;i<RESOURCES;i++)
    {
        fprintf(logfile,"    %02d",res[i].total);
    }
    fprintf(logfile,"\n");
    loglen++;
    fprintf(logfile,"Avail");
    for(i=0;i<RESOURCES;i++)
    {
        fprintf(logfile,"    %02d",res[i].available);
    }
    fprintf(logfile,"\n");
    loglen++;
    for(j=0;j<CONCURRENTTASKS;j++)
    {
        if(proctable[j].pid!=-1)
        {
            fprintf(logfile,"P%02d  ",j);
            for(i=0;i<RESOURCES;i++)
            {
                fprintf(logfile,"    %02d",res[i].procalloc[j]);
            }
            fprintf(logfile,"\n");
            loglen++;
        }
    }
}

void cleanup()
{
	int i;
	char timestr[31];
	struct timespec tpend;

	//calculate statistics
	for(i=0;i<CONCURRENTTASKS;i++)
	{
		if(proctable[i].pid!=-1)
		{
			fprintf(stderr,"Killing child: %d\n",proctable[i].pid);
			kill(proctable[i].pid,SIGTERM);
		}
	}
	sleep(1);
	printresources();
	fprintf(logfile,"Number of requests granted immediately: %i\n",numgrantednow);
	fprintf(logfile,"Number of requests granted after waiting: %i\n",numgrantedlater);
	fprintf(logfile,"Number of processes terminating successfully: %i\n",numtermsuccess);
	fprintf(logfile,"Number of processes terminated by deadlock recovery: %i\n",numtermdeadlock);
	fprintf(logfile,"Number of times deadlock detection ran: %i\n",numdeadlockrun);
	fprintf(logfile,"Number of processes in deadlock: %i\n",numdeadlockprocs);
	fprintf(logfile,"Number of deadlocks detected: %i\n",numdeadlocks);
	if(numdeadlocks==0)
	    fprintf(logfile,"Average number of processes killed per deadlock: 0.0\n");	
    else
	    fprintf(logfile,"Average number of processes killed per deadlock: %05.6f\n",(double)numtermdeadlock/(double)numdeadlocks);	
	
	fprintf(stderr,"Clean up started\n");

	if(shmdt(sc)==-1)
	{
		perror("Failed to detach from shared memory for system clock");
		exit(1);
	}
	if(shmctl(scid, IPC_RMID, NULL)==-1)
	{
		perror("Failed to remove shared memory for system clock");
		exit(1);
	}
	if(shmdt(res)==-1)
	{
		perror("Failed to to detach from shared memory for resources");
		exit(1);
	}
	if(shmctl(resid,IPC_RMID, NULL)==-1)
	{
		perror("Failed to remove shared memory for resources");
		exit(1);
	}
	if(msgctl(msgid, IPC_RMID, NULL)==-1)
	{
		perror("Failed to remove message queue");
		exit(1);
	}
	if((sem_close(clockaccess))==-1)
	{
	    perror("Failed to close clock access semaphore");
	    exit(1);
	}
	if((sem_unlink("/clockaccess"))==-1)
	{
	    perror("Failed to unlink clock access semaphore");
	    exit(1);
	}
	fprintf(stderr,"Clean up complete\n");
	if(clock_gettime(CLOCK_REALTIME, &tpend)==-1)
	{
		perror("Failed ot get ending time");
		exit(1);
	}
	timespec2str(timestr,tpend);
	fprintf(stderr,"End of program: %s\n", timestr);
	fclose(logfile);

}

static void catchctrlcinterrupt(int signo)
{
	char ctrlcmsg[]="Ctrl-c interrupt\n";
	int msglen=sizeof(ctrlcmsg);
	write(STDERR_FILENO,ctrlcmsg,msglen);
	cleanup();
	exit(1);
}

void abnormalterm()
{
    cleanup();
    exit(1);
}

static void catchtimerinterrupt(int signo)
{
	char timermsg[]="Timer interrupt after specified number of seconds\n";
	int msglen =sizeof(timermsg);
	write(STDERR_FILENO, timermsg,msglen);
	cleanup();
	exit(1);
}

static int setuptimer(int t)
{
	struct itimerval value;
	value.it_interval.tv_sec=t;
	value.it_interval.tv_usec=0;
	value.it_value=value.it_interval;
	return(setitimer(ITIMER_REAL,&value,NULL));
}

struct Queue{
	int front;
	int rear;
	int size;
	int array[CONCURRENTTASKS];
};
//function to create queue
//it initalizes the size of the queue to 0
struct Queue* createQueue(){
	struct Queue* queue=(struct Queue*)malloc(sizeof(struct Queue));
	queue->front=queue->size=0;
	queue->rear=CONCURRENTTASKS-1;
	return queue;
}
//check if queue is full 
int isFull(struct Queue* queue)
{
	return(queue->size==CONCURRENTTASKS);
}
//check if queue is empty when size is 0
int isEmpty(struct Queue* queue)
{
	return(queue->size==0);
}
//Add an item to the queue
int enqueue(struct Queue* queue, int item)
{
	if(isFull(queue))
		return -1;
	queue->rear=(queue->rear + 1) % CONCURRENTTASKS;
	queue->array[queue->rear]=item;
	queue->size=queue->size+1;
	return 0;
}
//take an item off the queue
int dequeue(struct Queue* queue)
{
	if(isEmpty(queue))
		return -1;
	int item= queue->array[queue->front];
	queue->front=(queue->front+1)%CONCURRENTTASKS;
	queue->size=queue->size-1;
	return item;
}
//function to get the front of the queue
int front(struct Queue* queue)
{
	if(isEmpty(queue))
	{
		return -1;
	}
	return queue->array[queue->front];
}
//function to get the rear of the queue
int rear(struct Queue* queue)
{
	if(isEmpty(queue))
		return -1;
	return queue->array[queue->rear];
}
//Function to get size of queue
int size(struct Queue* queue)
{
	return queue->size;
}

int allocateresources(int resproc, int resource, int numresources)
{
    if(res[resource].available>=numresources)
    {
        res[resource].available-=numresources;
        res[resource].allocated+=numresources;
        res[resource].procalloc[resproc]+=numresources;
        return 0;
    }
    else
    {
        return -1;
    }
}

int releaseresources(int resproc, int resource, int numresources)
{
    res[resource].available+=numresources;
    res[resource].allocated-=numresources;
    res[resource].procalloc[resproc]-=numresources;
    return 0;
}

void terminateproc(int proc)
{
    int i;
    for(i=0;i<RESOURCES;i++)
    {
        if(res[i].procalloc[proc]>0)
        {
            if(releaseresources(proc,i,res[i].procalloc[proc])!=0)
            {
                perror("Unable to release resources when terminating process");
                abnormalterm();
            }
        }
        res[i].procreq[proc]=0;
    }
    proctable[proc].pid=-1;
    proctable[proc].reqres=-1;
    proctable[proc].reqnumres=0;
}

// Check if the request for process pnum is less than or equal to available vector
bool req_lt_avail(int * avail, int pnum)
{
    int i=0;
    for(;i<RESOURCES;i++)
        if(res[i].procreq[pnum]>avail[i])
            break;
    return (i==RESOURCES);
}

bool deadlock()
{
    int work[RESOURCES];       
    bool finish[CONCURRENTTASKS];
    
    int i;
    for(i=0;i<RESOURCES;i++)
        work[i]=res[i].available;
    for(i=0;i<CONCURRENTTASKS;i++)
        finish[i]=false;
    
    int p=0;
    for(;p<CONCURRENTTASKS;p++)   //for each processes
    {
        if(finish[p]) continue;
        if(req_lt_avail(work,p))
        {
            finish[p]=true;
            for(i=0;i<RESOURCES;i++)
                work[i]+=res[i].procalloc[p];
            p=-1;
        }
    }

    bool d=false;
    for(p=0;p<CONCURRENTTASKS;p++)   
    {
        if(!finish[p])
        {
            d=true;
            break;
        }
    }
    
    if(d)
    {
        fprintf(stderr,"Processes: ");
        fprintf(logfile,"Processes: ");
        for(p=0;p<CONCURRENTTASKS;p++)   //for each processes
        {
            if(!finish[p])
            {
                fprintf(stderr," P%d ",p);
                fprintf(logfile," P%d ",p);
                enqueue(deadlockq,p);
                deadlockprocs++;
            }
        }
        fprintf(stderr," are in deadlock\n");
        fprintf(logfile," are in deadlock\n");
    }
    
    return d;
}

void help()
{
	printf("\nThis program is a resouce management simulator. The options in this program are h and v , h is an option for help and v is an option for verbose");
	printf("\nDuring this simulation children are spawned an then randomly choose reasources they need. The OSS then decides if the child can have the reasource it wants. ");
	printf("\nThe OSS also checks if their is a deadlock among the reasources being used whenever a reasource is being granted to a child proccess or taken away from a child process.\n");
}
int main(int argc,char**argv)
{
	//variable declaration
	int i=0;
	int j=0;
	int option;
	int s=100;
	int verbose=0;
	char* logfilename="logfile.log";
	struct timespec tpstart;
	char timestr[31];
	if(clock_gettime(CLOCK_REALTIME,&tpstart)==-1)
	{
		perror("Failed to get starting time");
		return 1;
	}
	timespec2str(timestr,tpstart);
	fprintf(stderr,"Beginning the program: %s\n", timestr);
	//get options from command line
	while((option=getopt(argc,argv,"h"))!=-1)
	{
		switch(option)
		{
			case 'h':
				help();
				break;
			case 'v':
				verbose=1;
				break;
			default:
				perror("Usage: Unknown option ");
				exit(EXIT_FAILURE);
		}
	}
	fprintf(stderr, "Usage: %s \n",argv[0]);
	if(setuptimer(s) ==-1)
	{
		perror("Failed to set up the timer");
		return 1;
	}
	signal(SIGALRM,catchtimerinterrupt);
	signal(SIGINT,catchctrlcinterrupt);

	if((logfile=fopen(logfilename,"w"))==NULL)
	{
		perror("Failed to open logfile");
		return 1;
	}

	//get shared memory
	key_t sckey;
	if((sckey=ftok(".",13))==-1)
	{
		perror("Failed to return system clock key");
		abnormalterm();
	}
	if((scid=shmget(sckey,sizeof(systemclock_t),PERM |IPC_CREAT))==-1)
	{
		perror("Failed to create shared memory segment for system clock");
		abnormalterm();
	}
	if((sc=(systemclock_t*)shmat(scid,NULL, 0))==(void*)-1)
	{	
		perror("Failed to attach shared memory segment for system clock");
		if(shmctl(scid,IPC_RMID,NULL)==-1)
			perror("Failed to remove shared memory for system clock");
		abnormalterm();
	}
	key_t reskey;
	if((reskey=ftok(".",27))==-1)
	{
		perror("Failed to return resources key");
		abnormalterm();
	}
	if((resid=shmget(reskey,sizeof(resourcedesc_t)*RESOURCES,PERM |IPC_CREAT))==-1)
	{
		perror("Failed to create shared memory segment for resources");
		abnormalterm();
	}
	if((res=(resourcedesc_t*)shmat(resid,NULL, 0))==(void*)-1)
	{	
		perror("Failed to attach shared memory segment for resources");
		if(shmctl(resid,IPC_RMID,NULL)==-1)
			perror("Failed to remove shared memory for resources");
		abnormalterm();
	}
	srand(time(0));
	sc->sec=0;
	sc->nsec=0;
	for(i=0;i<CONCURRENTTASKS;i++)
	{
	    proctable[i].pid=-1;
	    proctable[i].reqres=-1;
	    proctable[i].reqnumres=0;
	}
	
	// create clock access semaphore
	if((clockaccess=sem_open("/clockaccess",SEM_FLAGS,SEM_PERMS,1))==SEM_FAILED)
	{
	    sem_unlink("/clockaccess");
	    if((clockaccess=sem_open("/clockaccess",SEM_FLAGS,SEM_PERMS,1))==SEM_FAILED)
	    {
	        perror("Failed to create clock access semaphore");
	        abnormalterm();
	    }
	}
	
	// set up resources
	for(i=0;i<RESOURCES;i++)
	{
	    res[i].resourceid=i;
	    res[i].total=randomint(1,10);
	    res[i].available=res[i].total;
	    res[i].allocated=0;
	    if(randomint(1,100)<=20)
	    {
	        res[i].shareable=1;
	    }
	    else
	    {
	        res[i].shareable=0;
	    }
	    for(j=0;j<CONCURRENTTASKS;j++)
	    {
	        res[i].procalloc[j]=0;
	        res[i].procreq[j]=0;
	    }
	}
	printresources();
	
	//get message queue
	key_t msgkey;
	if((msgkey=ftok(".",32))==-1)
	{
		perror("Failed to return massage queue key");
		abnormalterm();
	}
	if((msgid=msgget(msgkey, PERM|IPC_CREAT))==-1)
	{
		perror("Failed to create message queue");
		abnormalterm();
	}
	
	//set up wait queue
	struct Queue* waitq = createQueue();

	mymsg_t mymsg;
	char *msg="Done";
	int msglen=strlen(msg);
	int numProcesses=0;
	int totalProcesses=0;
	int nextpid=0;
	int next=0;
	int item=0;
	int notdone=1;
	int loop=0;
	i=0;
	int sec=0;
	int nsec=0;
	int x=0;
	char numString[10];
	char* token;
	char* requesttype;
	int reqproc;
	int reqres;
	int reqnumres;
	int sendmessage;
	int numgrants=0;
	int numWaiting=0;
	int d;
	int numnomsg=0;
	int killedproc=-1;

	//start cycling
	while(notdone)
	{
		if(numProcesses<CONCURRENTTASKS && totalProcesses<TOTALTASKS)
		{
			//start porcess
			nextpid=findnextpid();
			numProcesses++;
			totalProcesses++;
			childpid=fork();
			if(childpid==-1)
			{
				perror("Failed to fork user process");
				abnormalterm();
			}
			if(childpid==0)
			{
				sprintf(numString,"%d",nextpid);
				execl("./user_proc","user_proc",numString,NULL);
				perror("Failed to exec to user");
				abnormalterm();
			}
			//parent code
			if(loglen<maxLogLen && verbose==1)
			{
				fprintf(logfile,"Master has created process %d(%d)\n",nextpid,childpid);
				loglen++;
			}
			proctable[nextpid].pid=childpid;
			proctable[nextpid].reqres=-1;
			proctable[nextpid].reqnumres=0;
			sleep(1);
		}
		
		if(!isEmpty(waitq))
		{
			numWaiting=size(waitq);
			//check each waiting processes
			for(x=0;x<numWaiting;x++)
			{
				item=dequeue(waitq);
    			if(allocateresources(item,proctable[item].reqres,proctable[item].reqnumres)!=-1)
    			{
        			if(loglen<maxLogLen)
    	    		{
    		    	    fprintf(logfile,"R%02d:%03d has been allocated to process %d from wait queue\n",proctable[item].reqres,
    		    	                proctable[item].reqnumres,item);
    		    	    loglen++;			
    	    		}
    		    	fprintf(stderr,"R%02d:%03d has been allocated to process %d from wait queue\n",proctable[item].reqres,
    		    	                proctable[item].reqnumres,item);    	    	
    		    	numgrants++;
    	    		numgrantedlater++;
    	    		res[proctable[item].reqres].procreq[item]=0;
    	    		proctable[item].reqres=-1;
    	    		proctable[item].reqnumres=0;
            		memcpy(mymsg.mtext,msg,msglen);
            		mymsg.mtype=proctable[item].pid;
            		if (msgsnd(msgid,&mymsg,msglen,0)==-1)
            		{
            			perror("Failed to send message");
            			return 1;
            		}
        			if(loglen<maxLogLen && verbose==1)
        			{    		
        			    fprintf(logfile,"Master sent message to process %d(%d)\n",item,proctable[item].pid);
        			    loglen++;
        			}
    			}
				else
				{
					enqueue(waitq, item);
				}
			}
		}
        
    	deadlockq = createQueue();
    	deadlockprocs=0;
        numdeadlockrun++;
        if(deadlock())
        {
    		if(loglen<maxLogLen)
    		{
    			fprintf(logfile,"Master: Deadlock detected\n");
    			fprintf(stderr,"Master: Deadlock detected\n");
    			loglen++;
    		}
    		numdeadlocks++;
    		numdeadlockprocs+=deadlockprocs;
    		numtermdeadlock++;
    		killedproc=dequeue(deadlockq);
    		if(loglen<maxLogLen)
    		{
        		fprintf(logfile,"Process %d in deadlock is being killed\n",killedproc);
        		fprintf(stderr,"Process %d in deadlock is being killed\n",killedproc);
    			loglen++;
    		}
    		kill(proctable[killedproc].pid,SIGTERM);
    		terminateproc(killedproc);
			numProcesses--;
			// remove killed process from wait queue
			numWaiting=size(waitq);
			for(x=0;x<numWaiting;x++)
			{
				item=dequeue(waitq);
    			if(item==killedproc)
    			{
        			if(loglen<maxLogLen)
    	    		{
    		    	    fprintf(logfile,"Killed process %d removed from wait queue\n",killedproc);
    		    	    loglen++;			
    	    		}
		    	    fprintf(stderr,"Killed process %d removed from wait queue\n",killedproc);
    			}
				else
				{
					enqueue(waitq, item);
				}
			}
        }

		//wait for message from user process
        sendmessage=1;
        requesttype="";
        reqproc=-1;
        reqres=-1;
        reqnumres=0;
		int msgsize;
		if((msgsize=msgrcv(msgid,&mymsg,100,getpid(),IPC_NOWAIT))==-1)
		//if((msgsize=msgrcv(msgid,&mymsg,100,getpid(),0))==-1)
		{
		    if(errno==ENOMSG)
		    {
		        sendmessage=0;
		        numnomsg++;
		    }
		    else
		    {
			    perror("Failed to recieve message");
			    return 1;
		    }
		}
		else
		{
				    numnomsg=0;
            requesttype = strtok(mymsg.mtext, ",");
            reqproc = atoi(strtok(0, ","));
            reqres = atoi(strtok(0, ","));
            reqnumres = atoi(strtok(0, ","));
		}

		if(strcmp(requesttype,"Request")==0)
		{
			if(loglen<maxLogLen)
			{
			    fprintf(logfile,"Master has detected that process %d is requesting R%02d:%03d\n",reqproc,reqres,reqnumres);
			    loglen++;
			}			    
			if(allocateresources(reqproc,reqres,reqnumres)!=-1)
			{
    			if(loglen<maxLogLen)
	    		{
		    	    fprintf(logfile,"R%02d:%03d has been allocated to process %d\n",reqres,reqnumres,reqproc);
                    loglen++;			
	    		}
		    	fprintf(stderr,"R%02d:%03d has been allocated to process %d\n",reqres,reqnumres,reqproc);
		    	numgrants++;
	    		numgrantednow++;
			}
			else
			{
    			if(loglen<maxLogLen && verbose==1)
	    		{
	    		    fprintf(logfile,"R%02d:%03d could not be allocated to process %d\n",reqres,reqnumres,reqproc);
			        fprintf(logfile,"Process %d has been placed on the wait queue\n",reqproc);
			        loglen=loglen+2;
	    		}
    		    fprintf(stderr,"R%02d:%03d could not be allocated to process %d\n",reqres,reqnumres,reqproc);
	    		enqueue(waitq,reqproc);
	    		proctable[reqproc].reqres=reqres;
	    		proctable[reqproc].reqnumres=reqnumres;
	    		res[reqres].procreq[reqproc]+=reqnumres;
	    		sendmessage=0;
			}
		}
		else if(strcmp(requesttype,"Release")==0)
		{
			if(loglen<maxLogLen && verbose==1)
			{
    			fprintf(logfile,"Master has detected that process %d is releasing R%02d:%03d\n",reqproc,reqres,reqnumres);
    			loglen++;
			}
			releaseresources(reqproc,reqres,reqnumres);
			if(loglen<maxLogLen && verbose==1)
			{
    			fprintf(logfile,"R%02d:%03d has been released by process %d\n",reqres,reqnumres,reqproc);
    			loglen++;
			}
			fprintf(stderr,"R%02d:%03d has been released by process %d\n",reqres,reqnumres,reqproc);
		}
		else if(strcmp(requesttype,"Terminate")==0)
		{
			if(loglen<maxLogLen && verbose==1)
			{			
			    fprintf(logfile,"Master has detected that process %d is terminating\n",reqproc);
			    loglen++;
			}
		    fprintf(stderr,"Master has detected that process %d is terminating\n",reqproc);
			terminateproc(reqproc);
			numProcesses--;
			numtermsuccess++;
			sendmessage=0;
		}
		else if(strcmp(requesttype,"")==0)
		{
		    sleep(1);
    }
		else
		{
			perror("Master has detected an unknown request.");
			abnormalterm();
		}
        
        // send message back to user
        if(sendmessage==1)
        {
    		memcpy(mymsg.mtext,msg,msglen);
    		mymsg.mtype=proctable[reqproc].pid;
    		if (msgsnd(msgid,&mymsg,msglen,0)==-1)
    		{
    			perror("Failed to send message");
    			return 1;
    		}
			if(loglen<maxLogLen && verbose==1)
			{    		
			    fprintf(logfile,"Master sent message to process %d(%d)\n",reqproc,proctable[reqproc].pid);
			    loglen++;
			}
        }
        
		//advance clock for cycle
		sec=1;
		nsec=randomint(0,1000);
    sem_wait(clockaccess);
		advanceclock(sc,sec,nsec);
		sem_post(clockaccess);
		if(loglen<maxLogLen && verbose==1)
		{
			fprintf(logfile,"Master: Current system clock time is %d:%06d\n",sc->sec,sc->nsec);
			loglen++;
		}

        if(numgrants%20==0 && verbose==1)
        {
            printresources();
        }

        if(numnomsg>500000)
        {
            fprintf(stderr,"Master has stopped receiving messages\n");
            numWaiting=size(waitq);
            fprintf(stderr,"Size of wait queue: %d\n",numWaiting);
            notdone=0;
        }

		if(totalProcesses>=TOTALTASKS)
		{
			notdone=0;
		}
		loop++;
	}
	cleanup();
	return 0;
}
