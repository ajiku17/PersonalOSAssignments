#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Max number of characters for a working directory string */
#define MAXCWD 4096

/* Name of the PATH variable (which I'm pretty sure is always going to be "PATH", but oh well) */
#define PATH_VARIABLE_NAME "PATH"

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

/* Current working directory*/
char working_directory[MAXCWD];

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_wait(struct tokens* tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "print working directory"},
  {cmd_cd, "cd", "change working directory"},
  {cmd_wait, "wait", "wait for child processes"}
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {

  /* Free the tokens before exiting (this was missing in the starter code for some reason) */
  tokens_destroy(tokens);
  /* */

  exit(0);
}

/* Prints current working directory*/
int cmd_pwd(unused struct tokens *tokens){
  
  printf("%s\n", working_directory);

  return 1;
}

/* Changes the current working directory*/
int cmd_cd(struct tokens *tokens){

  if (tokens_get_length(tokens) == 2){
    
    char* arg = tokens_get_token(tokens, 1);
    char dest_dir[MAXCWD];

    if (arg != NULL){
      
      if(arg[0] == '/'){ // we are guaranteed to have at least one character, since it's not NULL
        // full path
        memcpy(dest_dir, arg, strlen(arg) + 1);

      }else{
        // relative path
        
        // first extra space for slash
        char relative_dest[strlen(arg) + 2];
        relative_dest[0] = '/';

        memcpy(relative_dest + 1, arg, strlen(arg) + 1); // relative path with a slash

        strcpy(dest_dir, working_directory); // make a copy so we won't corrupt the current value

        strcat(dest_dir, relative_dest); // new dest is now in dest_dir

      }

      int status = 0;
      status = chdir(dest_dir);

      if (status != 0){ 
        // Error descriptions taken out of man chdir
        switch (errno) {
          case EACCES:
            printf("cd: Permission denied.\n");
            break;
          case ENAMETOOLONG:
            printf("cd: Path is too long.\n");
            break;
          case ENOTDIR:
            printf("cd: %s is not a directory.\n", dest_dir);
            break;
          case ENOENT:
            printf("cd: %s does not exist.\n", dest_dir);
            break;

          default:
            printf("cd: error %d occured, check \"man chdir for more detailed descritpion\".\n", errno);
            break;
        }
      }else{ 
        // Success

        /* Update the current working directory variable*/
        char copy[MAXCWD];
        if(getcwd(copy, MAXCWD) != NULL){
          memcpy(working_directory, copy, strlen(copy) + 1);
        }
      }
    }

  }else{
    printf("cd: invalid number of arguments, one needed\n");
  }

  return 1;
}

int cmd_wait(unused struct tokens* tokens){
  errno = 0;
  int status = wait(0); // wait for every child 
  
  if(errno == ECHILD){
    printf("wait: there are no child processes\n");
  }else if (status == -1){
    printf("wait: an error has occured\n");
  }

  unused int attr_status = tcsetattr(shell_terminal, TCSANOW, &shell_tmodes);

  tcsetpgrp(shell_terminal, shell_pgid);

  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}


int execv_from_path(char* path_var, int left, int right, char* program, char** prog_args){

  char executable_path_option[left - right + 1 + 1 + strlen(program)];

  memcpy(executable_path_option, path_var + right, left - right);
  executable_path_option[left - right] = '\0'; // NULL terminate the executable string, so we can use strcat
  strcat(executable_path_option, "/");
  strcat(executable_path_option, program);
  
  prog_args[0] = executable_path_option;

  /* I guess, I somehow have to clean up memory before the new executable is loaded ? I'm not sure */

  execv(prog_args[0], prog_args);

  /* Some error handling is in order here */

  return -1;
}


/* Calls the procedure from path using execv (a poor implementation of execvp) */
int poor_execvp(char* program, char** prog_args){

  if (strchr(program, '/') != NULL){
    // Full path
    errno = 0;

    prog_args[0] = program; // Make a copy in heap memory 

    /* I guess, I somehow have to clean up memory before the new executable is loaded ? I'm not sure */
      
    execv(prog_args[0], prog_args);
  
  }else{
    // Relative path

    char* path = getenv(PATH_VARIABLE_NAME);
    int error = 0;
    int left = 0;
    int right = 0;

    for(; left < strlen(path); left++){
      errno = 0;
      if(path[left] == ':'){
        execv_from_path(path, left, right, program, prog_args);
        right = left + 1;
        error |= errno;
      }

    }
    /* Last PATH entry is not terminated with ':', so we have to call it manually */
    execv_from_path(path, left, right, program, prog_args); 
  }

  return -1;  
}

/* opens a new file descriptor(or an existing one) and switches it with fd_to */
int replace_io_stream(char* filename, int fd_to){
  int fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  return dup2(fd, fd_to);
}

int call_execvp(struct tokens* tokens){

  char* program_path = tokens_get_token(tokens, 0);

  size_t num_tokens = tokens_get_length(tokens);

  /* It is guarateed that '&' will only be placed as the last token */
  if(strcmp("&", tokens_get_token(tokens, tokens_get_length(tokens) - 1)) == 0){
    num_tokens--;
  }

  char* prog_args[num_tokens + 1]; // +1 is for the NULL pointer at the end
  // This does not load the executed program name into the argument list
  int token_index = 1;
  int arg_index = 1;

  for(token_index = 1; token_index < num_tokens; token_index++){
    if (strcmp("<", tokens_get_token(tokens, token_index)) == 0){
      char* file_input = tokens_get_token(tokens, ++token_index);
      if(file_input != NULL){
        replace_io_stream(file_input, STDIN_FILENO);
      }else{
        // shell error
        printf("shell: provide input file\n");
        exit(-1);
      }
    }else if (strcmp(">", tokens_get_token(tokens, token_index)) == 0){
      char* file_output = tokens_get_token(tokens, ++token_index);
      if(file_output != NULL){
        replace_io_stream(file_output, STDOUT_FILENO);
      }else{
        // shell error
        printf("shell: provide output file\n");
        exit(-1);
      }
    }else{
      prog_args[arg_index] = strdup(tokens_get_token(tokens, token_index)); // Make a copy in heap memory
      arg_index++;
    }
  }

  prog_args[arg_index] = NULL; // According to manual, argument array must be NULL terminated

  poor_execvp(program_path, prog_args); // Execute the program

  return -1;
}



/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  /* Initialize working directory */
  unused char* status = getcwd(working_directory, MAXCWD);
  // printf("%s\n", status); // I honestly dont know how to handle if an error occurs heres

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

/* Void handler which essentially does nothing */
void void_handler(int signum){
  (void)signum;
}

int main(unused int argc, unused char *argv[]) {
  init_shell();
   
  static char line[4096];
  int line_num = 0;

  /* Ignore any sigttou signals, so that the shell can be moved back into foreground */
  signal(SIGTTOU, SIG_IGN);

  /* Ignore sigchld signals, so there are no zombie child processes left after their termination
     when the parent does not wait for them. */
  signal(SIGCHLD, SIG_IGN);

  /* Ignore sigtstp and sigint, so the shell doesn't quit on CTRL-C or CTRL-Z */
  signal(SIGTSTP, void_handler);
  signal(SIGINT, void_handler);

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* Run the program entered by the user using fork() -> exec() */  

      if(tokens_get_length(tokens) >= 1){

        int background_process = 0;
        if(strcmp("&", tokens_get_token(tokens, tokens_get_length(tokens) - 1)) == 0){
          background_process = 1;
        }

        pid_t child_id = fork();
        errno = 0; // Just in case

        if (child_id == 0){
          // child
          
          call_execvp(tokens);

          /* Some error handling is in order here, but lets defer it for now */
          printf("Error has occured. errno: %d\n", errno);

          /* 
            In case of failure free the memory allocated for the program arguments.
            I'm not sure if doing this explicitly is necessary.
          */

          /* Exit the child process with error */
          exit(-1);

        }else{
          // parent

          //give child it's own group
          unused int group_status = setpgid(child_id, child_id);

          //bring it into foreground
          unused int foreground_status = tcsetpgrp(shell_terminal, child_id);

          /* Shell must waits for the child process to finish */
          if(!background_process){
            int wstatus;
            waitpid(child_id, &wstatus, WUNTRACED);
          }

          unused int attr_status = tcsetattr(shell_terminal, TCSANOW, &shell_tmodes);

          tcsetpgrp(shell_terminal, shell_pgid);
        }

      }    
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
