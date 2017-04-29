#include <stdlib.h>
#include <cstdlib>
#include <unistd.h>
#include <sstream>
#include <string>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>

using std::string;
using std::stringstream;

const unsigned int MAX_LINE_LENGTH = 2048*10;
const unsigned int MAX_LINES = 256;
const unsigned int MAX_PROCESSES = 128;
const unsigned int MAX_PIPES = MAX_PROCESSES - 1;

char buffer[MAX_LINE_LENGTH];

string arguments[MAX_LINES];
string processes[MAX_PROCESSES][MAX_LINES];

string getcwdir();

int numPipes();

void reset();
void fillProcesses();
void execute(string[]);
void printstatus(int);
string remove_slashes(string);
string remove_firstAndLast(string); 
void quotes();

int status = 1;

int main(){

  //ignore all of the signals we want to ignore
  signal (SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  //loop forever
  while(true){

    string prompt = "1730sh:" + getcwdir() + "$ ";

    //prompt
    write(STDOUT_FILENO, prompt.c_str(), prompt.length());

    //read
    read(STDIN_FILENO, buffer, MAX_LINE_LENGTH);

    //if the user typed something
    if(strlen(buffer) > 1){

      //just to be sure
      buffer[strlen(buffer)] = '\0';

      //put the input words into argv for easy use
      stringstream ss(buffer);

      string currS;

      int i = 0;

      while(ss >> currS){

        arguments[i] = currS;

        i++;

      }//while

      //fill the array with the processes
      fillProcesses();
      quotes();
      //get the number of pipes for our loop that creates each process
      int numpipes = numPipes();

      //get the number of processes, this number is one greater than the
      //index of the last process that we want to run
      int numProcs = numpipes + 1;

      //id of the children
      int pid;

      //storage for our pipe file descriptors
      int pipes[MAX_PIPES][2];

      //if we have to handle piping
      if(numpipes > 0){

	/* code in this loop was adapted from pipe4.cpp */
	//loop through each process
	for(int i = 0; i < numProcs; i++){

	  //count the number of pipes we have created so we can
	  //remove them later
	  int nPipes = 0;

	  //if we're not on the last process, create a new pipe and
	  //put it in the pipes array
	  if(i != (numProcs - 1)){

	    if(pipe(pipes[i]) == -1){

	      perror("pipe");

	    }//if

	    nPipes++;

	  }//if

	  //fork
	  if((pid = fork()) == -1){

	    perror("fork");

	  }//if

	  //if we're in the child
	  else if (pid == 0){

	    //unignore all of the signals we ignored
	    signal (SIGINT, SIG_DFL);
	    signal(SIGQUIT, SIG_DFL);
	    signal(SIGTSTP, SIG_DFL);
	    signal(SIGTTIN, SIG_DFL);
	    signal(SIGTTOU, SIG_DFL);
	    signal(SIGCHLD, SIG_DFL);

	    //if we're not in the first process, redirect standard input
	    //to the read side of the last pipe
	    if(i != 0){

	      //duplicate the file descriptor into standard input
	      if(dup2(pipes[i-1][0], STDIN_FILENO) == -1){

		perror("dup2");

	      }//if

	    }//if

	    //if we're not in the last process, redirect standard output
	    //to the write side of the current pipe
	    if(i != (numProcs - 1)){

	      //duplicate the file descriptor into standard output
	      if(dup2(pipes[i][1], STDOUT_FILENO) == -1){

		perror("dup2");

	      }//if

	    }//if

	    //close all of the pipes we have made so far
	    for(int j = 0; j < numpipes; j++){

	      close(pipes[j][0]);
	      close(pipes[j][1]);

	    }//for

	    //execute the process
	    execute(processes[i]);

	  }//else if

	}//for

      }//if

      //if we have exit
      else if(processes[0][0] == "exit"){

	if(processes[0][1] != ""){

	  return std::stoi(processes[0][1]);

	}//if

	return status;

      }//else if

      //if we have cd
      else if(processes[0][0] == "cd"){

	if(chdir(processes[0][1].c_str()) < 0){

	  perror("cd");

	}//if

      }//else if

      else if(processes[0][0] == "help"){

	write(STDOUT_FILENO,"\nbg JID -- Resume the stopped job JID in the background, as if it had been started with&.\n",90);
	write(STDOUT_FILENO,"cd [PATH] -- Change the current directory to PATH. The environmental variable HOME is the default PATH.\n",104);
	write(STDOUT_FILENO,"exit [N] -- Cause the shell to exit with a status of N. If N is omitted, the exit status is that of the last job executed.\n",123);
	write(STDOUT_FILENO,"export NAME[=WORD] -- NAME is automatically included in the environment of subsequently executed jobs.\n",103);
	write(STDOUT_FILENO,"fg JID -- Resume job JID in the foreground, and make it the current job.\n",73);
	write(STDOUT_FILENO,"help -- Display helpful information about builtin commands.\n",60);
	write(STDOUT_FILENO,"jobs -- List current jobs.\n",27);
	write(STDOUT_FILENO,"kill [-s SIGNAL] PID -- The kill utility sends the specied signal to the specied process or process group PID (see kill(2)).  If no signal is specied, the SIGTERM signal is sent.\n\n",180);

      }//else if

      //if we have no pipes
      else{
	
	//fork
	if((pid = fork()) == -1){

	  perror("fork");

	}//if

	//if we're in the child
	else if (pid == 0){

	  //unignore all of the signals we ignored
	  signal (SIGINT, SIG_DFL);
	  signal(SIGQUIT, SIG_DFL);
	  signal(SIGTSTP, SIG_DFL);
	  signal(SIGTTIN, SIG_DFL);
	  signal(SIGTTOU, SIG_DFL);
	  signal(SIGCHLD, SIG_DFL);

	  //execute the command
	  execute(processes[0]);

	}//else if

      }//else

      //close out all of the pipes, as we no longer need them
      for(int j = 0; j < numpipes; j++){

	close(pipes[j][0]);
	close(pipes[j][1]);

      }//for

      int stat;

      //wait for the child and print its status once it exits
      //This doesn't work if the child terminates before we reach this line,
      //which happens often
      if(waitpid(pid, &stat, WUNTRACED) != -1) printstatus(stat);

    }//if

    reset();

  }//while

  return EXIT_SUCCESS;

}//main 


void quotes() 
{
  //each process:
  for(int i = 0; processes[i][0] != ""; i++) { 
    int startpos = 0;
    int endpos = 0;
    string inquotes = "";
    bool quoteDetected = false;
    //within the process:
    for(int j = 0; j < MAX_LINES; j++) {
      if((processes[i][j] != "") && (processes[i][j]).at(0) == '\"') {
	startpos = j;
	quoteDetected = true;
	break;
      }
    }
    //startpos is one before the quote
    if(quoteDetected) {
      for(int k = startpos; k < MAX_LINES; k++) {
	inquotes += processes[i][k];
	inquotes += " ";
	if((processes[i][k]).back() == '\"' && *(processes[i][k].rbegin() + 1) != '\\') {
	  endpos = k;
	  break;
	} // if
	
      } // for
      
      inquotes = remove_slashes(inquotes);
      inquotes = remove_firstAndLast(inquotes);
      inquotes = inquotes.substr(0, inquotes.length() - 1);
      
      for(int j = startpos; j < MAX_LINES; j++) {
	if(j == startpos) {
	  processes[i][j] = inquotes;
	}
	else
	  processes[i][j] = processes[i][endpos - startpos + j];
	
      }
      
      

      
    } // if quoteDetected
  } // big for 
  
    
  //go thru the array
  
  
  //save first quote  
  
  
}


string remove_slashes(string str)
{
  string str_to_erase = "\\\""; // removing \"
  size_t pos = str.find(str_to_erase, 0);

  while(pos != string::npos)
    {
      str.erase(pos, 1);
      pos = str.find(str_to_erase, pos + 1);
    }
  return str;
}

/* Used for removing double quotes on both sides of a string */
string remove_firstAndLast(string str)
{
  str.erase(0, 1); //remove first char from str
  str.pop_back(); //remove last
  return str;
}


/*
 * Gets the current working directory as a string,
 * and replaces the home path with a tilde
 * @return the current working directory as a string, with the home path
 * replaced with a tilde
 */
string getcwdir(){

  //maximum path length
  const unsigned int MAXPATHLEN = 2048;

  //path cstring
  char path[MAXPATHLEN];

  //put the current working directory into path
  getcwd(path, MAXPATHLEN);

  //copy that into a string
  string str(path);

  //we need to replace the home directory with a tilde if we see it
  const char *homedir;

  //put the home directory into homedir
  if((homedir = getenv("HOME")) == NULL){

    homedir = getpwuid(getuid())->pw_dir;

  }//if

  //put the home directory into a string
  string s_homedir(homedir);

  //if the path includes the home directory
  if(str.find(s_homedir) != string::npos){

    //get the substring after the home path
    string sub = str.substr(s_homedir.length());

    //take everything before the substring and turn it into a tilde
    str = "~" + sub;

  }//if

  return str;

}//getcwdir

/*
 * Returns the number of pipes in arguments
 * @return the number of pipes in arguments
 */
int numPipes(){

  //we assume zero
  int pipes = 0;

  //if we find a |, increment the number of pipes
  for(int i = 0; !arguments[i].empty(); i++){

    if(arguments[i] == "|") pipes++;

  }//for

  //return the number of pipes
  return pipes;

}//numJobs

/*
 * Resets the global variables and arrays, ready for the next
 * set of inputs
 */
void reset(){

  //clean up buffer
  for(int i = 0; buffer[i] != '\0'; i++){

    buffer[i] = '\0';

  }//for

  //clean up arguments
  for(int i = 0; arguments[i] != ""; i++){

    arguments[i] = "";

  }//for

  for(int i = 0; processes[i][1] != ""; i++){

    for(int j = 0; processes[i][j] != ""; j++){

      processes[i][j] = "";

    }//for

  }//for

}//reset

/*
 * Fills processes with the arguments for each process
 */
void fillProcesses(){

  //last position in arguments that we want to read from
  //for each process
  int lastpos = 0;

  int increment = 0;

  //loop as many times as we have processes
  for(int i = 0; i < (numPipes() + 1); i++){

    //store all of the process' arguments into their respective locations in
    //the processes array
    for(int j = lastpos; (arguments[j] != "|") && (arguments[j] != ""); j++){

      processes[i][j - lastpos] = arguments[j];

      increment++;

    }//for

    lastpos = increment + 1;

    increment = lastpos;

  }//for

}//fillProcesses

/*
 * Executes the given command
 * @param args the command line to execute
 */
void execute(string args[]){

  //we need a character array for execvp
  char * argv[MAX_PROCESSES];

  //save each argument into argv
  for(int i = 0; args[i] != ""; i++){

    argv[i] = strdup(args[i].c_str());

    argv[i + 1] = nullptr;

  }//for

  //execute the program
  execvp(argv[0], argv);

  //if we didn't execute, there was an error
  perror("execvp");

  //strdup(3) uses dynamic memory allocation, so we need to free
  //all of the strings stored by argv
  for(int i = 0; argv[i][0] != '\0'; i++) free(argv[i]);

}//execute

/*
 * Writes the specified process change to standard output
 * @param status the int value of the status change
 */
void printstatus(int stat){

  //write the process change
  write(STDOUT_FILENO,"\nProcess status change: ",24);

  if(WIFEXITED(stat)){

    write(STDOUT_FILENO,"Exited\n",7);

    status = WEXITSTATUS(stat);

  }//if

  else if(WIFSIGNALED(stat) && !WCOREDUMP(stat)) write(STDOUT_FILENO,"Killed\n",7);

  else if(WIFSIGNALED(stat) && WCOREDUMP(stat)) write(STDOUT_FILENO,"Killed, core file dumped\n",25);

  else if(WIFSTOPPED(stat)) write(STDOUT_FILENO,"Stopped\n",8);

  else if(WIFCONTINUED(stat)) write(STDOUT_FILENO,"Continued\n",10);

}//printstatus
