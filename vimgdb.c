#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <argp.h>
#include <pty.h>
#include <signal.h>
#include <regex.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>

static void display_instruction_notes(void);
static void create_pipes(void);
static void start_gdb(void);
static void start_filters(void);
static void clean_up(void);
static void set_input_mode (void);
static void filter_gdbout(void);
static void filter_stdin(void);
static void filter_vimout(void);
static void parse_args(int argc, char **argv);
static void send_to_gdb(const char *command);
static void send_to_vim(const char *command, ...);
static void create_vim_pipe(void);
static void start_vim(void);

/* Use this variable to remember original terminal attributes. */
struct termios saved_attributes;
char *cwd; /* current working directory */


/* gdb state info */
int gdb_pty;
pid_t gdb_pid;
char *gdb_args=NULL;

/* vim state info */
char *server;/* This is the name of the vim server we are using default GVIM */
char vim_pipe_file[19];/* pipe for vim to talk to us through */

/* 
 * key sequence that will get us into normal mode and back to insert 
 * (if thats what you want.)
 *
 */
#define START_COMMAND   "<C-\\><C-N>"
#define END_COMMAND     ""


int main(int argc, char **argv)
{
    /* parse arguments removing --server= and handling --help */
    parse_args(argc,argv);
    
    /* put terminal into non-canoncial mode */
    set_input_mode();
   
    display_instruction_notes();

    /* fork + run gdb (with pseudo termainal) */
    start_gdb();

    /* create new pipe for talking to vim */
    create_vim_pipe();
    
    /* initialise communication with vim */
    start_vim();
    
    /* check for desired vim server or start new vim */
    start_filters();

}

static void parse_args(int argc, char **argv)
{
    int i;
    
    if((gdb_args=(char *)malloc(1))==NULL)
        exit(EXIT_FAILURE);
    strcpy(gdb_args,"");

    if((server=(char *)malloc(1))==NULL)
        exit(EXIT_FAILURE);
    strcpy(server,"");

    /* I dont like this piece of code :( */
    for(i=1;i<argc;i++)
    {
        if(strncmp(argv[i],"--server=",9)==0 && (strlen(argv[i])-9)>0 && strlen(server)==0)
        {
            server=(char *)realloc(server,strlen(argv[i])-9);
            strcpy(server,argv[i]+9);
        }else if(strcmp(argv[i],"--help")==0){
            system("gdb --help");
            printf("\n\n  --server=SERVER    the vim server that you want to connect to.\n\n");
            exit(EXIT_SUCCESS);
        }else{
            /* add everything else to argv for gdb */
            if((gdb_args=(char *)realloc(gdb_args,strlen(gdb_args)+strlen(argv[i])+2))==NULL)
            {
                fprintf(stderr,"Couldn't alloc memory for gdb parameters\n");
                exit(EXIT_FAILURE);
            }
            strcat(gdb_args," ");
            strcat(gdb_args,argv[i]);
        }
    }

}


/* reset terminal settings */
static void clean_up(void)
{
    tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}



static void set_input_mode(void)
{
    struct termios tattr;
    char *name;

    /* Check if stdin is a terminal. */
    if (!isatty(STDIN_FILENO))
    {
        fprintf (stderr, "Not a terminal.\n");
        exit (EXIT_FAILURE);
    }

    /* Save the terminal attributes so we can restore them later. */
    tcgetattr (STDIN_FILENO, &saved_attributes);
    atexit (clean_up);

    cfmakeraw(&tattr);/* bsd raw mode */

    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}




static void display_instruction_notes(void)
{
    printf("\r\e[22;0m\e[36;1m*****\e[37;1m Welcome to VIMGDB \e[36;1m*****\e[22;0m\n\n");

    printf("\r\e[22;0m\e[32;1m* \e[33;1mYou can use vimgdb here just like you would use gdb.\e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mYou can also use it through a vim server. There is a 'Gdb' menu you can use or keybindings (Which you can lookup using the menu) \e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mBy default it will try to connect to a server called GVIM but you can specify another with --server=SERVER (gvim --serverlist should show you any current servers)\e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mThis gdb session will automatically do a code 'list' whenever a stack frame is shown (e.g. after a 'step' or 'next' instruction) It also highlights your current line in green (assuming your terminal supports color. (sorry if it doesn't))\e[22;0m\n");

    printf("\n\n\r\e[22;0m\e[36;1m*****\e[37;1m _Known problems_ \e[36;1m*****\e[22;0m\n\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mToo many little ones to mention!\e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mCurrently not very configurable (It was a bit of a hack, originally intended for my eyes only.)\e[22;0m\n");
    printf("\r\e[22;0m\e[31;1m* \e[33;1mYou can only set break points where the source file is in the current working directory for VIM. You can change this with :cd /new/cwd \e[22;0m\n");
    printf("\r\e[22;0m\e[31;1m* \e[33;1mYou must have a vimserver running before you start vimgdb. (that just means running gvim usually.)\e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mOnly tested using linux, (slackware/gentoo)\e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mGcc warns about using mktemp.\e[22;0m\n");

    printf("\n\n\r\e[22;0m\e[36;1m*****\e[37;1m _Bugs and Features_ \e[36;1m*****\e[22;0m\n\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mI welcome any bug fixing, code cleaning, feature adding or ideas etc. \e[22;0m\n");
    printf("\r\e[22;0m\e[32;1m* \e[33;1mPlease send patches, flames etc to <rbragg@essex.ac.uk> (Please put vimgdb somewhere in the subject.)\e[22;0m\n");

    printf("\r\n\nPress any key to continue...");
    getc(stdin);
    printf("\n\r");
}




static void start_gdb(void)
{
    char *command=NULL;

    gdb_pid=forkpty(&gdb_pty,NULL,NULL,NULL);

    switch(gdb_pid)
    {
        case 0:
            if((command=(char *)malloc(12 + strlen(gdb_args)))==NULL){
                fprintf(stderr,"Couln't alloc memorry for command string\n");
                exit(EXIT_FAILURE);
            }

            sprintf(command,"exec gdb -f%s",gdb_args);

            execl("/bin/sh", "sh", "-c", command, NULL);

            break;
        case -1:
            printf("ERROR at forkpty()");
            fflush(stdout);
            exit(EXIT_FAILURE);
            break;
        default:
            return;
    }
}


static void create_vim_pipe(void)
{
    strcpy(vim_pipe_file,"/tmp/vimgdb.XXXXXX");
    if(mktemp(vim_pipe_file)==NULL || strlen(vim_pipe_file)==0)
    {
        fprintf(stderr,"Failed to create unique pipe name for vim\n");
        exit(EXIT_FAILURE);
    }
    
    if(mknod(vim_pipe_file,S_IFIFO,0)==-1)
    {
        fprintf(stderr,"Failed to create unique pipe for vim\n");
        exit(EXIT_FAILURE);
    }

    chmod(vim_pipe_file,S_IRUSR|S_IWUSR);/* (600) */

}


static void start_vim(void)
{
/*    send_to_vim(START_COMMAND,":chdir ", cwd, END_COMMAND, NULL);*/
    /*send_to_vim(ESC_TO_NORMAL_KEYS,":call Gdb_Interf_Init(",vim_pipe_file,")", NULL);*/
}



static void start_filters(void)
{
    pid_t filter1_pid;
    pid_t filter2_pid;
    pid_t filter3_pid;
    
    /* filters stdin and forwards to gdb */
    if((filter1_pid=fork())==0){
        filter_stdin();
        exit(EXIT_SUCCESS);
    }
    /* filters gdb output and forwards to stdout */
    if((filter2_pid=fork())==0){
        filter_gdbout();
        exit(EXIT_SUCCESS);
    }
    /* filters vim pipe */
    if((filter3_pid=fork())==0){
        filter_vimout();
        exit(EXIT_SUCCESS);
    }
    cwd=getcwd(NULL,0);
  
    /* Initialise communication channel with vim */
    send_to_vim(START_COMMAND, ":call Gdb_Interface_Init('", vim_pipe_file, "','", cwd, "')<CR>", END_COMMAND, NULL);

    /* make it clear that this gdb session is linked with a vim server */
    send_to_gdb("set prompt (vimgdb");
    send_to_gdb("[");
    send_to_gdb(server);
    send_to_gdb("]) \n");
    
    /* wait for gdb to terminate */
    waitpid(gdb_pid,NULL,0);
        
    kill(filter1_pid,SIGTERM);
    kill(filter2_pid,SIGTERM);
    kill(filter3_pid,SIGTERM);

    send_to_vim(START_COMMAND, ":call Gdb_Interface_Deinit()<CR>", END_COMMAND, NULL);

    exit(EXIT_SUCCESS);
}

static void filter_stdin(void)
{
    int c;

    /* No filtering actually done on stdin at the moment. */
    while(c=getc(stdin))
        write(gdb_pty,&c,1);

    fprintf(stderr,"stdin filter EOF\n");
    fflush(stderr);

}

/* macro to pull out matched sub strings within regular expressions. */
#define SUB_EXPR(X) line_buff+sub_expr[X].rm_so,sub_expr[X].rm_eo-sub_expr[X].rm_so

static void filter_gdbout(void)
{
    char c;
    char line_buff[1000];
    static int buff_ptr=0;
    char * vim_command;
    regex_t break_reg;
    regex_t sframe_reg;
    regex_t clear_reg;
    regex_t line_reg;
    regex_t source_dirs_reg;
    char line_reg_tmp[10];
    static int last_line=-1;
    static int color_enabled=1;
    
    regmatch_t sub_expr[5];

    if((vim_command=malloc(1000))==NULL)
    {
        fprintf(stderr,"Couldn't alloc memory for vim command\n");
        exit(EXIT_SUCCESS);
    }
    
    /* compile our regular expressions (these should be moved somwhere else) */
    if(regcomp(&break_reg,"Breakpoint ([1-9]+) at 0x.*: file ([^,]+), line ([0-9]+).",REG_EXTENDED)!=0)
            printf("Expression failed to compile!");
    if(regcomp(&sframe_reg,"\032\032([^:]*):([0-9]+).*",REG_EXTENDED)!=0)
            printf("Expression failed to compile!");
    if(regcomp(&clear_reg,"Deleted breakpoint ([0-9]+)",REG_EXTENDED)!=0)
            printf("Expression failed to compile!");
    if(regcomp(&source_dirs_reg,"Source directories searched (.*$cdir:$cwd)",REG_EXTENDED)!=0)
            printf("Expression failed to compile!");

    for(;read(gdb_pty,&c,1) && buff_ptr<1000;buff_ptr++)
    {
        if(c!='\n')
        {
            line_buff[buff_ptr]=c;
            
        }else{
            printf("\e[22;0m");
            /*color_enabled=1;*/

            line_buff[buff_ptr]='\0';
            buff_ptr=-1;
            //Match usefull gdb output...

            sprintf(line_reg_tmp,"^%d",last_line-1);
            if(regcomp(&line_reg,line_reg_tmp,REG_EXTENDED)!=0)
                printf("Expression failed to compile!");

            if(regexec(&break_reg,line_buff,4,sub_expr,0)==0)
            {   /* Detect break points being set */
                
                strcpy(vim_command,":call Gdb_Breakpoint(");
                strncat(vim_command,SUB_EXPR(1));/* id */
                strcat(vim_command,",'");
                strncat(vim_command,SUB_EXPR(2));/* file */
                strcat(vim_command,"',");
                strncat(vim_command,SUB_EXPR(3));/* line */
                strcat(vim_command,")<CR>");
                send_to_vim(START_COMMAND, vim_command, END_COMMAND, NULL);
                /*printf("__%s__",vim_command);*/
            }else if(regexec(&clear_reg,line_buff,2,sub_expr,0)==0)
            {   /* Detect cleared break points */
                
                strcpy(vim_command,":call Gdb_ClearBreakpoint(");
                strncat(vim_command,SUB_EXPR(1));/* id */
                strcat(vim_command,")<CR>");
                send_to_vim(START_COMMAND, vim_command, END_COMMAND, NULL);
            }else if(regexec(&sframe_reg,line_buff,3,sub_expr,0)==0)
            {   /* Detect stack frames */
                
                strcpy(vim_command,":call Gdb_DebugStop('");
                strncat(vim_command,SUB_EXPR(1));/* file */
                strcat(vim_command,"',");
                strncat(vim_command,SUB_EXPR(2));/* line */
                strcat(vim_command,")<CR>");
                send_to_vim(START_COMMAND, vim_command, END_COMMAND, NULL);
                
                line_reg_tmp[0]='\0';
                strncat(line_reg_tmp,SUB_EXPR(2));
                last_line=atoi(line_reg_tmp);
                
                send_to_gdb("list ");
                sprintf(line_reg_tmp,"%d",last_line-10);
                send_to_gdb(line_reg_tmp);
                send_to_gdb(",");
                sprintf(line_reg_tmp,"%d",last_line+10);
                send_to_gdb(line_reg_tmp);
                send_to_gdb("\n");

                /* look at help :edit  (++opt,cmd) */
                /*printf("\n__%s__",vim_command);*/
            }else if(regexec(&line_reg,line_buff,3,sub_expr,0)==0)
            {   /* detect current position line */
                printf("\e[1;44m");
                fflush(stdout);
                color_enabled=0;
            }else if(regexec(&source_dirs_reg,line_buff,3,sub_expr,0)==0)
            {
                printf("")
            }
        }

        /* Sorry if this offends you :) */
/*
        if(color_enabled)
        {
            switch(c)
            {                
                case '(': case ')': case ',':
                    printf("\e[1;33m%c\e[22;0m",c);
                    break;
                    
                case '\\': case '/': case '*':
                    printf("\e[1;32m%c\e[22;0m",c);
                    break;
                case '0': case '1': case '2': case '3': case '4': case '5':
                case '6': case '7': case '8': case '9':
                    printf("\e[1;37m%c\e[22;0m",c);
                    break;
                case '{': case '}':
                    printf("\e[1;31m%c\e[22;0m",c);
                    break;
                case '"': case '#':
                    printf("\e[1;36m%c\e[22;0m",c);
                    break;
                
                default:
                  putchar(c);  
            }
        }else
        */
            putchar(c);

        fflush(stdout);
    }

    printf("filter_gdbout\n");
    fflush(stderr);

}

static void filter_vimout(void)
{
    FILE *vim_out;
    int c;
   
    vim_out=fopen(vim_pipe_file,"r");

    while(c=getc(vim_out))
    {
        if(c!=EOF)
            write(gdb_pty,&c,1); 
    }

    printf("filter_vimout\n");
    fflush(stderr);
}

/* not actually used */
static void send_to_gdb(const char *command)
{
    int len=strlen(command);
    write(gdb_pty,command,len);
}

static void send_to_vim(const char *command, ...)
{
    char *vim_command=NULL;
    char *arg;
    va_list ap;
    
    va_start(ap, command);
    
    /* this should be moved somwhere else (unneccissary to do every time we talk to vim) */
    if(strlen(server)==0){
        if((server=realloc(server,5))==NULL){
            fprintf(stderr,"Couldn't alloc memory for server name\n");
            exit(EXIT_FAILURE);
        }
        strcpy(server,"GVIM");
    }

    if((vim_command=(char *)malloc(54 + strlen(command) + strlen(server)))==NULL){
        fprintf(stderr,"Couldn't alloc memory for vim command\n");
        exit(EXIT_FAILURE);
    }
    
    sprintf(vim_command,"gvim --servername %s -u NONE -U NONE --remote-send \"%s",server,command);
    
    for(;;){/* loop through variable argument list */

        arg=va_arg(ap, char *);
        if(arg==NULL)/* terminate on NULL pointer */
        {
            va_end(ap);
            break;
        }

        if((vim_command=(char *)realloc(vim_command, strlen(vim_command) + strlen(arg) +2))==NULL)
        {
            fprintf(stderr,"Couldn't realloc memory to append argument to vim command");
            fflush(stderr);
            exit(EXIT_FAILURE);
        }
        strcat(vim_command, arg);

    }

    if((vim_command=(char *)realloc(vim_command, strlen(vim_command) + 1))==NULL)
    {
        fprintf(stderr,"Couldn't realloc memory to append \" to vim command :)");
        fflush(stderr);
        exit(EXIT_FAILURE);        
    }
    strcat(vim_command, "\"");
/*
    printf("%s\n",vim_command);
    fflush(stdout);
*/
    system(vim_command);
    free(vim_command);

}


