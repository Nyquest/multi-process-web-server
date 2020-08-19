#include<iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fstream>
#include <filesystem>
#include <time.h>

#define VERSION "0.3.2"
#define LOG_FILE "webserver.log"
#define PID_FILE "webserver.pid"

using namespace std;
namespace fs = std::filesystem;

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

int workerProcess() {
	return 0;
}

int masterProcess() {

	pid_t master_pid = getpid();

	time_t my_time = time(NULL);

	log << "------------------------" << endl;
	log << "Master " << VERSION << " starting..." << endl;
	log << "------------------------" << endl;

	log << "Current time: " << ctime(&my_time);

	log << "Master PID " << master_pid << endl;

	log << "Current path: " << fs::current_path() << endl;

	writePid(master_pid);


	int status = 0;
	sigset_t  sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGCHLD);

	sigprocmask(SIG_BLOCK, &sigset, NULL);
 
	int children = 0;
	pid_t pid;

    while(1) {

    	if(children < 5) {
    		usleep(10 * 1000 * 1000);
    		pid = fork();
    		++children;

    		switch(pid) {
    			case -1: {
    				log << "Can't fork: " << errno << endl;
    				break;
    			}
    			case 0: {
    				log << "Worker with PID " << getpid() << " created. Parent pid = " << getppid() << endl;
    				usleep(1000000);
    				exit(workerProcess());
    			}
    		}
    		
    	}

    	usleep(10 * 1000 * 1000);

    }


	return status;
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

int main(int argc, char *argv[]) {
	cout << "***************************" << endl;
	cout << "WebServer " << VERSION << " starting..." << endl;
	cout << "***************************" << endl;
	cout << "Current path: " << fs::current_path() << endl;
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
