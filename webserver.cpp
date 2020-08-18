#include<iostream>
#include <unistd.h>
#include <sys/stat.h>

using namespace std;

int main(int argc, char *argv[]) {
	cout << "***************************" << endl;
	cout << "WebServer 0.0.2 starting..." << endl;
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
		case -1:
			cout << "Error: " << strerror(errno) << endl;
			return -1;
		case 0:
			cout << "Launcher worked." << endl;
			//exit(EXIT_SUCCESS);
			return 0;
		default:
			cout << "Daemon launched with pid " << pid << endl;
			// umask(0);
			// int sid = setsid();
			// if(sid < 0) {
			// 	cout << "sid = " << sid << endl;
			// }
			// int chdir_val = chdir("/");
			// if(chdir_val < 0) {
			// 	cout << "chdir = " << chdir_val << endl;
			// }
			// close(STDIN_FILENO);
	  //       close(STDOUT_FILENO);
	  //       close(STDERR_FILENO);
			// while(1) {
			// 	cout << "Daemon " << pid << " heartbeat" << endl;
			// 	usleep(1000000);
			// }
			return 0;
	}
}
