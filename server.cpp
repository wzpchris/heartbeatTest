#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>
using namespace std;
#define BUFFER_SIZE  1024

enum Type { HEART = 1, OTHER };

struct PACKET_HEAD {
	enum Type type;
	int length;
};

void *heart_handler(void *arg);

class Server {
private:
	struct sockaddr_in server_addr;
	socklen_t server_addr_len;
	int listen_fd;
	int max_fd;
	fd_set master_set;
	fd_set working_set;
	struct timeval timeout;
	map<int, pair<string, int> > mmap; //记录客户端fd-><ip, count>
public:
	Server(int port);
	~Server();
	void Bind();
	void Listen(int queue_len = 5);
	void Accept();
	void Run();
	void Recv(int nums);
	friend void *heart_handler(void *arg);
};

Server::Server(int port)
{
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htons(INADDR_ANY);
	server_addr.sin_port = htons(port);

	//create socket to listen
	listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if(listen_fd < 0)
	{
		cout << "create socket failed";
		exit(1);
	}

	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

Server::~Server()
{
	for(int fd = 0; fd <= max_fd; ++fd)
	{
		if(FD_ISSET(fd, &master_set))
		{
			close(fd);
		}
	}
}

void Server::Bind()
{
	if(-1 == (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr))))
	{
		cout << "server bind failed";
		exit(1);
	}
	cout << "bind successfully.\n";
}

void Server::Listen(int queue_len)
{
	if(-1 == listen(listen_fd, queue_len))
	{
		cout << "server listen failed";
		exit(1);
	}

	cout << "listen successfully.\n";
}

void Server::Accept()
{
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
	if(new_fd < 0)
	{
		cout << "server accept failed";
		exit(1);
	}

	string ip(inet_ntoa(client_addr.sin_addr));
	cout << ip << ":" << client_addr.sin_port << " new connection was accepted.\n";
	mmap.insert(make_pair(new_fd, make_pair(ip, 0)));
	
	FD_SET(new_fd, &master_set);
	if(new_fd > max_fd)
	{
		max_fd = new_fd;
	}
}

void Server::Recv(int nums)
{
	for(int fd = 0; fd <= max_fd; ++fd)
	{
		if(FD_ISSET(fd, &working_set))
		{
			bool close_conn = false;
			PACKET_HEAD head;
			recv(fd, &head, sizeof(head), 0);
			
			if(head.type == HEART)
			{
				mmap[fd].second = 0;
				cout << "received heart-beat from client.\n";
			}else {
				//数据包,通过head.length确认数据包长度
				if(head.type == 0 && head.length == 0)
				{
					close_conn = true;
				}
			}

			if(close_conn)
			{
				close(fd);
				FD_CLR(fd, &master_set);
				if(fd == max_fd)
				{
					while(FD_ISSET(max_fd, &master_set) == false)
						--max_fd;
				}
			}
		}
	}
}

void Server::Run()
{
	pthread_t id;
	int ret = pthread_create(&id, NULL, heart_handler, (void*)this);
	if(ret != 0)
	{
		cout << "cannot create heart_beat checking thread.\n";
	}

	max_fd = listen_fd;
	FD_ZERO(&master_set);
	FD_SET(listen_fd, &master_set);

	while(1)
	{
		FD_ZERO(&working_set);
		memcpy(&working_set, &master_set, sizeof(master_set));
		
		timeout.tv_sec = 30;
		timeout.tv_usec = 0;
		
		int nums = select(max_fd + 1, &working_set, NULL, NULL, &timeout);
		if(nums < 0)
		{
			cout << "select error!";
			exit(1);
		}

		if(nums == 0)
		{
			continue;
		}

		if(FD_ISSET(listen_fd, &working_set))
		{
			Accept();
		}else{
			Recv(nums);
		}
	}
}

//thread function 
void *heart_handler(void *arg)
{
	cout << "the heartbeat checking thread started.\n";
	Server *s = (Server *)arg;
	while(1)
	{
		map<int, pair<string, int> >::iterator it = s->mmap.begin();
		for(; it != s->mmap.end(); )
		{
			//3s*5没有收到心跳包,判定客户端掉线
			if(it->second.second == 5)
			{
				cout << "the client " << it->second.first << " has be offline.\n";
				int fd = it->first;
				close(fd);
				FD_CLR(fd, &s->master_set);
				if(fd == s->max_fd)
				{
					while(FD_ISSET(s->max_fd, &s->master_set) == false)
						s->max_fd--;
				}

				s->mmap.erase(it++);
			}
			else if(it->second.second < 5 && it->second.second >= 0)
			{
				it->second.second += 1;
				++it;
			}else {
				++it;
			}
		}
		sleep(3);
	}
}

int main()
{
	Server server(15000);
	server.Bind();
	server.Listen();
	server.Run();
	return 0;
}
