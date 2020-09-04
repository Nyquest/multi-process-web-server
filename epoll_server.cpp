#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

using namespace std;

#define MAX_EVENTS 32
#define BUFFER_SIZE 4096

char const *header200 = "HTTP/1.0 200 OK\nServer: MultiProcessWebServer v0.1\nContent-Type: text/html\n\n";
char const *body = "<b>Hello, World!</b>";
char const *header400 = "HTTP/1.0 400 Bad Request \nServer: MultiProcessWebServer v0.1\nConnection: Close\nContent-Type: text/html\n\n";
char const *body400 = "<em>Bad request!</em>";

#define handle_error(msg) \
	do { perror(msg); exit(EXIT_FAILURE); } while (0)

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

method extract_method(char * buffer, int buffer_size) {

	for(int p = 0; p < buffer_size; ++p) {
		if(buffer[p] == ' ') {
			if(p == 4 && buffer[0] == 'P' && buffer[1] == 'O' && buffer[2] == 'S' && buffer[3] == 'T') {
				return POST;
			} else if (p == 3 && buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T') {
				return GET;
			} else {
				return UNKNOWN;
			}
		}
	}

	return UNKNOWN;
}

int main() {

	cout << "Welcome to Epoll server" << endl;

	int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	struct sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(12345);
	SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int bindResult = ::bind(master_socket, (struct sockaddr *)(&SockAddr), sizeof(SockAddr));

	if (bindResult == -1) {
		handle_error("bind");
	}

	cout << "bindResult = " << bindResult << endl; 

	set_nonblock(master_socket);

	cout << "SOMAXCONN = " << SOMAXCONN << endl;

	listen(master_socket, SOMAXCONN);

	int EPoll = epoll_create1(0);

	struct epoll_event event;
	event.data.fd = master_socket;
	event.events = EPOLLIN;
	epoll_ctl(EPoll, EPOLL_CTL_ADD, master_socket, &event);

	while(true) {
		struct epoll_event events[MAX_EVENTS];
		cout << "wait events..." << endl; 
		int N = epoll_wait(EPoll, events, MAX_EVENTS, -1);
		cout << "N = " << N << endl;
		for(int i=0; i < N; i++) {
			if(events[i].data.fd == master_socket) {
				cout << "New Connection..." << endl;
				int slave_socket = accept(master_socket, 0, 0);
				cout << "Connection accepted: " << slave_socket << endl;
				set_nonblock(slave_socket);

				struct epoll_event event;
				event.data.fd = slave_socket;
				event.events = EPOLLIN;

				epoll_ctl(EPoll, EPOLL_CTL_ADD, slave_socket, &event);
			} else {
				cout << "read from fd " << events[i].data.fd << endl;
				static char buffer[BUFFER_SIZE];
				int recv_result = recv(events[i].data.fd, buffer, BUFFER_SIZE, MSG_NOSIGNAL);

				cout << "recv_result for " << events[i].data.fd << " = " << recv_result << endl;

				if(recv_result == 0 && (errno != EAGAIN)) {
					cout << "Close " <<  events[i].data.fd << endl;
					shutdown(events[i].data.fd, SHUT_RDWR);
					close(events[i].data.fd);
				} else {
					cout << "===header===" << endl;
					cout << buffer;
					cout << "============" << endl;

					bool has_error = false;


					method _method = extract_method(buffer, BUFFER_SIZE);

					cout << "method = " << _method << endl;

					if(_method == UNKNOWN) {
						cout << "Incorrect method!" << endl;
						send(events[i].data.fd, header400, strlen(header400), MSG_NOSIGNAL);
						send(events[i].data.fd, body400, strlen(body400), MSG_NOSIGNAL);
						has_error = true;
					}

					cout << "fd " << events[i].data.fd << " has_error " << has_error << endl;

					if(!has_error) {
						cout << "send 200" << endl;
						send(events[i].data.fd, header200, strlen(header200), MSG_NOSIGNAL);
						send(events[i].data.fd, body, strlen(body), MSG_NOSIGNAL);	
					}

					cout << "shutdown fd " << events[i].data.fd << endl;
					shutdown(events[i].data.fd, SHUT_RDWR);
					close(events[i].data.fd);

				}

			}
		}

	}
}