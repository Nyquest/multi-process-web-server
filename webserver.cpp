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

const auto processor_count = std::thread::hardware_concurrency();

char const *header_200_text_html = "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Type: text/html\n\n";
char const *header_200_image_png = "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Disposition: inline\nContent-Type: image/png\n\n";
char const *header_200_text_javascript = "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Type: text/javascript\n\n";
char const *header_200_application_octet_stream = "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Type: application/octet-stream\n\n";
char const *header_200_application_json = "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Type: application/json;charset=UTF-8\n\n";

char const *body_not_implemented = "<b>Not implemented</b>";

char const *header_400 = "HTTP/1.0 400 Bad Request \nServer: MultiProcessWebServer v0.1\nConnection: Close\nContent-Type: text/html\n\n";
char const *body_400 = "<em>Bad request!</em>";

char const *header_404 = "HTTP/1.0 404 Not Found\nServer: MultiProcessWebServer v0.1\nContent-Type: text/html\n\n";

char const *root_directory = "/";

char const *default_page = "/index.html";

char const *route_calc = "/calc";

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

			std::map<pid_t, int>::iterator it;
			it = master_vars.socket_map.find(pid);
			if(it != master_vars.socket_map.end()) {
				master_vars.socket_map.erase(it);
				log << "Writing socket " << it->second << " deleted from map" << endl;


				int index = -1;
				for(int i = 0; i < master_vars.sockets.size(); ++i) {
					if(master_vars.sockets[i] == it->second) {
						index = i;
						break;
					}
				}

				if(index != -1) {
					master_vars.sockets.erase(master_vars.sockets.begin() + index);
					log << "Writing socket " << it->second << " deleted from vector" << endl;
				}
				--master_vars.children;
			}
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

enum method {POST, GET, UNKNOWN};

enum http_version {HTTP_1_0, HTTP_1_1, HTTP_2, UNKNOWN_VERSION};

enum content_type {HTML, JS, PNG, OCTET_STREAM};

method extract_method(char * buffer, int buffer_size, int *method_last_index) {

	for(int p = 0; p < buffer_size; ++p) {
		if(buffer[p] == ' ') {
			if(p == 4 && buffer[0] == 'P' && buffer[1] == 'O' && buffer[2] == 'S' && buffer[3] == 'T') {
				*method_last_index = p;
				return POST;
			} else if (p == 3 && buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T') {
				*method_last_index = p;
				return GET;
			} else {
				return UNKNOWN;
			}
		}
	}

	return UNKNOWN;
}

void extract_route(char * buffer, int buffer_size, int *method_last_index, int *route_begin_index, int *route_end_index) {
	*route_begin_index = *method_last_index + 1;
	for(int i = *route_begin_index + 1; i < buffer_size; ++i) {
		if(buffer[i] == ' ') {
			*route_end_index = i;
			break;
		}
	}
}

char * extract_file_path(char * buffer, int *route_begin_index, int *route_end_index) {
	int index = *route_end_index;

	for(int i = *route_begin_index; i < *route_end_index; ++i) {
		if(buffer[i] == '?') {
			index = i;
			break;
		}
	}

	char * filePath = new char[index - *route_begin_index + 1];
	int i = 0;
	for(i = *route_begin_index; i < index; ++i) {
		filePath[i - *route_begin_index] = buffer[i];
	}
	filePath[i - *route_begin_index] = '\0';
	return filePath;
}

inline bool file_exists (const std::string& filename) {
  struct stat buffer;
  return (stat (filename.c_str(), &buffer) == 0 && S_ISREG(buffer.st_mode));
}

int calc(char * buffer, int *body_begin_index) {
	char state = '^';
	char op = '+';

	int num1 = 0;
	int num2 = 0;

	for(int i = *body_begin_index; i < strlen(buffer); ++i) {
		char cur = buffer[i];
		switch(state) {
			case '^': {
				if(buffer[i] == ':') {
					state = ':';
				}
				break;
			}
			case ':': {
				if(buffer[i] == '"') {
					state = 'f';
				}
				break;
			}
			case 'f': {
				if(buffer[i] >= '0' && buffer[i] <= '9') {
					num1 *= 10;
					num1 += buffer[i] - '0';
				} else if(buffer[i] == '+' || buffer[i] == '-' || buffer[i] == '*' || buffer[i] == '/') {
					op = buffer[i];
					state = 's';
				}
				break;
			}
			case 's': {
				if(buffer[i] >= '0' && buffer[i] <= '9') {
					num2 *= 10;
					num2 += buffer[i] - '0';
				} else if(buffer[i] == '"') {
					state = 'e';
					break;
				}
				break;
			}
		}
		if(state == 'e') {
			break;
		}
	}

	int result = 0;

	switch(op) {
		case '+': {
			return num1 + num2;
		}
		case '-': {
			return num1 - num2;
		}
		case '*': {
			return num1 * num2;
		}
		case '/': {
			return (num2 == 0) ? 0 : num1 / num2;
		}

	}

	return 0;
}

void extract_body(char * buffer, int buffer_size, int *body_begin_index) {
	for(int i = 3; i < buffer_size; ++i) {
		if(*body_begin_index == -1 && buffer[i - 1] == '\n' && buffer[i - 2] == '\r' && buffer[i - 3] == '\n') {
			*body_begin_index = i;
			break;
		}
	}
}

content_type get_content_type(const char * filename) {
	cout << "get_content_type = " << filename << endl;
	int len = strlen(filename);
	for(int i = len - 1; i >=0; --i) {
		if(filename[i] == '.') {
			switch(len - i - 1) {
				case 2: {
					if(filename[i + 1] == 'j' && filename[i + 2] == 's') {
						return JS;
					}
					break;
				}
				case 3: {
					if(filename[i + 1] == 'p' && filename[i + 2] == 'n' && filename[i + 3] == 'g') {
						return PNG;
					}
					break;
				}
				case 4: {
					if(filename[i + 1] == 'h' && filename[i + 2] == 't' && filename[i + 3] == 'm' && filename[i + 4] == 'l') {
						return HTML;
					}
					break;
				}
			}
			break;
		}
	}
	return OCTET_STREAM;
}

http_version extract_http_version(char * buffer, int buffer_size, int *route_end_index) {
	for(int i = *route_end_index + 1; i < buffer_size; ++i) {
		if(buffer[i] == '\n' || buffer[i] == '\r') {
			if(i - *route_end_index - 1 == 8
				&& buffer[i - 8] == 'H'
				&& buffer[i - 7] == 'T'
				&& buffer[i - 6] == 'T'
				&& buffer[i - 5] == 'P'
				&& buffer[i - 4] == '/'
				&& buffer[i - 3] == '1'
				&& buffer[i - 2] == '.') {

				if(buffer[i - 1] == '0') {
					return HTTP_1_0;
				} else if(buffer[i - 1] == '1') {
					return HTTP_1_1;
				}
				return UNKNOWN_VERSION;
			} else if(i - *route_end_index - 1 == 6
				&& buffer[i - 6] == 'H'
				&& buffer[i - 5] == 'T'
				&& buffer[i - 4] == 'T'
				&& buffer[i - 3] == 'P'
				&& buffer[i - 2] == '/'
				&& buffer[i - 1] == '2') {
				return HTTP_2;
			} else {
				return UNKNOWN_VERSION;
			}
		}
	}
	return UNKNOWN_VERSION;
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
	HTTP-request handler
*/

void http_request_handler(int fd) {
	log << "FD " << fd << ": http_request_handler" << endl;

	pid_t pid = getpid();

	static char buffer[BUFFER_SIZE];
	int recv_result = recv(fd, buffer, BUFFER_SIZE, MSG_NOSIGNAL);

	log << "PID " << pid << ": " << "fd = " << fd << ", recv_result = " << recv_result << ", errno = " << errno << endl;

	if(recv_result == 0 && (errno != EAGAIN)) {
		log << "FD " <<  fd << " close" << endl;
		shutdown(fd, SHUT_RDWR);
		close(fd);
		return;
	}

	log << "===header===" << endl;
	log << buffer;
	log << "============" << endl;

	int method_last_index = 0;

	method _method = extract_method(buffer, BUFFER_SIZE, &method_last_index);

	if(_method == UNKNOWN) {
		log << "Incorrect method!" << endl;
		send(fd, header_400, strlen(header_400), MSG_NOSIGNAL);
		send(fd, body_400, strlen(body_400), MSG_NOSIGNAL);
		shutdown(fd, SHUT_RDWR);
		close(fd);
		return;
	}

	int route_begin_index = 0;
	int route_end_index = 0;

	extract_route(buffer, BUFFER_SIZE, &method_last_index, &route_begin_index, &route_end_index);

	http_version _http_version =  extract_http_version(buffer, BUFFER_SIZE, &route_end_index);

	char * file_path = extract_file_path(buffer, &route_begin_index, &route_end_index);

	log << "file_path = '" << file_path << "'" << endl;

	switch(_method) {
		case GET: {
			string full_file_path(global_args.directory);

			if(strcmp(file_path, root_directory) == 0) {
				full_file_path += default_page;
			} else {
				full_file_path += file_path;
			}

			log << "full_file_path = '" << full_file_path << "'" << endl;

			ifstream file_input(full_file_path.c_str(), std::ios::binary);

			if(file_input && file_exists(full_file_path)) {
				std::string content( (std::istreambuf_iterator<char>(file_input) ), (std::istreambuf_iterator<char>()) );

				content_type _content_type = get_content_type(full_file_path.c_str());

				switch(_content_type) {
					case HTML: {

						string html_response =  "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Type: text/html\nContent-Length: " + to_string(content.size()) + "\n\n";

						log << "response: " << html_response << endl;

						send(fd, html_response.c_str(), html_response.size(), MSG_NOSIGNAL);

						break;
					}
					case JS: {
						send(fd, header_200_text_javascript, strlen(header_200_text_javascript), MSG_NOSIGNAL);
						break;
					}
					case PNG: {
						send(fd, header_200_image_png, strlen(header_200_image_png), MSG_NOSIGNAL);
						break;
					}
					default: {
						send(fd, header_200_application_octet_stream, strlen(header_200_application_octet_stream), MSG_NOSIGNAL);
						break;
					}
				}

				send(fd, content.c_str(), content.size(), MSG_NOSIGNAL);
			} else {
				log << "File '" << full_file_path << "' not found" << endl;
				send(fd, header_404, strlen(header_404), MSG_NOSIGNAL);
			}

			break;
		}
		case POST: {

			if(strcmp(file_path, route_calc) != 0) {
				send(fd, header_404, strlen(header_404), MSG_NOSIGNAL);
			} else {

				int body_begin_index = -1;

				extract_body(buffer, BUFFER_SIZE, &body_begin_index);

				if(body_begin_index == -1) {
					send(fd, header_400, strlen(header_400), MSG_NOSIGNAL);
					send(fd, body_400, strlen(body_400), MSG_NOSIGNAL);
				} else {

					int calc_result = calc(buffer, &body_begin_index);

					string json_result = "{\n\t\"result\": " + to_string(calc_result) + "\n}";

					send(fd, header_200_application_json, strlen(header_200_application_json), MSG_NOSIGNAL);
					send(fd, json_result.c_str(), json_result.size(), MSG_NOSIGNAL);
				}
			}
			break;
		}
	}

	delete[] file_path;

	log << "FD " <<  fd << " close" << endl;
	shutdown(fd, SHUT_RDWR);
	close(fd);
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
			http_request_handler(fd);
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

				if(events[ei].events & EPOLLERR) {
					log << "FD " << fd << ": EPOLLERR" << endl;
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
