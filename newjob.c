#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"
#define DEBUG


int jobid=0;
int siginfo=1;
int fifo;
int globalfd;

struct waitqueue *head[3]={NULL,NULL,NULL};
struct waitqueue *next=NULL,*current =NULL;

/* µ÷¶È³ÌÐò */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
	int  count = 0;
	bzero(&cmd,DATALEN);
	if((count=read(fifo,&cmd,DATALEN))<0)
		error_sys("read fifo failed");


#ifdef DEBUG
	printf("scheduler!\n");
#endif

	updateall();

	switch(cmd.type){
	case ENQ:

		do_enq(newjob,cmd);

		break;
	case DEQ:

		do_deq(cmd);
		break;
	case STAT:

		do_stat(cmd);
		break;
	default:
		break;
	}

	/* Ñ¡Ôñ¸ßÓÅÏÈ¼¶×÷Òµ */
	if(next==NULL)
		next=jobselect();
	else{
		next=next;
	}
	/* ×÷ÒµÇÐ»» */
	jobswitch();
}

int allocjid()
{
	return ++jobid;
}
void putback(int k){
	struct waitqueue * p;
	current->job->run_time=0;
	kill(current->job->pid,SIGSTOP);

	current->job->wait_time = 0;
	current->job->state = READY;
	if(head[k]){
		for(p = head[k]; p->next != NULL; p = p->next);
			p->next = current;	
	}else{
			head[k] = current;
	}

    current=NULL;
}
void addpri(int k,struct waitqueue * p,struct waitqueue * prev)
{
	struct waitqueue *q;
	int i;
	p->job->wait_time=0;

	i=p->job->curpri;
	if(prev)
		prev->next=p->next;
	else
	{
		head[k]=p->next;
		p->next=NULL;
	}
	if(head[i])
	{
		for(q=head[i];q!=NULL;q=q->next);
			q->next = p;
			p->next = NULL;
	}else
	{
		head[k]= p;
	}

}
void check(){
	int runtime=current->job->run_time;
    if(current->job->curpri==0){
    	if(runtime==5&&current->job->state!=DONE){
    		putback(0);
    	}
    }

    else
    if(current->job->curpri==1){
    	if(runtime==2&&current->job->state!=DONE){
    		current->job->curpri--;
    		putback(0);
    	}
    }
    else

    if(current->job->curpri==2){
    	if(runtime==1&&current->job->state!=DONE){
    		current->job->curpri--;
    		putback(1);
    	}
    }
}
void updateall()
{
	struct waitqueue *p;
	struct waitqueue *prev=NULL;
    int k=0;
	/* ¸üÐÂ×÷ÒµÔËÐÐÊ±¼ä */
	if(current){
		current->job->run_time += 1; /* ¼Ó1´ú±í1000ms */
		check();
	}

     
	/* ¸üÐÂ×÷ÒµµÈ´ýÊ±¼ä¼°ÓÅÏÈ¼¶ */
	for(k=0;k<3;k++){
		for(p = head[k]; p != NULL; p = p->next){
			p->job->wait_time += 1000;
			if(p->job->wait_time >= 10000 && p->job->curpri < 2){
				p->job->curpri++;
				p->job->wait_time = 0;
				addpri(k,p,prev);//yi zhi xin de dui lie
				if(prev)
				{
					p=prev->next;
				}else
				{
					p=head[k];
				}
			}
			prev=p;
		}
	}
	//fang ru xin de dui lie

}

struct waitqueue* jobselect()
{
	struct waitqueue *select;
	int tmp;
	int k=2;
	select = NULL;
	if(current!=NULL){
		tmp=current->job->curpri;
	}else{
		tmp=-1;
	}

	for(k=2;k>tmp;k--){
		if(head[k]){
			/* ±éÀúµÈ´ý¶ÓÁÐÖÐµÄ×÷Òµ£¬ÕÒµ½ÓÅÏÈ¼¶×î¸ßµÄ×÷Òµ */
			select=head[k];

			head[k]=head[k]->next;
			select->next=NULL;
			break;
		}
	}
	return select;
}

void jobswitch()
{
	struct waitqueue *p;
	int i;
	int pri;
	if(current && current->job->state == DONE){ /* µ±Ç°×÷ÒµÍê³É */
		/* ×÷ÒµÍê³É£¬É¾³ýËü */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* ÊÍ·Å¿Õ¼ä */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) /* Ã»ÓÐ×÷ÒµÒªÔËÐÐ */

		return;
	else if (next != NULL && current == NULL){ /* meici yunxing wan */
		printf("next job!\n");
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time=0;
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ /* ÇÐ»»×÷Òµ */

		printf("switch to Pid: %d stop cmd:%s\n",next->job->pid,current->job->cmdarg[0]);
		kill(current->job->pid,SIGSTOP);
		pri=current->job->curpri;
		current->job->wait_time = 0;
		current->job->state = READY;

		/* ·Å»ØµÈ´ý¶ÓÁÐ */
		if(head[pri]){
			for(p = head[pri]; p->next != NULL; p = p->next);
			p->next = current;
		}else{
			head[pri] = current;
		}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;

		kill(current->job->pid,SIGCONT);
		return;
	}else{ /* next == NULLÇÒcurrent != NULL£¬²»ÇÐ»» */
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;

	switch (sig) {
case SIGVTALRM: /* µ½´ï¼ÆÊ±Æ÷ËùÉèÖÃµÄ¼ÆÊ±¼ä¸ô */
	scheduler();
	return;
case SIGCHLD: /* ×Ó½ø³Ì½áÊøÊ±´«ËÍ¸ø¸¸½ø³ÌµÄÐÅºÅ */
	ret = waitpid(-1,&status,WNOHANG);
	if (ret == 0)
		return;
	if(WIFEXITED(status)){
		current->job->state = DONE;
		printf("normal termation, exit status = %d\n",WEXITSTATUS(status));
	}else if (WIFSIGNALED(status)){
		printf("abnormal termation, signal number = %d\n",WTERMSIG(status));
	}else if (WIFSTOPPED(status)){
		printf("child stopped, signal number = %d\n",WSTOPSIG(status));
	}
	return;
	default:
		return;
	}
}

void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)
{
	struct waitqueue *newnode,*p;
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	int pri;
	sigset_t zeromask;

	sigemptyset(&zeromask);
#ifdef DEBUG
	printf("do_Enq\n");
	#endif

	/* ·â×°jobinfoÊý¾Ý½á¹¹ */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	pri=newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}

	arglist[i] = NULL;


#ifdef DEBUG
	printf("do_Enq 1.1\n");
	#endif
	/*ÏòµÈ´ý¶ÓÁÐÖÐÔö¼ÓÐÂµÄ×÷Òµ*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;


	//if qiangzhan ,fang dou bufang
	if(current!=NULL&&newnode->job->curpri>current->job->curpri){
		next=newnode;
	}else{
	#ifdef DEBUG
	printf("do_Enq 1.5\n");
	#endif
	if(head[pri])
	{
		for(p=head[pri];p->next != NULL; p=p->next);
			p->next =newnode;
	}else
		head[pri]=newnode;
	}
	/*Îª×÷Òµ´´½¨½ø³Ì*/
	#ifdef DEBUG
	printf("do_Enq 2\n");
	#endif
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();
		/*×èÈû×Ó½ø³Ì,µÈµÈÖ´ÐÐ*/
		printf("raise stop\n");
		raise(SIGSTOP);


		/*¸´ÖÆÎÄ¼þÃèÊö·ûµ½±ê×¼Êä³ö*/
		dup2(globalfd,1);
		/* Ö´ÐÐÃüÁî */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		newjob->pid=pid;
		waitpid(-1,NULL,WUNTRACED);

	}

}

void do_deq(struct jobcmd deqcmd)
{
	int deqid,i;
	int k;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n",deqid);
#endif

	/*current jodid==deqid,ÖÕÖ¹µ±Ç°×÷Òµ*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* »òÕßÔÚµÈ´ý¶ÓÁÐÖÐ²éÕÒdeqid */
	#ifdef DEBUG
	printf("2deq jid %d\n",deqid);
#endif

		select=NULL;
		selectprev=NULL;
		for(k=0;k<3;k++){
			if(head[k]){
				for(prev=head[k],p=head[k];p!=NULL;prev=p,p=p->next){
						#ifdef DEBUG
							printf("3deq jid %d %d %d\n",deqid,p->job->jid,k);
						#endif
					if(p->job->jid==deqid){
						select=p;
						selectprev=prev;
						
						if(select==selectprev)
							head[k]=head[k]->next;
						else
							selectprev->next=select->next;

							#ifdef DEBUG
							printf("1DEBUG FOUND!!!\n");
							#endif
						break;
					}

				}
				if(select){
					
					break;
				}
			}
		}
		if(select){
			#ifdef DEBUG
				printf("DEBUG FOUND!!!\n");
			#endif
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
}

void do_stat(struct jobcmd statcmd)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	int k=2;
	/*
	*´òÓ¡ËùÓÐ×÷ÒµµÄÍ³¼ÆÐÅÏ¢:
	*1.×÷ÒµID
	*2.½ø³ÌID
	*3.×÷ÒµËùÓÐÕß
	*4.×÷ÒµÔËÐÐÊ±¼ä
	*5.×÷ÒµµÈ´ýÊ±¼ä
	*6.×÷Òµ´´½¨Ê±¼ä
	*7.×÷Òµ×´Ì¬
	*/

	/* ´òÓ¡ÐÅÏ¢Í·²¿ */
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\tPri\n");
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\tCurrent\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}
    for(k=2;k>-1;k--){
		for(p=head[k];p!=NULL;p=p->next){
			strcpy(timebuf,ctime(&(p->job->create_time)));
			timebuf[strlen(timebuf)-1]='\0';
			printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\n",
				p->job->jid,
				p->job->pid,
				p->job->ownerid,
				p->job->run_time,
				p->job->wait_time,
				timebuf,
				"READY",k);
		}
	}
}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf;
	struct sigaction newact,oldact1,oldact2;

	if(stat("/tmp/server",&statbuf)==0){
		/* Èç¹ûFIFOÎÄ¼þ´æÔÚ,É¾µô */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)
		error_sys("mkfifo failed");
	/* ÔÚ·Ç×èÈûÄ£Ê½ÏÂ´ò¿ªFIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* ½¨Á¢ÐÅºÅ´¦Àíº¯Êý */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);

	/* ÉèÖÃÊ±¼ä¼ä¸ôÎª1000ºÁÃë */
	interval.tv_sec=1;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
	close(globalfd);
	return 0;
}
