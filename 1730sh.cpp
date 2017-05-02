#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <cstdlib>
#include <sstream>
#include <fcntl.h>
#include <string>
#include <pwd.h>

using std::string;
using std::stringstream;

const unsigned int MAX_LINE_LENGTH = 2048*10;
const unsigned int       MAX_LINES = 256;
const unsigned int   MAX_PROCESSES = 128;
const unsigned int       MAX_PIPES = MAX_PROCESSES - 1;
const unsigned int        MAX_JOBS = 256;

//buffer for reading user input
char buffer[MAX_LINE_LENGTH];

//arrays that hold the  arguments type in, the processes 
//on individual lines, and the jobs
string                      jobs[MAX_JOBS];
string                arguments[MAX_LINES];
string processes[MAX_PROCESSES][MAX_LINES];

/* prototypes */

//string
string                  getcwdir();
string      remove_slashes(string);
string remove_firstAndLast(string);

//int
int               numPipes();
int           removeJob(int);
int    updateJob(int,string);

//void
void                        reset();
void                       quotes();
void                    printJobs();
void                fillProcesses();
void              execute(string[]);
void           printstatus(int,int);
void       addJob(int,string[],int);
void   redirectIO(int&, int&, int&);

//used for the exit status of the last run program
int status = 1;

//handler that does nothing (used so that we can ensure that the parent
//and child are on the same page process-wise)
void my_handler(int signum){}

int main(){

  //ignore all of the signals we want to ignore
  signal (SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  //set up our handler
  signal(SIGUSR1, my_handler);
  signal(SIGUSR2, my_handler);

  int fd;

  //get the file descriptor of the terminal
  if((fd = open("/dev/tty",O_RDONLY)) < 0) perror("open");

  //set the process group that controls the terminal to the process group
  //of our shell
  if(tcsetpgrp(fd,getpgrp()) < 0) perror("tcsetgrp");

  int shellPID = getpid();
  int shellPGID = getpgid(getpid());

  //loop forever
  while(true){

    string prompt = "1730sh:" + getcwdir() + "$ ";

    //print the prompt
    write(STDOUT_FILENO, prompt.c_str(), prompt.length());

    //read user input
    read(STDIN_FILENO, buffer, MAX_LINE_LENGTH);

    //if the user typed something
    if(strlen(buffer) > 1){

      //just to be sure
      buffer[strlen(buffer)] = '\0';

      //if we are running the process in the background
      bool background = false;

      //if we are running a builtin
      bool builtin = false;

      //put the input words into arguments for easy use
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

      //if the last thing the user typed was an ampersand
      for(int i = 0; processes[i][0] != ""; i++){

	for(int j = 0; processes[i][j] != ""; j++){

	  if((processes[i][j] == "&") && (processes[i][j+1] == "") && (processes[i + 1][0] == "")){

	    //we're running in the background
	    background = true;

	    //remove the ampersand from the arguments
	    processes[i][j] = "";

	  }//if

	}//for

      }//for

      // These may or may not be modified later in child depending on if <, >, >>, etc. were detected:
      int in_fileno = STDIN_FILENO;
      int out_fileno = STDOUT_FILENO;
      int err_fileno = STDERR_FILENO;

      //get the number of pipes for our loop that creates each process
      int numpipes = numPipes();

      //get the number of processes, this number is one greater than the
      //index of the last process that we want to run
      int numProcs = numpipes + 1;

      //id of the child
      int pid;
      int pgid;

      //storage for our pipe file descriptors
      int pipes[MAX_PIPES][2];

      //if we have to handle piping
      if(numpipes > 0){

	/*      code in this loop was adapted from pipe4.cpp      */
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

	    //if the child should be run in the background
	    if(background){

	      //move it out of the process group of the shell,
	      //so that it doesn't have control of the terminal
	      setpgid(getpid(),getpid());

	    }//if

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
	      if(dup2(pipes[i-1][0], STDIN_FILENO) == -1){ //remember: stdin_fileno is a variable that was set above

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

	    //if we have IO redirection
	    redirectIO(in_fileno, out_fileno, err_fileno);

	    //tell the  parent we're here
	    kill(shellPID, SIGUSR2);

	    //if we're running in the background, stop
	    if(background) raise(SIGSTOP);

	    //execute the process
	    execute(processes[i]);

	  }//else if

	  else if(pid != 0){

	    pause();

	    pgid = getpgid(pid);

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

	builtin = true;
	
	if(processes[0][1] == "") { // if no arguments after cd
	  if(chdir(getenv("HOME")) < 0) // change dir to HOME by default
	    perror("cd");
	}
	
	else // argument exists after cd
	  if(chdir(processes[0][1].c_str()) < 0)
	    perror("cd");
	
      }//else if

      //if we have help
      else if(processes[0][0] == "help"){

	builtin = true;

	write(STDOUT_FILENO,"\nbg JID -- Resume the stopped job JID in the background, as if it had been started with &.\n",91);
	write(STDOUT_FILENO,"cd [PATH] -- Change the current directory to PATH. The environmental variable HOME is the default PATH.\n",104);
	write(STDOUT_FILENO,"exit [N] -- Cause the shell to exit with a status of N. If N is omitted, the exit status is that of the last job executed.\n",123);
	write(STDOUT_FILENO,"export NAME[=WORD] -- NAME is automatically included in the environment of subsequently executed jobs.\n",103);
	write(STDOUT_FILENO,"fg JID -- Resume job JID in the foreground, and make it the current job.\n",73);
	write(STDOUT_FILENO,"help -- Display helpful information about builtin commands.\n",60);
	write(STDOUT_FILENO,"jobs -- List current jobs.\n",27);
	write(STDOUT_FILENO,"kill [-s SIGNAL] PID -- The kill utility sends the specified signal to the specied process or process group PID (see kill(2)).  If no signal is specied, the SIGTERM signal is sent.\n\n",182);

      }//else if

      //if we have jobs
      else if(processes[0][0] == "jobs"){

	builtin = true;

	//print the jobs
	printJobs();

      }//else if

      //if we have fg
      else if(processes[0][0] == "fg"){

	builtin = true;

	//usage error
	if(processes[0][1] == ""){

	  write(STDOUT_FILENO,"fg: usage: fg JID\n",18);

	}//if

	//usage error
	else if(removeJob(std::stoi(processes[0][1])) < 0){

	  write(STDOUT_FILENO,"fg: no such JID\n",17);

	}//if

	//no usage errors
	else{

	  //try to get  the process ID the user typed
	  //if the user did not type a number, this line crashes the shell
	  int processPID = std::stoi(processes[0][1]);

	  //put the process in the process group that has access to the shell
	  setpgid(processPID, shellPGID);

	  //signal the  proces to continue
	  if(kill(processPID, SIGCONT) < 0) perror("kill");

	  int stat;

	  //wait for the process
	  if(waitpid(processPID, &stat, WUNTRACED) != -1) printstatus(stat,pid);

	}//else

      }//else if

      //if we have bg
      else if(processes[0][0] == "bg"){

	builtin = true;

	//usage error
	if(processes[0][1] == ""){

          write(STDOUT_FILENO,"bg: usage: bg JID\n",18);

        }//if

	//the job does not exist
        else if(updateJob(std::stoi(processes[0][1]),"Running") < 0){

          write(STDOUT_FILENO,"bg: no such JID\n",17);

        }//if

        else{

	  //try to get the process ID  the user typed
          //if the user did not type a number, this line crashes the shell
          int processPID = std::stoi(processes[0][1]);

	  //put the process into its own process group so it doesn't have access
	  //to the terminal
          setpgid(processPID, processPID);

	  //signal the  proces to continue, and don't wait for it
          kill(processPID, SIGCONT);

        }//else

      }//else if

      //if we have kill
      else if(processes[0][0] == "kill"){

	builtin = true;

	//usage error
	if(processes[0][1] == "") write(STDOUT_FILENO,"kill: usage: kill [-s SIGNAL] JID\n",34);

	else if(processes[0][1] == "-s"){

	  //usage errors
	  if((processes[0][2] == "") || (processes[0][3] == "")) write(STDOUT_FILENO,"kill: usage: kill [-s SIGNAL] JID\n",34);

	  else{

	    int signum;

	    //try to get the process ID the user typed
	    //if the user did not type a number, this line crashes the shell
	    int JID = std::stoi(processes[0][3]);

	    //the signal the user typed
	    string signal = processes[0][2];

	    //signals that we can handle
	    if(signal == "SIGSTOP"){
	      signum = SIGSTOP;
	      updateJob(JID,"Stopped");
	    }//if
	    else if(signal == "SIGKILL"){
	      signum = SIGKILL;
	      removeJob(JID);
	    }//if
	    else if(signal == "SIGTERM"){
	      signum = SIGTERM;
	      removeJob(JID);
	    }//else if
	    else if(signal == "SIGINT"){
	      signum = SIGINT;
	      updateJob(JID,"Stopped");
	    }//else if
	    else if(signal == "SIGQUIT"){
	      signum = SIGQUIT;
	      removeJob(JID);
	    }//else if
	    else if(signal == "SIGTSTP"){
	      signum = SIGTSTP;
	      updateJob(JID,"Stopped");
	    }//else if
	    else if(signal == "SIGTTIN"){
	      signum = SIGTTIN;
	    }//else if
	    else if(signal == "SIGTTOU"){
	      signum = SIGTTOU;
	    }//else if
	    else if(signal == "SIGCHLD"){
	      signum = SIGCHLD;
	    }//else if
	    else if(signal == "SIGABRT"){
	      signum = SIGABRT;
	      removeJob(JID);
	    }//else if
	    else if(signal == "SIGTERM"){
	      signum = SIGTERM;
	      removeJob(JID);
	    }//else if
	    else if(signal == "SIGCONT"){
	      signum = SIGCONT;
	      updateJob(JID,"Running");
	    }//else if
	    else{

	      //set signum to the integer that the user typed
	      //if the  user did not type an integer, this rashes the shell
	      signum = std::stoi(signal);

	    }//else

	    //send the  signa
	    if(kill(JID,signum) < 0) perror("kill");

	  }//else

	}//else if

	else{

	  //usage error
	  if(processes[0][1] == "") write(STDOUT_FILENO,"kill: usage: kill [-s SIGNAL] JID\n",34);

	  else{

	    //try to get the process ID  the user typed
	    //if the user did not type a number, this line crashes the shell
	    int JID = std::stoi(processes[0][1]);

	    //send the  signalto terminate the process
	    if(kill(JID,SIGTERM) < 0) perror("kill");

	    //remove the job from the list of running jobs
	    removeJob(JID);

	  }//else

	}//else

      }//else if

      //if we have export
      else if(processes[0][0] == "export") {
	builtin = true;
        size_t found = (processes[0][1]).find("="); //index where = is found
        if(found != string::npos) { //contains =
          string name = (processes[0][1]).substr(0, found); //string before the =
          string value = (processes[0][1]).substr(found + 1, (processes[0][1]).size()); //string after the =
          setenv(name.c_str(), value.c_str(), 1); // overwrite if already exists
        }
      }

      //if we have no pipes and aren't running a builtin
      else{

	//fork
	if((pid = fork()) == -1){

	  perror("fork");

	}//if

	//if we're in the child
	else if (pid == 0){

	  //if the child should be run in the background
	  if(background){

	    //move it out of the process group of the shell,
	    //so that it doesn't have control of the terminal
	    setpgid(getpid(),getpid());

	  }//if

	  //unignore all of the signals we ignored
	  signal (SIGINT, SIG_DFL);
	  signal(SIGQUIT, SIG_DFL);
	  signal(SIGTSTP, SIG_DFL);
	  signal(SIGTTIN, SIG_DFL);
	  signal(SIGTTOU, SIG_DFL);
	  signal(SIGCHLD, SIG_DFL);

	  //redirect IO
	  redirectIO(in_fileno, out_fileno, err_fileno);

	  //tell the parent we're here
	  kill(shellPID, SIGUSR2);

	  //if we're running in the background, stop here
	  if(background) raise(SIGSTOP);

	  //wait for the parent to catch up
	  else pause();

	  //execute the command
	  execute(processes[0]);

	}//else if

	//we need to wait for the child to tell us that it has changed its PGID
	else if(pid != 0){

	  pause();

	  pgid = getpgid(pid);

	}//else if

      }//else

      if(!builtin){

	//close out all of the pipes, as we no longer need them
	for(int j = 0; j < numpipes; j++){

	  close(pipes[j][0]);
	  close(pipes[j][1]);

	}//for

	//if the program has terminated
	bool exited = false;

	//the status that waitpid() returns
	int stat = 0;

	//if we're not in the background, wait for the child and print its status once it exits
	//This doesn't work if the child terminates before we reach this line,
	//which happens often
	if(!background){

	  //tell the child we're here
	  kill(pid,SIGUSR1);

	  //wait for the child and print its status if we detect a status change
	  if(waitpid(pid, &stat, WUNTRACED) != -1) printstatus(stat,pid);

	  //if wait returned -1, the process terminated before we could wait for it
	  else exited = true;

	}//if

	//add the  job to jobs
	addJob(pgid,arguments,stat);

	//if the program terminated, remove it from jobs
	if(exited) removeJob(pgid);

      }//if

    }//if

    //reset all of our  globalarrays
    reset();

  }//while

  //this is pointless
  return EXIT_SUCCESS;

}//main 

/* 
 * Modifies the global variable 'processes' so that arguments within quotes
 *      will count as one argument altogether. 
 * The leading and trailing quotes as well as any backslashes that directly precede a "
 *      will be removed from each of the quoted strings. 
 */
void quotes() 
{
  for(int i = 0; processes[i][0] != ""; i++) { // for each process  
    int startpos = 0; //starting position of the quoted string
    int endpos = 0; //end pos of quoted string 
    string inquotes = ""; // the string contained within quotes 
    bool quoteDetected = false;

    // Find the first (leading) quotes
    for(unsigned int j = 0; j < MAX_LINES; j++) { //for each string (argument) within the process
      if((processes[i][j] != "") && (processes[i][j]).at(0) == '\"') { // process[i][j] is an argument within process i 
	startpos = j;
	quoteDetected = true;
	break;
      }
    }
    
    // Find the ending quotes
    if(quoteDetected) {
      for(unsigned int k = startpos; k < MAX_LINES; k++) {
	inquotes += processes[i][k]; // add everything inside quotes to string inquotes
	inquotes += " ";
	if((processes[i][k]).back() == '\"' && *(processes[i][k].rbegin() + 1) != '\\') { //if string doesn't end with backslash-quotes 
	  endpos = k;
	  break;
	} // if
	
      } // for
      
      inquotes = remove_slashes(inquotes);
      inquotes = remove_firstAndLast(inquotes);
      inquotes = inquotes.substr(0, inquotes.length() - 1);
      
      // Rearrange array so that the quoted string only counts as one argument
      for(unsigned int j = startpos; j < MAX_LINES; j++) {
	if(j == (unsigned int)startpos) {
	  processes[i][j] = inquotes;
	}
	else
	  processes[i][j] = processes[i][endpos - startpos + j]; //difference + j	
      
      } // for
      
    } // if(quoteDetected)
  } // big for  
} // quotes()


/* 
 * Helper function for redirectIO. Return true if string is <, >, >>, e>, or e>>. Else return false
 * @param s the string  to check
 * @return true if string is <, >, >>, e>, or e>>, return false otherwise
 */
bool isArrows(string s)
{
  if(s == ">")
    return true;
  else if(s == "<")
    return true;
  else if(s == ">>")
    return true;
  else if(s == "e>>")
    return true;
  else if(s == "e>")
    return true;

  else
    return false;
}

/*
 * This function is called in a child process just before exec to acheive IO redirection.
 * First parses jobStdin, jobStdout, jobStderr.
 *      --> their file descriptors (returned by open(2)) are then assigned to in_fileno, out_fileno, and/or err_fileno.
 *      --> Finally, duplicate those file descriptors onto STDIN, STDOUT, and STDERR 
 * Also removes <, >, >>, e>, and e>> (and anything following them) from the process's arguments using bool 'omit'
 * @param in_fileno the integer whose value we want to correspond to our input
 * @param out_fileno the integer whose value we want to correspond to our output
 * @param err_fileno the integer whose value we want to correspond to our error
 */
void redirectIO(int& in_fileno, int& out_fileno, int& err_fileno) 
{
  string jobStdin = ""; // the name of the file after '<'
  string jobStdout = "";
  string jobStderr = "";
  
  /* First, parse jobStdin, jobStdout, and jobStderr and get their file descriptors */

  for(int i = 0; processes[i][0] != ""; i++) { // for each process (although IO chars will only be in the last process)
    
    bool omit = false; // Becomes true when any IO char (<, >, >>, etc.) is encountered    
    

    for(unsigned int j = 0; j < MAX_LINES; j++) { // for every arg in the process
      
      string& token = processes[i][j]; 
      
      if(isArrows(token))  
	omit = true; // the arrow itself (<, >, etc.) and everything past it will be omitted (removed) from the process's arguments
      
  
      if(token == "<") { 
	jobStdin = processes[i][j+1]; // assuming a filename is given after the <
	if((in_fileno = open(jobStdin.c_str(), O_RDONLY)) == -1) { // attempt to open input file for reading 
	  perror("open");
	  exit(EXIT_FAILURE);
	}
      }
      else if(token == ">") { // truncate 
	jobStdout = processes[i][j+1];
	if((out_fileno = open(jobStdout.c_str(), O_WRONLY | O_TRUNC)) == -1) {
	  perror("open");
	  exit(EXIT_FAILURE);
	}
      }
      else if(token == ">>") { // append
	jobStdout = processes[i][j+1];
	if((out_fileno = open(jobStdout.c_str(), O_WRONLY | O_APPEND)) == -1) {
	  perror("open");
	  exit(EXIT_FAILURE);
	}
      }
      else if(token == "e>") { // truncate 
	jobStderr = processes[i][j+1];
	if((err_fileno = open(jobStderr.c_str(), O_WRONLY | O_TRUNC)) == -1) {
	  perror("open");
	  exit(EXIT_FAILURE);
	}
      }
      else if(token == "e>>") { // append
	jobStderr = processes[i][j+1];
	if((err_fileno = open(jobStderr.c_str(), O_WRONLY | O_APPEND)) == -1) {
	  perror("open");
	  exit(EXIT_FAILURE);
	}
      }
      
      if(omit) // remove omitted tokens from process's arguments    
	token = ""; //remember: token is a REFERENCE to processes[i][j]
  
    } // for
  } // for

  /* Now do the actual redirecting: */
  if(dup2(in_fileno, STDIN_FILENO) == -1) { //duplicate input file onto STDIN
    perror("dup2");
  }
  if(dup2(out_fileno, STDOUT_FILENO) == -1) { //duplicate output file onto STDOUT
    perror("dup2");
  }
  if(dup2(err_fileno, STDERR_FILENO) == -1) {
    perror("dup2");
  }
  
} //redirectIO()

	
/* 
 * Removes backslashes that are directly followed by quotes within a string
 * @param str the string to remove backslashes from
 * @return the edited string 
 */
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

/* 
 * Used for removing double quotes on both sides of a string
 * @param s the string  to remove doubl quotes from
 * @return the edited string
 */
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

  //clean up processes
  for(int i = 0; processes[i][0] != ""; i++){

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
 * @param JID the id of the process whose status has changed
 */
void printstatus(int stat, int JID){

  string jobID = std::to_string(JID);

  //write the process change
  write(STDOUT_FILENO,"\nProcess with JID ",18);
  write(STDOUT_FILENO,jobID.c_str(),jobID.length());
  write(STDOUT_FILENO," status changed: ",17);

  if(WIFEXITED(stat)){

    write(STDOUT_FILENO,"Exited\n",7);

    status = WEXITSTATUS(stat);

  }//if

  else if(WIFSIGNALED(stat) && !WCOREDUMP(stat)) write(STDOUT_FILENO,"Killed\n",7);

  else if(WIFSIGNALED(stat) && WCOREDUMP(stat)) write(STDOUT_FILENO,"Killed, core file dumped\n",25);

  else if(WIFSTOPPED(stat)) write(STDOUT_FILENO,"Stopped\n",8);

}//printstatus

/*
 * Adds a job to the jobs array
 * @param ID the JID of the job
 * @param command the arguments of the job
 * @param stat the status of the job
 */
void addJob(int ID, string command[], int stat){

  string out = "";

  string status = "";

  string JID = std::to_string(ID);

  //what to write for the status
  if(WIFSTOPPED(stat)) status = "Stopped";

  else if(stat == 0) status = "Stopped";

  else status = "Running";

  out = JID + "\t" + status + "\t\t";

  //add the arguments
  for(int i = 0; command[i] != ""; i++){

    out += command[i];

    out += " ";

  }//for

  int pos = 0;

  //find the first empty spot in the array
  for(int i = 0; jobs[i] != ""; i++){

    pos++;

  }//for

  //put this job there
  jobs[pos] = out;

}//addJob

/*
 * Removes a job from the jobs array
 * @param JID the job id of the job to remove
 */
int removeJob(int JID){

  int out = -1;

  string ID = std::to_string(JID);

  int end = ID.length();

  for(int i = 0; jobs[i] != ""; i++){

    if(jobs[i].substr(0,end) == ID){

      out = i;

      jobs[i] = "removed";

      break;

    }//if

  }//for

  for(int i = 0; jobs[i] != ""; i++){

    if(jobs[i] == "removed"){

      jobs[i] = jobs[i + 1];

      if(jobs[i + 1] != "") jobs[i + 1] = "removed";

    }//if

  }//for

  return out;

}//removeJob

/*
 * Prints all of the currently running or stopped jobs to standard output
 */
void printJobs(){

  write(STDOUT_FILENO,"JID\tSTATUS\t\tCOMMAND\n",20);

  for(int i = 0; jobs[i] != ""; i++){

    write(STDOUT_FILENO,jobs[i].c_str(),jobs[i].length());

    write(STDOUT_FILENO,"\n",1);

  }//for

}//printJobs

/*
 * Sets the status of the job with job ID JID to running
 * @param JID the job id of the job to update
 * @return the index of the job in jobs on success and -1 on failure
 */
int updateJob(int JID, string status){

  int out = -1;

  string ID = std::to_string(JID);

  int end = ID.length();

  for(int i = 0; jobs[i] != ""; i++){

    if(jobs[i].substr(0,end) == ID){

      string str = jobs[i];

      int index = jobs[i].find('S');

      if(index == (signed int)string::npos) index = jobs[i].find('R');

      str = str.replace(index, status.length(), status);

      jobs[i] = str;

      out = i;

      break;

    }//if

  }//for

  return out;

}//resumeJob
