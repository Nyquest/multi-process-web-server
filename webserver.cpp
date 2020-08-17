#include<iostream>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[]) {
	int key = 0;
	char *host;
	int port;
	char *directory;
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
				cout << "Unknown key" << endl;
				break;
		}
	}

	cout << "host = " << host << endl;
	cout << "port = " << port << endl;
	cout << "directory = " << directory << endl;
}
