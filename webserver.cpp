#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fstream>
#include <time.h>
#include <thread>
#include <string.h>

#define VERSION "0.4.2"
#define LOG_FILE "webserver.log"
#define PID_FILE "webserver.pid"

using namespace std;

const auto processor_count = std::thread::hardware_concurrency();

static std::ofstream log (LOG_FILE);

int children = 0;

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

void demonize() {
	umask(0);
	int sid = setsid();
	if(sid < 0) {
		cerr << "sid = " << sid << endl;
	}
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}


void masterSignalHandler(int sig, siginfo_t *si, void *ptr) {
	log << "Master caught signal: " << strsignal(sig) << endl;

	int status;
	pid_t pid;
	if(sig == SIGCHLD) {
		log << "SIGCHLD caught from Process #" << si->si_pid << endl;
		while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
			log << "Child " << pid << " terminated with status " << status << endl;
			--children;
		}
	}
}

/*
	WORKER
*/

int workerProcess(int socket) {
	log << "Worker with PID " << getpid() << " created. Parent pid = " << getppid() << ". Socket = " << socket << endl;

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	while (1) {
		log << "Worker " << getpid() << " is alive" << endl;
		usleep(5 * 1000 * 1000);
	}

	return 0;
}

/*
	MASTER
*/

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

	struct sigaction act;
	act.sa_sigaction = masterSignalHandler;
	act.sa_flags = SA_SIGINFO;

	int res = sigaction(SIGCHLD, &act, NULL);

	cout << "res = " << res << endl;
 
	pid_t pid;

	while(1) {

		if(children < processor_count) {
			usleep(0.5 * 1000 * 1000);

			int sv[2];

			if(socketpair(AF_LOCAL, SOCK_STREAM, 0, sv) < 0) {
				perror("socketpair");
				log << "Can't create socketpair" << endl;
				continue;
			}

			log << "socketpair: " << sv[0] << " and " << sv[1] << endl;

			pid = fork();
			++children;

			switch(pid) {
				case -1: {
					log << "Can't fork: " << errno << endl;
					break;
				}
				case 0: {
					close(sv[0]);
					int exitCode = workerProcess(sv[1]);
					log << "Exit for " << getpid() << " with code " << exitCode << endl;
					exit(exitCode);
				}
				default: {
					close(sv[1]);
					log << "Master socket " << sv[0] << endl;
				}
			}
			
		} else {
			log << "Master " << getpid() << " is waiting for signal " << endl;
			usleep(5 * 1000 * 1000);

			// waitpid(-1, NULL, 0);
		}

		log << "children = " << children << endl;
		usleep(1 * 1000 * 1000);

	}


	return 0;
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
