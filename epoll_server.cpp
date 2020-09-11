#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <signal.h>

using namespace std;

#define MAX_EVENTS 32
#define BUFFER_SIZE 4096

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

void signalHandler(int sig, siginfo_t *si, void *ptr) {
	cout << "Master caught signal: " << strsignal(sig) << endl;
	if(sig == SIGINT) {
		cout << "SIGINT caught" << endl;
		exit(0);
	}
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

int main() {

	cout << "Welcome to Epoll server" << endl;

	string target_directory = "/usr/src/multi-process-web-server/static-site";

	if(target_directory.length() > 0 && target_directory.back() == '/') {
		target_directory.pop_back();
	}

	cout << "target_directory = " << target_directory << endl;

	struct sigaction act;
	act.sa_sigaction = signalHandler;
	act.sa_flags = SA_SIGINFO;

	if(sigaction(SIGINT, &act, NULL) == -1) {
		cout << "Error of sigaction SIGINT" << endl;
	}

	int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	int flag = 1;
	if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
		handle_error("Reuse addr error");
	}

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
		for(int ei = 0; ei < N; ei++) {

			int fd = events[ei].data.fd;

			if(fd == master_socket) {
				cout << "New Connection..." << endl;
				int slave_socket = accept(master_socket, 0, 0);
				cout << "Connection accepted: " << slave_socket << endl;
				set_nonblock(slave_socket);

				struct epoll_event event;
				event.data.fd = slave_socket;
				event.events = EPOLLIN;

				epoll_ctl(EPoll, EPOLL_CTL_ADD, slave_socket, &event);
			} else {
				cout << "read from fd " << fd << endl;
				static char buffer[BUFFER_SIZE];
				int recv_result = recv(fd, buffer, BUFFER_SIZE, MSG_NOSIGNAL);

				cout << "recv_result for " << fd << " = " << recv_result << endl;

				if(recv_result == 0 && (errno != EAGAIN)) {
					cout << "Close " <<  fd << endl;
					shutdown(fd, SHUT_RDWR);
					close(fd);
				} else {
					cout << "===header===" << endl;
					cout << buffer;
					cout << "============" << endl;

					int method_last_index = 0;

					method _method = extract_method(buffer, BUFFER_SIZE, &method_last_index);

					if(_method == UNKNOWN) {
						cout << "Incorrect method!" << endl;
						send(fd, header_400, strlen(header_400), MSG_NOSIGNAL);
						send(fd, body_400, strlen(body_400), MSG_NOSIGNAL);
						shutdown(fd, SHUT_RDWR);
						close(fd);
						continue;
					}

					int route_begin_index = 0;
					int route_end_index = 0;

					extract_route(buffer, BUFFER_SIZE, &method_last_index, &route_begin_index, &route_end_index);

					http_version _http_version =  extract_http_version(buffer, BUFFER_SIZE, &route_end_index);

					char * file_path = extract_file_path(buffer, &route_begin_index, &route_end_index);

					cout << "file_path = '" << file_path << "'" << endl;

					switch(_method) {
						case GET: {
							string full_file_path(target_directory);

							if(strcmp(file_path, root_directory) == 0) {
								full_file_path += default_page;
							} else {
								full_file_path += file_path;
							}

							cout << "full_file_path = '" << full_file_path << "'" << endl;

							ifstream file_input(full_file_path.c_str(), std::ios::binary);

							if(file_input && file_exists(full_file_path)) {
								std::string content( (std::istreambuf_iterator<char>(file_input) ), (std::istreambuf_iterator<char>()) );

								content_type _content_type = get_content_type(full_file_path.c_str());

								switch(_content_type) {
									case HTML: {
										send(fd, header_200_text_html, strlen(header_200_text_html), MSG_NOSIGNAL);
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
								cout << "File '" << full_file_path << "' not found" << endl;
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

					cout << "shutdown fd " << fd << endl;
					shutdown(fd, SHUT_RDWR);
					close(fd);

				}

			}
		}

	}
}