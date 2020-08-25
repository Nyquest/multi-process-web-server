#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fstream>
#include <time.h>
#include <thread>
#include <string.h>

#define VERSION "0.3.4"
#define LOG_FILE "webserver.log"
#define PID_FILE "webserver.pid"
#define CHILD_RESTART 1

using namespace std;

const auto processor_count = std::thread::hardware_concurrency();

static std::ofstream log (LOG_FILE);

void writePid(pid_t pid) {
	FILE *f;
	f = fopen(PID_FILE, "w+");
	if(f){
		fprintf(f, "%u", pid);
		fclose(f);
		log << "Master PID " << pid << " written" << endl;
	} else {
		log << "Master PID write error: " << errno << " " << strerror(errno) << endl;
	}
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
    int sig = -1;

    if ( sigwait(set, &sig) < 0 )
        return -1;

    return sig;
}

void demonize() {
	umask(0);
	int sid = setsid();
	if(sid < 0) {
		cerr << "sid = " << sid << endl;
	}
	// int chdir_val = chdir("/");
	// if(chdir_val < 0) {
	// 	cerr << "chdir = " << chdir_val << endl;
	// }
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}

void workerSignalError(int sig, siginfo_t *si, void *ptr) {
	log << "Signal: " << strsignal(sig) << ". Address: " << si->si_addr << endl;
	exit(CHILD_RESTART);
}

int workerProcess() {
	log << "Worker with PID " << getpid() << " created. Parent pid = " << getppid() << endl;

	// todo del
	// int sid = setsid();
	// if(sid < 0) {
	// 	cerr << "sid = " << sid << endl;
	// }

	// demonize();

	// todo del end

	struct sigaction act;
	sigset_t set;
	
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = workerSignalError;

	sigemptyset(&act.sa_mask);

	sigaction(SIGFPE, &act, 0);
    sigaction(SIGILL, &act, 0); 
    sigaction(SIGSEGV, &act, 0);
    sigaction(SIGBUS, &act, 0);
 
    sigemptyset(&set);

    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);

    sigprocmask(SIG_BLOCK, &set, NULL);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);


    log << "Worker " << getpid() << " sleeping..." << endl;
	usleep(15 * 1000 * 1000);
	log << "Worker " << getpid() << " woke up" << endl;

	return 0;
	// return 1;
}

int masterProcess() {

	pid_t master_pid = getpid();

	time_t my_time = time(NULL);

	log << "------------------------" << endl;
	log << "Master " << VERSION << " starting..." << endl;
	log << "------------------------" << endl;

	log << "Current time: " << ctime(&my_time);

	log << "Master PID " << master_pid << endl;

	log << "Processor count: " << processor_count << endl;

	writePid(master_pid);


	int status = 0;
	sigset_t  set;
	siginfo_t siginfo;

	sigemptyset(&set);

	// sigaction(SIGCHLD, &act, 0);
	log << "SIGQUIT = " << sigaddset(&set, SIGQUIT) << endl;
	log << "SIGINT = " << sigaddset(&set, SIGINT) << endl;
	log << "SIGTERM = " << sigaddset(&set, SIGTERM) << endl;
	log << "SIGCHLD = " << sigaddset(&set, SIGCHLD) << endl;

	sigprocmask(SIG_BLOCK, &set, NULL);
 
	int children = 0;
	pid_t pid;

    while(1) {

    	if(children < processor_count) {
    		usleep(0.5 * 1000 * 1000);
    		pid = fork();
    		++children;

    		switch(pid) {
    			case -1: {
    				log << "Can't fork: " << errno << endl;
    				break;
    			}
    			case 0: {
    				int exitCode = workerProcess();
    				log << "Exit for " << getpid() << " with code " << exitCode << endl;
    				// kill(getpid(), SIGKILL);
    				exit(exitCode);
    			}
    		}
    		
    	} else {
    		log << "Master " << getpid() << " is waiting for signal " << endl;

    		waitpid(-1, NULL, 0);

    		log << "Parent waited for child " << endl;

    		// pause();
   //  		int signalCode = sigwaitinfo(&set, &siginfo);
    		// log << "Master received signal" << endl;
   //  		log << "Signal code = " << signalCode << endl;

   //  		log << "SIGCHLD = " << SIGCHLD << endl;

			// if(siginfo.si_signo == SIGCHLD) {
			// 	log << "SIGCHLD from" << pid << endl;
			// }
    	}

    	log << "children = " << children << endl;
    	usleep(1 * 1000 * 1000);

    }


	return status;
}


int main(int argc, char *argv[]) {
	cout << "***************************" << endl;
	cout << "WebServer " << VERSION << " starting..." << endl;
	cout << "***************************" << endl;
	int key = 0;
	const char *host = "localhost";
	int port = 11777;
	const char *directory = "/tmp/";

	if(argc > 1) {
		while( (key = getopt(argc, argv, "h:p:d:")) != -1 ) {
			switch(key) {
				case 'h':
					host = optarg;
					break;
				case 'p':
					port = atoi(optarg);
					break;
				case 'd':
					directory = optarg;
					break;
				case '?':
					cerr << "Unknown key" << endl;
					break;
			}
		}
	}
	cout << "host = " << host << endl;
	cout << "port = " << port << endl;
	cout << "directory = " << directory << endl;

	pid_t launcher_pid = getpid();

	cout << "launcher_pid = " << launcher_pid << endl;

	pid_t pid;

	pid = fork();

	switch(pid) {
		case -1: {
			cout << "Error: " << strerror(errno) << endl;
			return -1;
		}
		case 0: {
			cout << "Daemon launched with pid " << getpid() << endl;

			demonize();

			return masterProcess();
		}
			
		default: {
			cout << "Launcher worked." << endl;
			return 0;
		}
			
	}
}
