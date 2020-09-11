#include <arpa/inet.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#define VERSION "0.4.2"
#define LOG_FILE "webserver.log"
#define PID_FILE "webserver.pid"
#define MAX_EVENTS 32
#define BUFFER_SIZE 4096

using namespace std;

// const auto processor_count = std::thread::hardware_concurrency();
const auto processor_count = 2;

static std::ofstream log (LOG_FILE);

struct global_args_t {
	string host;
	int port;
	string directory;
} global_args;

struct master_vars_t {
	int children;
	std::map<pid_t, int> socket_map;
	vector<int> sockets;
} master_vars;

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
			--master_vars.children;
		}
	}
}

int set_nonblock(int fd) {
	int flags;
	#if defined(O_NONBLOCK)
		if(-1 == (flags = fcntl(fd, F_GETFL, 0)))
			flags = 0;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	#else
		flags = 1;
		return ioctl(fd, FIOBIO, &flags);
	#endif
}

ssize_t sock_fd_write(int socket, void *buf, ssize_t buflen, int fd) {
	log << "sock_fd_write: socket = " << socket << ", fd = " << fd << endl; 
	ssize_t size;
	struct msghdr msg;
	struct iovec iov;

	union {
		struct cmsghdr cmsghdr;
		char control[CMSG_SPACE(sizeof(int))];
	} cmsgu;

	struct cmsghdr * cmsg;

	iov.iov_base = buf;
	iov.iov_len = buflen;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if(fd != -1) {
		msg.msg_control = cmsgu.control;
		msg.msg_controllen = sizeof(cmsgu.control);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*((int *)CMSG_DATA(cmsg)) = fd;
	} else {
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		log << "Not passing fd" << endl;
	}

	size = sendmsg(socket, &msg, 0); 
	if(size == -1) {
		log << "Sendmsg error: " << errno << endl;
		log << strerror(errno) << endl;
	} else {
		log << "sendmsg: OK. Socket " << socket << ", fd " << fd << " with size " << size << endl;
	}
	return size;
}

ssize_t sock_fd_read(int socket, void * buf, ssize_t bufsize, int *fd) {
	ssize_t size;

	if(fd) {
		struct msghdr msg;
		struct iovec iov;

		union {
			struct cmsghdr cmsghdr;
			char control[CMSG_SPACE(sizeof(int))];
		} cmsgu;

		struct cmsghdr * cmsg;

		iov.iov_base = buf;
		iov.iov_len = bufsize;

		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		msg.msg_control = cmsgu.control;
		msg.msg_controllen = sizeof(cmsgu.control);

		size = recvmsg(socket, &msg, 0);

		if(size < 0) {
			log << "recvmsg error: " << errno << endl;
			log << strerror(errno) << endl;
			exit(1);
		}

		cmsg = CMSG_FIRSTHDR(&msg);

		if(cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
			if(cmsg->cmsg_level != SOL_SOCKET) {
				log << "Invalid cmsg_level " << cmsg->cmsg_level << endl;
				exit(1);
			}
			if(cmsg->cmsg_type != SCM_RIGHTS) {
				log << "Invalid cmsg_type " << cmsg->cmsg_type << endl;
				exit(1);
			}
			*fd = *((int *)CMSG_DATA(cmsg));
		} else {
			log << "cmsg->cmsg_len = " << cmsg->cmsg_len << endl;
			log << "CMSG_LEN(sizeof(int)) = " << CMSG_LEN(sizeof(int)) << endl; 
			*fd = -1;
		}

	} else {
		size = read(socket, buf, bufsize);
		if(size < 0) {
			log << "Can't read from reading socket" << endl;
			exit(1);
		}
	}

	return size;
}

/*
	WORKER
*/
int workerProcess(int socket) {

	pid_t pid = getpid();

	log << "PID " << pid << ": reading socket = " << socket << endl;

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	int fd;
	char buf[16];
	ssize_t size;

	while (1) {
		log << "PID " << pid << ": wait fd..." << endl;
		size = sock_fd_read(socket, buf, sizeof(buf), &fd);
		log << "PID " << pid << ": got fd " << fd << ", size " << size << endl; 
		
		if(size <= 0){
			break;
		}
		
		if(fd != -1) {

			static char buffer[BUFFER_SIZE];
			int recv_result = recv(fd, buffer, BUFFER_SIZE, MSG_NOSIGNAL);

			log << "PID " << pid << ": " << "fd = " << fd << ", recv_result = " << recv_result << ", errno = " << errno << endl;

			if(recv_result == 0 && (errno != EAGAIN)) {
				log << "FD " <<  fd << " close" << endl;
				shutdown(fd, SHUT_RDWR);
				close(fd);
			} else {
				write(fd, buffer, recv_result);
				log << "FD " <<  fd << " close" << endl;
				shutdown(fd, SHUT_RDWR);
				close(fd);
			}

		}
	}

	return 0;
}

/*
	MASTER
*/
int masterProcess() {

	pid_t master_pid = getpid();

	time_t my_time = time(NULL);

	master_vars.children = 0;

	log << "------------------------" << endl;
	log << "Master " << VERSION << " starting..." << endl;
	log << "------------------------" << endl;

	log << "Current time: " << ctime(&my_time);

	log << "Master PID " << master_pid << endl;

	log << "Processor count: " << processor_count << endl;

	log << "SOMAXCONN: " << SOMAXCONN << endl;

	writePid(master_pid);

	struct sigaction act;
	act.sa_sigaction = masterSignalHandler;
	act.sa_flags = SA_SIGINFO;

	if(sigaction(SIGCHLD, &act, NULL) == -1) {
		log << "Error of sigaction SIGCHLD" << endl;
	}
 
	pid_t pid;

	int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	int flag = 1;
	if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
		log << "Reuse addr error: " << errno << endl;
		log << strerror(errno) << endl;
	} else {
		log << "Reuse addr: OK" << endl;
	}

	log << "ip: " << global_args.host << endl;
	log << "port: " << global_args.port << endl;

	struct sockaddr_in SockAddr;

	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(global_args.port);

	if(global_args.host.compare("localhost") == 0) {
		log << "localhost => 127.0.0.1" << endl;
		SockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else {
		SockAddr.sin_addr.s_addr = inet_addr(global_args.host.c_str());
	}

	if(::bind(master_socket, (struct sockaddr *)(&SockAddr), sizeof(SockAddr)) == -1) {
		log << "Bind error: " << errno << endl;
		log << strerror(errno) << endl;
		exit(EXIT_FAILURE);
	} else {
		log << "Bind: OK" << endl;
	}

	set_nonblock(master_socket);

	if(listen(master_socket, SOMAXCONN) == -1) {
		log << "Listen error: " << errno << endl;
		log << strerror(errno) << endl;
		exit(EXIT_FAILURE);
	} else {
		log << "Listen: OK" << endl;
	}

	int epoll = epoll_create1(0);

	struct epoll_event event;
	event.data.fd = master_socket;
	event.events = EPOLLIN;
	epoll_ctl(epoll, EPOLL_CTL_ADD, master_socket, &event);

	int round_robin_index = 0;

	char required_buf[1];
	required_buf[0] = '1';

	while(1) {

		bool fork_created = false;

		while(master_vars.children < processor_count) {
			usleep(0.5 * 1000 * 1000);

			int sv[2];

			if(socketpair(AF_LOCAL, SOCK_STREAM, 0, sv) < 0) {
				log << "Can't create socketpair" << endl;
				continue;
			}

			pid = fork();
			++master_vars.children;

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
					master_vars.socket_map.insert(make_pair(pid, sv[0]));
					master_vars.sockets.push_back(sv[0]);
					fork_created = true;
				}
			}
			
		}

		if(fork_created) {
			std::map<pid_t, int>::iterator it = master_vars.socket_map.begin();
			while(it != master_vars.socket_map.end()) {
				log << "PID " << it->first << ": writing_socket = " << it->second << endl;
				it++;
			}

			for(int i = 0; i < master_vars.sockets.size(); ++i) {
				log << i << ") " << master_vars.sockets[i] << endl;
			}
		}

		struct epoll_event events[MAX_EVENTS];
		log << endl;
		log << "wait events..." << endl; 
		int new_event_count = epoll_wait(epoll, events, MAX_EVENTS, -1);
		log << "new_event_count = " << new_event_count << endl;

		for(int ei = 0; ei < new_event_count; ei++) {
			int fd = events[ei].data.fd;
			if(fd == master_socket) {
				log << "New client connection..." << endl;
				int slave_socket = accept(master_socket, 0, 0);
				log << "Connection accepted: " << slave_socket << endl;
				set_nonblock(slave_socket);

				struct epoll_event event;
				event.data.fd = slave_socket;
				event.events = EPOLLIN;

				epoll_ctl(epoll, EPOLL_CTL_ADD, slave_socket, &event);
			} else {
				log << "---------" << endl;
				log << "FD " << fd << ": events = " << events[ei].events << endl;

				if(events[ei].events & EPOLLHUP) {
					log << "FD " << fd << ": EPOLLHUP" << endl;
					close(fd);
					continue;
				}

				if(master_vars.sockets.size() > 0) {
					while(round_robin_index >= master_vars.sockets.size()) {
						round_robin_index -= master_vars.sockets.size();
					}
					log << "round_robin_index = " << round_robin_index << ":" << master_vars.sockets[round_robin_index] << endl;
					ssize_t size = sock_fd_write(master_vars.sockets[round_robin_index], required_buf, 1, fd);
					log << "++round_robin_index" << endl;
					++round_robin_index;
				} else {
					log << "No socketpairs!" << endl;
				}
			}
		}

	}


	return 0;
}


int main(int argc, char *argv[]) {
	cout << "***************************" << endl;
	cout << "WebServer " << VERSION << " starting..." << endl;
	cout << "***************************" << endl;
	int key = 0;
	global_args.host = "127.0.0.1";
	global_args.port = 11777;
	global_args.directory = "/tmp/";

	if(argc > 1) {
		while( (key = getopt(argc, argv, "h:p:d:")) != -1 ) {
			switch(key) {
				case 'h':
					global_args.host = string(optarg);
					break;
				case 'p':
					global_args.port = atoi(optarg);
					break;
				case 'd':
					global_args.directory = string(optarg);
					break;
				case '?':
					cerr << "Unknown key" << endl;
					break;
			}
		}
	}
	cout << "host = " << global_args.host << endl;
	cout << "port = " << global_args.port << endl;
	cout << "directory = " << global_args.directory << endl;

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
