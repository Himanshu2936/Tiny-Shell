/* 
 * tsh - A tiny shell program with job control
 * 
 * <Himanshu Agarwal>
 * <201501218> <himanshu2936@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/******************************************************
 ** My Own Defined Function
******************************************************/
pid_t Fork(void){
	pid_t pid;
	if ((pid = fork()) < 0)								/*Function to fork a child process and check for frok error.*/
		unix_error("Error in fork");					/*returns pid*/
	return pid;
}
/*****************************************************/
void execute(char **argv){
	if(execvp(*argv,argv)<0){
		printf("%s: Command not found\n", argv[0]);		/*Function to execute the requested executable file using execvp*/
		exit(1);										/*with checking error for exec functions*/
	}
}
/****************************************************/
void printjob(pid_t pid,char *cmdline){
struct job_t *temp;										/*Function to print pid and jid of a job from job table*/
temp=getjobpid(jobs,pid);								/*used in do_fgbg*/
printf("[%d] (%d) %s",temp->jid,temp->pid,cmdline);
}
/****************************************************/
int countjobs(struct job_t *jobs){
	int i=0,count=0;
	for (i = 0; i < MAXJOBS; i++) {						/*function to count the total stopped job in job table*/
		if (jobs[i].pid != 0) {	
            if(jobs[i].state==ST)						/*used to check if there are any background jobs before quit*/
			count++;									/*used when user enters quit command*/
		}
	}
	return count;
}
/******************************************************

******************************************************/

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS];									//command line's argument's array
    sigset_t signal_set;
    sigemptyset(&signal_set);
    if(sigaddset(&signal_set,SIGCHLD)!=0)
    	unix_error("Error in Signal Bloking");
    int result_parseline = parseline(cmdline,argv);			//calling parseline to parse the cmdline
    if (argv[0]==NULL)
    	return;
    int result_builtin_cmd=builtin_cmd(argv);				//calling builtin_cmd to check if the command was a built in command
    if(result_builtin_cmd==1)
    	return;
    else{
    	sigprocmask(SIG_BLOCK,&signal_set,NULL);			//if not a builtin command then create a fork child and execute the file
    	pid_t pid=Fork();									//calling Fork to create a new child
    	if(pid==0){
    		setpgrp();
    		sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    		execute(argv);									//calling execute to execute the requested program
    	}
    	else{
    		if(result_parseline==1)
    			addjob(jobs,pid,BG,cmdline);
    		else 
    			addjob(jobs,pid,FG,cmdline);				//handling bg and fg jobs
    		sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    		if(result_parseline==1){
    			printjob(pid,cmdline);						//printing jif pid of a background requested job
    			return;			
    		}
    		waitfg(pid);
    		return;
    	}
    }
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    if(!strcmp(argv[0],"quit")){
    	int mjobs=countjobs(jobs);
    	if(mjobs!=0){						//check before quit if there are any background jobs still stopped using countjob function
    			if(mjobs==1)
    			printf("There is Still %d background job stopped...\nCan't Quit Now!\n",mjobs);
    		else
    			printf("There are Still %d background jobs stopped...\nCan't Quit Now!",mjobs);
    	}
    	else 
    		exit(0);
    	return 1;
    }
    if(!strcmp(argv[0],"jobs")){
    	listjobs(jobs);
    	return 1;
    }
    if(!strcmp(argv[0],"fg")){
    	do_bgfg(argv);
    	return 1;
    }
    if(!strcmp(argv[0],"bg")){
    	do_bgfg(argv);
    	return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    int isjob=0;
    if(argv[1]==NULL){
    	printf("%s command requires PID or %%jobid argument\n",argv[0]);
    	return;
    }
    else{
    	if(argv[1][0]=='%'){
    		argv[1][0]='0';
    		isjob=1;
    	}																		//it's the error checking part for fg bg command
    	int atoi_value=atoi(argv[1]);
    	if(!atoi_value){
   			printf("%s: argument must be a PID or %%jobid\n",argv[0] );
   			return;
   		}
   		struct job_t *resultjob=getjobjid(jobs,atoi_value);
    	if(resultjob==NULL){
    		if(isjob)
   				printf("(%d): No such job\n",atoi_value);
   			else 
   				printf("(%d): No such process\n",atoi_value);
    		return;
    	}
    	else{
    		if(!strcmp(argv[0],"bg")){
   				if(resultjob->state==ST){										//here is the implementation for fg bg
    				resultjob->state=BG;										//with correct pid and jid's
    				int a=resultjob->pid;										//this block is for bg command
    				printf("[%d] (%d) ", resultjob->jid, resultjob->pid);
					printf("%s", resultjob->cmdline);
    				kill(-a,SIGCONT);
   				}
    		}
    		else{
    			if(resultjob->state==ST){
    				resultjob->state=FG;
    				int a=resultjob->pid;
    				kill(-a,SIGCONT);											//this block is for fg command
    				waitfg(a);
    			}
    			if(resultjob->state==BG){
    				resultjob->state=FG;
    				int a=resultjob->pid;
    				waitfg(a);
   				}
   			}
    	}	
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    struct job_t *temp;															//waitfg function wait until the job is in foreground
    temp=getjobpid(jobs,pid);
    while(temp->state==FG){
    	sleep(1);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    int stat,signal;
    pid_t pid;
    while((pid=waitpid(-1,&stat,WUNTRACED|WNOHANG))>0){								//sigchld handler to reap the terminated process and
    	if(WIFEXITED(stat)){														//check their termination cause and delete them from
    		deletejob(jobs,pid);													//job table
    	}
    	else if(WIFSIGNALED(stat)){   		
    		signal=WTERMSIG(stat);
    		printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,signal);
    		deletejob(jobs,pid); 
    	}
    	else if(WIFSTOPPED(stat)){													//check if a child is stopped then set its state to ST
    		struct job_t *temp;														//and print the appropriate messege
    		temp=getjobpid(jobs,pid);
    		temp->state=ST;
    		signal=WSTOPSIG(stat);
    		printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,signal);
    	}
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    pid_t pid = fgpid(jobs);			//sigint handler catches the signal and transfer it to all foreground process
    if(pid!=0){
    	pid=-pid;
    	kill(pid,sig);					//giving -ve pid means send signal to process group with abs(pid) pid 
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t pid = fgpid(jobs);			//sigstp handler catches the signal and transfer it to all foreground process
    if(pid!=0){
    	pid=-pid;						//giving -ve pid means send signal to process group with abs(pid) pid					
    	kill(pid,sig);
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}