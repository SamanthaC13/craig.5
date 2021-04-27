#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<time.h>
#include<signal.h>
#include<sys/time.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<math.h>
#include<semaphore.h>
#define PERM (S_IRUSR|S_IWUSR)
#define TERMINATIONPERCENT 10
#define B 3
#define RESOURCES 10


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

typedef struct {
	long mtype;
	char mtext[100];
}mymsg_t;

int randomint(int lower,int upper)
{
	int num=(rand()%(upper-lower+1))+lower;
	return num;
}

void advanceclock(systemclock_t *sc, int addsec,int addnsec)
{
	sc->sec=(sc->sec+addsec)+((sc->nsec+addnsec)/1000000);
	sc->nsec=(sc->nsec+addnsec)%1000000;
}

int main(int argc,char**argv)

{
	systemclock_t *sc;
	resourcedesc_t *res;
	int scid,resid;
	sem_t *clockaccess;
	struct timespec tpchild;
	int id;
	int i;
	int alloc=0;
	int logicalpid=atoi(argv[1]);
  char* requesttype;
  char requeststring[100];
  int resource;
  int numresources;
  int timebound;
	//fprintf(stderr, "Hello from user process! My number is %d(%d)\n",logicalpid,getpid());
	
	//get shared memory
	key_t sckey;
	if((sckey=ftok(".",13))==-1)
	{
		perror("User process failed to get system clock key");
		return 1;
	}
	if((scid=shmget(sckey,sizeof(systemclock_t),PERM))==-1)
	{
		perror("User process failed to get shared memory segment for system clock");
		return 1;
	}
	if((sc=(systemclock_t *)shmat(scid,NULL,0))==(void*)-1)
	{
		perror("User process failed to attach shared memory segment for system clock");
		if(shmctl(scid,IPC_RMID,NULL)==-1)
			perror("User process failed to remove memory segment for system clock");
		return 1;
	}
	//fprintf(stderr,"USER: Current system time is %d:%06d\n",sc->sec,sc->nsec);

	key_t reskey;
	if((reskey=ftok(".",27))==-1)
	{
		perror("User process failed to get resources key");
		return 1;
	}
	if((resid=shmget(reskey,sizeof(resourcedesc_t)*RESOURCES,PERM))==-1)
	{
		perror("User process failed to get shared memory segment for resources");
		return 1;
	}
	if((res=(resourcedesc_t *)shmat(resid,NULL,0))==(void*)-1)
	{
		perror("User process failed to attach shared memory segment for resources");
		if(shmctl(resid,IPC_RMID,NULL)==-1)
			perror("User process failed to remove memory segment for resources");
		return 1;
	}
	srand(getpid());
	timebound=randomint(0,B);

	//get access to message queue
	key_t msgkey;
	int msgid;
	if((msgkey=ftok(".",32))==-1)
	{
		perror("Failed to return message queue key");
		return 1;
	}
	if((msgid=msgget(msgkey, PERM))==-1)
	{
		perror("Failed to get message queue");
		return 1;
	}

	// get clock access semaphore
	if((clockaccess=sem_open("/clockaccess",0))==SEM_FAILED)
	{
        perror("Failed to open clock access semaphore");
        return 1;
	}
	
	int size;
	int sec=0;
	int nsec=0;
	mymsg_t mymsg;
	int notdone=1;
	while(notdone)
	{
	    sleep(timebound);
		//Determine if process will terminate
		if(randomint(1,100)<=TERMINATIONPERCENT)
		{	
			notdone=0;
		}
		
        // check if any resources allocated
        alloc=-1;
        for(i=0;i<RESOURCES;i++)
        {
            if(res[i].procalloc[logicalpid]>0)
            {
                alloc=i;
                break;
            }    
        }
        if(notdone==0)
        {
            // terminating
            requesttype="Terminate";
            resource=-1;
            numresources=-1;
        }
        else if(randomint(0,2)<=1 || alloc==-1)
        {
            // request a resource
            requesttype="Request";
            resource=randomint(0,RESOURCES-1);
            numresources=randomint(1,res[resource].total-res[resource].procalloc[logicalpid]);
        }
        else
        {
            // release a resource
            requesttype="Release";
            resource=alloc;
            numresources=res[alloc].procalloc[logicalpid];
        }
        sprintf(requeststring,"%s,%d,%d,%d",requesttype,logicalpid,resource,numresources);
        
		//send message
		memcpy(mymsg.mtext,requeststring,100);
		mymsg.mtype=getppid();
		if(msgsnd(msgid,&mymsg,100,0)==-1)
		{
			perror("User process failed to send message");
			return 1;
		}
		//fprintf(stderr,"USER %d: Message sent: %s\n",logicalpid,mymsg.mtext);
		
    	//advance clock 
    	sec=0;
    	nsec=randomint(0,500);
        sem_wait(clockaccess);
    	advanceclock(sc,sec,nsec);
    	sem_post(clockaccess);
    	//fprintf(stderr,"User: Current system clock time is %d:%06d\n",sc->sec,sc->nsec);

		//receive message
		memcpy(mymsg.mtext,"",100);
		if((size=msgrcv(msgid,&mymsg,100,getpid(),0))==-1)
		{
			perror("User process failed to receive message");
			return 1;
		}
		//fprintf(stderr,"USER %d: Message received: %s\n",logicalpid,mymsg.mtext);
	}


			
	if((sem_close(clockaccess))==-1)
	{
	    perror("Failed to close clock access semaphore in user");
	    return 1;
	}
	
	fprintf(stderr,"User process %d is completing\n",logicalpid);
	return 0;
}
