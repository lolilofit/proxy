//from os repo

#include <errno.h>
#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<netdb.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include<signal.h>
#include <fcntl.h>
#include <sys/file.h>

#define MAX_HEADER_SIZE 6000
#define MAX_BODY_SIZE 6000
int FDS_SIZE = 200;
extern int errno;


struct HttpParams {
	char path[MAX_HEADER_SIZE + 1];
	char host[MAX_HEADER_SIZE + 1];
	char method[30];
	char protocol[30];
};

struct HttpAnswer {
	char status[4];
};

struct List {
	char str[MAX_HEADER_SIZE + MAX_BODY_SIZE + 1];
	int len;
	struct List* next;
};

struct ClientHostList {
	int client;
	int remote_host;
	int should_cache;
	char* url;
	struct ClientHostList* next;
	struct CacheUnit* cache_unit;
	int is_cli_alive;
	int is_host_done;
	int is_first;
};

struct WaitingOne {
	int fd;
	int blocks_transfered;
	struct WaitingOne* next;
};

struct CacheUnit {
	struct List* mes_head;
	struct List* last_mes;
	char* url;
	int is_downloaded;
	struct WaitingOne* waiting;
	struct CacheUnit* next;
};


struct Cache {
	struct CacheUnit* units_head;
	int max_id;
};


void add_mes(struct CacheUnit* unit, char* mes, int mes_len) {
	struct List* add_this;
	add_this = (struct List*)malloc(sizeof(struct List));
	(add_this->str)[0] = '\0';
	memcpy(add_this->str, mes, mes_len);
	(add_this->str)[mes_len] = '\0';
	add_this->len = mes_len;
	add_this->next = NULL;

	unit->last_mes->next = add_this;
	unit->last_mes = unit->last_mes->next;
}


struct CacheUnit* find_cache_by_url(struct Cache* cache, char* url) {
	printf("find in cache by url : %s, size %d\n", url, strlen(url));
	struct CacheUnit* cur = cache->units_head->next;
	while (cur != NULL) {
		printf("compare %s and %s\n", url, cur->url);
		if (strcmp(url, cur->url) == 0) {
			printf("FOUND in cache\n");
			return cur;
		}
		cur = cur->next;
	}
	printf("not found\n");
	return NULL;
}


struct CacheUnit* init_cache_unit(int id, char* url) {
	struct CacheUnit* cache_unit;
	cache_unit = (struct CacheUnit*)malloc(sizeof(struct CacheUnit));
	cache_unit->next = NULL;
	cache_unit->mes_head = (struct List*)malloc(sizeof(struct List));
	cache_unit->mes_head->next = NULL;
	cache_unit->last_mes = cache_unit->mes_head;
	(cache_unit->mes_head->str)[0] = '\0';
	cache_unit->mes_head->len = 0;
	cache_unit->url = url;

	struct WaitingOne* head;
	head = (struct WaitingOne*)malloc(sizeof(struct WaitingOne));
	head->next = NULL;
	head->fd = -1;
	head->blocks_transfered = 0;
	cache_unit->waiting = head;

	return cache_unit;
}




struct Cache* init_cache(struct Cache* cache) {
	cache = (struct Cache*)malloc(sizeof(struct Cache));
	cache->max_id = 0;
	char* head_mes = (char*)malloc(sizeof(char));
	head_mes[0] = '\0';
	cache->units_head = init_cache_unit(0, head_mes);
	return cache;
}


struct CacheUnit* add_cache_unit(struct Cache* cache, int id, char* url) {
	struct CacheUnit* cache_unit = NULL;
	struct CacheUnit* cur = cache->units_head->next;
	if (cur == NULL) {
		cache_unit = init_cache_unit(id, url);
		cache->units_head->next = cache_unit;
		return cache_unit;
	}

	while (cur != NULL) {
		if (strcmp(cur->url, url) == 0)
			return NULL;
		if (cur->next == NULL) {
			cache_unit = init_cache_unit(id, url);
			cur->next = cache_unit;
			return cache_unit;
		}
		cur = cur->next;
	}
	return cache_unit;
}


void dealloc_cache(struct Cache* cache) {
	struct CacheUnit* unit = cache->units_head;
	struct CacheUnit* prev;
	while (unit) {
		struct List* cur_list = unit->mes_head;
		while (cur_list != NULL) {
			free(cur_list);
			cur_list = cur_list->next;
		}
		free(unit->url);
		prev = unit;
		unit = unit->next;
		free(prev);
	}
}

int add_waiting(struct CacheUnit* cache_unit, int client) {
	printf("add waiting\n");
	if (cache_unit == NULL)
		return -1;

	printf("wait now:\n");
	struct WaitingOne* cur = cache_unit->waiting;
	while (cur != NULL) {
		printf("wait - %d\n", cur->fd);
		cur = cur->next;
	}

	struct WaitingOne* tmp;
	tmp = cache_unit->waiting->next;
	struct WaitingOne* new_waiting;
	new_waiting = (struct WaitingOne*)malloc(sizeof(struct WaitingOne));
	new_waiting->next = tmp;
	new_waiting->fd = client;
	new_waiting->blocks_transfered = 0;
	cache_unit->waiting->next = new_waiting;


	printf("waiting after adding\n");
	struct WaitingOne* cur1 = cache_unit->waiting;
	while (cur1 != NULL) {
		printf("wait - %d\n", cur1->fd);
		cur1 = cur1->next;
	}

	printf("add waiting end\n");
	return 0;
}

struct Cache* cache;

void pars_path(struct HttpParams* response) {
	char* tmp;
	tmp = strstr(response->path, response->host);
	if (tmp != NULL) {
		char *end = tmp + strlen(response->host);
		strcpy(response->path, end);
	}
}

void pars_line(char* line, struct HttpParams* response, int is_first) {
	char* ptr;
	int flag = 0, i = 0;

	ptr = strtok(line, " ");

	if (is_first == 1)
		strcpy(response->method, ptr);

	while (ptr != NULL) {
		if (flag == 1)
			strcpy(response->host, ptr);
		if (strcmp(ptr, "Host:") == 0)
			flag = 1;

		if (i == 1 && is_first == 1)
			strcpy(response->path, ptr);
		if (i == 2 && is_first == 1)
			strcpy(response->protocol, ptr);
		i++;
		ptr = strtok(NULL, " ");
	}
}

void pars_answer_line(char* line, struct HttpAnswer* remote_answer, int is_first) {
	char* ptr;
	int flag = 0, i = 0;

	ptr = strtok(line, " ");
	while (ptr != NULL) {
		if (i == 1 && is_first == 1)
			strcpy(remote_answer->status, ptr);
		i++;
		ptr = strtok(NULL, " ");
	}
}


void parse_request(char* request, struct HttpParams* response, struct HttpAnswer* remote_answer) {
	int i = 0, flag = 0;
	char* ptr;
	char tmp[MAX_HEADER_SIZE + 1];
	if (response != NULL) {
		(response->host)[0] = '\0';
		(response->path)[0] = '\0';
	}

	struct List* head;
	head = (struct List*)malloc(sizeof(struct List));
	head->next = NULL;

	struct List* cur = head;

	ptr = strtok(request, "\r\n");

	while (ptr != NULL) {
		struct List* el;
		el = (struct List*)malloc(sizeof(struct List));
		strcpy(el->str, ptr);
		el->next = NULL;
		cur->next = el;
		cur = el;
		if (remote_answer != NULL)
			ptr = NULL;
		else
			ptr = strtok(NULL, "\r\n");
	}

	head = head->next;
	while (head != NULL) {
		if (flag == 0) {
			if (response != NULL)
				pars_line(head->str, response, 1);
			if (remote_answer != NULL)
				pars_answer_line(head->str, remote_answer, 1);
			flag = 1;
		}
		else {
			if (response != NULL)
				pars_line(head->str, response, 0);
		}
		cur = head;
		head = head->next;
		free(cur);
	}

	if (response != NULL) {
		pars_path(response);
		printf("path is %s\n", response->path);
	}
}

struct Host {
	char host[MAX_HEADER_SIZE + 1];
	int port;
};

int divide_host(char* response, struct Host* host) {
	char *tmp;
	char port[MAX_HEADER_SIZE + 1];

	tmp = strstr(response, ":");
	if (tmp != NULL) {
		strcpy(port, tmp + strlen(":"));
		strncpy(host->host, response, (tmp - response));
		(host->host)[tmp - response] = '\0';
		host->port = atoi(port);
	}
	else {
		strcpy(host->host, response);
		host->port = 80;
	}
	return 0;
}


int get_remote_socket(char* host, char* port, struct Host* clear_host) {
	int remote_port = 80, sc, res;
	struct sockaddr_in sc_addr;

	struct hostent *hp;
	hp = gethostbyname(clear_host->host);
	if (hp == NULL) {
		printf("wrong url\n");
		return -1;
	}

	sc = socket(PF_INET, SOCK_STREAM, 0);
	if (sc < 0) {
		printf("can't create remote socket\n");
		return -1;
	}

	sc_addr.sin_family = AF_INET;
	sc_addr.sin_addr.s_addr = ((struct in_addr*)((hp->h_addr_list)[0]))->s_addr;
	printf("remote_adr is: %s, CONNECT TO %s, %d\n", inet_ntoa(sc_addr.sin_addr), clear_host->host, clear_host->port);

	sc_addr.sin_port = htons(clear_host->port);
	if ((res = connect(sc, (struct sockaddr*)&sc_addr, sizeof(sc_addr))) < 0) {
		printf("can't connect to the remote socket\n");
		return -1;
	}
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	setsockopt(sc, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
	return sc;
}


struct ClientHostList* find_related(struct ClientHostList* head, int find_him) {

	struct ClientHostList* cur = head->next;
	struct ClientHostList* prev = head;
	while (cur != NULL) {
		if ((cur->client == find_him) || (cur->remote_host == find_him))
			return prev;
		cur = cur->next;
		prev = prev->next;
	}
	return NULL;
}


int transfer_cached(struct CacheUnit* cache_unit, int client) {
	printf("transfer to particular waiter\n");
	struct List* cur = cache_unit->mes_head->next;
	while (cur != NULL) {
		if (write(client, cur->str, cur->len) < 0) {
			printf("can't write to remote host\n");
			return -1;
		}
		cur = cur->next;
	}
	return 0;
}

int transfer_quest(struct CacheUnit* cache_unit, int client_fd) {
	if (cache_unit == NULL) {
		printf("null cache\n");
		return -1;
	}
	if (cache_unit->waiting == NULL) {
		printf("null waiting\n");
		return -1;
	}

	struct WaitingOne* client = cache_unit->waiting->next;
	struct WaitingOne* prev;

	while (client != NULL) {
		if (client->fd == client_fd) {
			struct List* cur = cache_unit->mes_head->next;
			int count = 0;
			printf("transfer to cli %d blocks %d\n", client->fd, client->blocks_transfered);

			while (cur != NULL) {
				if (count >= client->blocks_transfered) {
					if (write(client->fd, cur->str, cur->len) < 0) {
						printf("can't write to waiting client\n");
						return -1;
					}
				}
				count++;
				cur = cur->next;
			}
			client->blocks_transfered = count;
			printf("transfered now %d\n", client->blocks_transfered);
		}
		client = client->next;
	}
	//printf("transfer to waiters end\n");
	return 0;

}

int transfer_to_waiters(struct CacheUnit* cache_unit) {
	//printf("transfer to waiters\n");
	if (cache_unit == NULL) {
		printf("null cache\n");
		return -1;
	}
	if (cache_unit->waiting == NULL) {
		printf("null waiting\n");
		return -1;
	}

	struct WaitingOne* client = cache_unit->waiting->next;
	struct WaitingOne* prev;

	while (client != NULL) {
		struct List* cur = cache_unit->mes_head->next;
		int count = 0;
		printf("transfer to cli %d blocks %d\n", client->fd, client->blocks_transfered);

		while (cur != NULL) {
			if (count >= client->blocks_transfered) {
				if (write(client->fd, cur->str, cur->len) < 0) {
					printf("can't write to waiting client\n");
					return -1;
				}
			}
			count++;
			cur = cur->next;
		}
		client->blocks_transfered = count;
		printf("transfered now %d\n", client->blocks_transfered);
		client = client->next;
	}
	//printf("transfer to waiters end\n");
	return 0;
}

int free_waiting(struct CacheUnit* cache_unit) {
	if (cache_unit == NULL)
		return -1;
	if (cache_unit->waiting == NULL)
		return -1;

	struct WaitingOne* cur = cache_unit->waiting->next;
	struct WaitingOne* prev;
	while (cur != NULL) {
		prev = cur;
		cur = cur->next;
		free(prev);
	}
	cache_unit->waiting->next = NULL;
	return 0;
}


int transfer_to_remote(struct ClientHostList* related, struct pollfd* fds, int nfd) {

	char buf[MAX_HEADER_SIZE + MAX_BODY_SIZE + 1];
	buf[0] = '\0';
	int remote_host, client;
	client = related->client;
	int readen = 0;
	readen = recv(client, buf, 2048, 0);
	if (readen < 0) {
		printf("error reading\n");
		return -1;
	}
	if (readen == 0) {
		return -1;
	}
	buf[readen] = '\0';

	struct HttpParams* param;
	param = (struct HttpParams*)malloc(sizeof(struct HttpParams));
	char copy[readen];
	memcpy(copy, buf, readen);
	copy[readen] = '\0';
	parse_request(copy, param, NULL);

	struct Host h;
	h.host[0] = '\0';
	h.port = -1;


	if (param->host != NULL) {
		if ((param->host)[0] != '\0')
			divide_host(param->host, &h);
		else {
			if (param->path != NULL) {
				char *http;
				http = strstr(param->path, "//");
				if (http != NULL) {
					char* slash;
					slash = strstr(http + 2, "/");
					if (slash != NULL) {
						strncpy(param->host, http + 2, (slash - http - 2));
						(param->host)[slash - http - 2] = '\0';
						strcpy(param->path, slash + 1);
					}
					else {
						strcpy(param->host, http + 2);
						strcpy(param->path, "/");
					}
				}
				else {
					char* slash;
					slash = strstr(param->path, "/");
					if (slash != NULL) {
						strncpy(param->host, param->path, (slash - param->path));
						strcpy(param->path, slash + 1);
					}
					else {
						strcpy(param->host, param->path);
						strcpy(param->path, "/");
					}
				}
				divide_host(param->host, &h);

				printf("\n\nNO HOST IN HEADER end with 0 \n\n");
			}
		}

	}
	else {
		if (param->path != NULL) {
			char *http;
			http = strstr(param->path, "//");
			if (http != NULL) {
				char* slash;
				slash = strstr(http + 2, "/");
				if (slash != NULL) {
					strncpy(param->host, http + 2, (slash - http - 2));
					(param->host)[slash - http - 2] = '\0';
					strcpy(param->path, slash + 1);
				}
				else {
					strcpy(param->host, http + 2);
					strcpy(param->path, "/");
				}
			}
			else {
				char* slash;
				slash = strstr(param->path, "/");
				if (slash != NULL) {
					strncpy(param->host, param->path, (slash - param->path));
					(param->host[slash - param->path]) = '\0';
					strcpy(param->path, slash + 1);
				}
				else {
					strcpy(param->host, param->path);
					strcpy(param->path, "/");
				}
			}
			divide_host(param->host, &h);
			printf("\n\n NO HOST IN HEADER end null\n\n");
			printf("Host is: %s, path %s\n", param->host, param->path);

		}
	}

	char* url;
	url = (char*)malloc(sizeof(char)*readen);
	url[0] = '\0';

	if (h.host != NULL)
		strncat(url, h.host, readen);

	if (param->path != NULL)
		strncat(url, param->path, readen);

	//printf("BEFORe find in cache by url %s, client %d, host %d \n", url, related->client, related->remote_host);
	struct CacheUnit* found = find_cache_by_url(cache, url);
	printf("found end\n");

	if ((strcmp(param->method, "GET") == 0) || (strcmp(param->method, "HEAD") == 0)) {
		if (found != NULL) {
			if (found->is_downloaded == 1) {
				printf("it's in cache and downloaded, transfer to %d\n", related->client);
				if (transfer_cached(found, related->client) < 0)
					return -1;
				return 1;
			}
			else {
				add_waiting(found, client);
				transfer_quest(found, client);
				related->remote_host = -1;
				related->cache_unit = NULL;
				return 1;
			}
		}
		else {
			printf("created unit will be added to cache (probably)\n");
			related->should_cache = 1;
			related->url = (char*)malloc(sizeof(char)*readen);
			(related->url)[0] = '\0';
			strncpy(related->url, url, readen);
		}
	}
	else {
		related->should_cache = 0;
	}
	free(url);

	remote_host = get_remote_socket(param->host, "", &h);
	if (remote_host < 0) {
		return -1;
	}

	if (write(remote_host, buf, readen) < 0) {
		printf("can't write to remote host\n");
		return -1;
	}
	related->remote_host = remote_host;
	related->cache_unit = NULL;
	related->is_first = 1;
	fds[nfd].fd = remote_host;
	fds[nfd].events = POLLIN;
	free(param);
	return 0;
}


int transfer_back(struct ClientHostList* related) {
	int count = 0;
	char buf[MAX_HEADER_SIZE + MAX_BODY_SIZE + 1] = "";
	int remote_host = related->remote_host;
	int client = related->client;
	int readen = 0;
	int  status, client_alive = 1;
	buf[0] = '\0';

	if (related->is_first == 1) {
		printf("first read\n");
		readen = read(remote_host, buf, MAX_HEADER_SIZE + MAX_BODY_SIZE);
		buf[readen] = '\0';

		if (readen < 0) {
			printf("error reading from remote host\n");
			related->is_host_done = 1;
			return -1;
		}

		if (readen == 0) {
			related->is_host_done = 1;
			return 1;
		}
		count += readen;

		if (write(client, buf, readen) < 0) {
			printf("can't write to remote host\n");
			related->is_cli_alive = 0;
		}
		struct HttpAnswer* ans;
		ans = (struct HttpAnswer*)malloc(sizeof(struct HttpAnswer));
		char* pos = strchr(buf, '\r');
		if (pos != NULL) {
			int line_end = pos - buf;
			char copy[line_end + 1];
			strncpy(copy, buf, line_end);
			copy[line_end] = '\0';
			parse_request(copy, NULL, ans);
		}
		if (related->url != NULL)
			printf("answer is: %d, url: %s, client %d, host %d\n", atoi(ans->status), related->url, related->client, related->remote_host);
		if (related->should_cache == 1) {
			struct CacheUnit* found = find_cache_by_url(cache, related->url);
			if (found == NULL) {
				if (atoi(ans->status) == 200) {
					printf("let's add to cache\n");
					(cache->max_id)++;
					related->cache_unit = add_cache_unit(cache, cache->max_id, related->url);
					related->cache_unit->is_downloaded = 0;
					add_mes(related->cache_unit, buf, readen);
				}
			}
		}
		free(ans);
		related->is_first = 0;
		return  2;
	}
	//printf("Read again\n");
	buf[0] = '\0';
	readen = read(remote_host, buf, MAX_HEADER_SIZE + MAX_BODY_SIZE);
	if (readen < 0) {
		if (errno == EWOULDBLOCK) {
			if (related->cache_unit != NULL) {
				related->cache_unit->is_downloaded = 1;
			}
			related->is_host_done = 1;
			return 2;
		}
		if (related->url != NULL)
			printf("error reading from remote host in while %s, total readed %d\n", related->url, count);
		related->is_host_done = 1;
		return 1;
	}
	if (readen == 0) {
		if (related->cache_unit != NULL) {
			related->cache_unit->is_downloaded = 1;
		}
		related->is_host_done = 1;
		return  1;
	}
	buf[readen] = '\0';
	count += readen;
	if (related->cache_unit != NULL)
		add_mes(related->cache_unit, buf, readen);

	if (related->is_cli_alive == 1) {
		if (write(client, buf, readen) < 0) {
			printf("can't write to client wrto %d, real %d\n", client, related->client);
			perror("cli fail");
			related->is_cli_alive = 0;
		}
	}

	if (related->cache_unit != NULL)
		transfer_to_waiters(related->cache_unit);
	return 2;
}


struct ClientHostList* remove_conn_info(struct pollfd* fds, int i, struct ClientHostList* prev, struct ClientHostList* related, struct ClientHostList* last) {
	if (prev != NULL) {
		if (related != NULL) {
			struct ClientHostList* tmp = related->next;
			prev->next = tmp;
			if (related->cache_unit != NULL)
				free(related->url);
			free(related);
			if (tmp == NULL)
				return prev;
		}
		else {
			return prev;
		}
	}
	return last;
}

int evacuate_fds(struct pollfd* fds) {
	int i, count = 0;
	for (i = 0; i < FDS_SIZE; i++) {
		if (fds[i].fd > 0) {
			if (count != i) {
				fds[count].fd = fds[i].fd;
				fds[count].events = POLLIN;
				fds[i].fd = -1;
			}
			count++;
		}
	}
	return count;
}

struct pollfd* resize_fds(struct pollfd* fds) {
	struct pollfd* new_fds;
	FDS_SIZE = FDS_SIZE + 200;
	new_fds = realloc(fds, FDS_SIZE);
	if (new_fds == NULL) {
		printf("can't realloc clients fds\n");
		return fds;
	}
	return new_fds;
}



int main(int argc, char* argv[]) {

	struct sigaction sig;
	sig.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sig, NULL);

	cache = init_cache(cache);

	int i, sc, client, remote_host, ret, res;
	struct sockaddr_in sc_addr;
	int addrlen = sizeof(sc_addr);
	sc = socket(AF_INET, SOCK_STREAM, 0);
	if (sc == -1) {
		close(sc);
		perror("can't create socket\n");
		return 1;
	}

	sc_addr.sin_family = AF_INET;
	sc_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	sc_addr.sin_port = htons(5001);
	if (bind(sc, (struct sockaddr*)&sc_addr, sizeof(sc_addr)) < 0) {
		perror("can't bind");
		return -1;
	}

	if (listen(sc, SOMAXCONN) < 0) {
		printf("errir listen\n");
		return -1;
	}

	struct pollfd* fds;
	fds = (struct pollfd*)malloc(sizeof(struct pollfd)*FDS_SIZE);
	memset(fds, -1, sizeof(fds));
	fds[0].fd = sc;
	fds[0].events = POLLIN;
	int nfd = 1, size_copy, j;

	struct ClientHostList* head;
	head = (struct ClientHostList*)malloc(sizeof(struct ClientHostList));
	head->client = -1;
	head->remote_host = -1;
	head->cache_unit = NULL;
	head->next = NULL;
	struct ClientHostList* last;
	last = head;

	while (1) {
		ret = poll(fds, nfd, -1);
		if (ret < 0) {
			printf("poll failed\n");
			break;
		}
		if (ret == 0) {
			continue;
		}
		size_copy = nfd;
		for (i = 0; i < size_copy; i++) {
			if (fds[i].revents == 0) {
				continue;
			}
			if (fds[i].revents != POLLIN) {
				continue;
			}
			if (fds[i].fd == sc) {
				client = 0;
				//while(client != -1) {
				if ((client = accept(fds[i].fd, (struct sockaddr*)&sc_addr, (socklen_t*)&addrlen)) < 0) {
					printf("error accept\n");
					continue;
				}
				printf("Client %d accepted\n\n", client);
				fds[nfd].fd = client;
				fds[nfd].events = POLLIN;
				nfd++;
				if (nfd >= FDS_SIZE)
					nfd = evacuate_fds(fds);
				if (nfd >= FDS_SIZE) {
					printf("clients overflow\n");
					fds = resize_fds(fds);
				}

				struct ClientHostList* new_client;
				new_client = (struct ClientHostList*)malloc(sizeof(struct ClientHostList));
				new_client->client = client;
				new_client->remote_host = -1;
				new_client->next = NULL;
				new_client->url = NULL;
				new_client->is_cli_alive = 1;
				new_client->is_host_done = 1;
				new_client->is_host_done = 0;

				last->next = new_client;
				last = new_client;

				printf("accepting sucess %d, nfd=%d\n", client, nfd);
				//}
			}
			else {
				if (head == NULL) {
					printf("head is null, should be allocated\n");
					continue;
				}
				struct ClientHostList* prev = find_related(head, fds[i].fd);
				if (prev == NULL) {
					printf("prev is null\n");
					fds[i].fd = -1;
					nfd = evacuate_fds(fds);
					continue;
				}
				struct ClientHostList* related = prev->next;
				if (related == NULL) {
					fds[i].fd = -1;
					nfd = evacuate_fds(fds);
					continue;
				}

				if (related->client == fds[i].fd) {
					printf("message came for %d (to host), and host is %d, client %d\n\n", fds[i].fd, related->remote_host);
					if (related->remote_host > 0) {
						for (j = 0; j < nfd; j++) {
							if (fds[j].fd == related->remote_host) {
								close(fds[j].fd);
								fds[j].fd = -1;
								related->remote_host = -1;
							}
						}
						related->remote_host = -1;
						related->is_host_done = 1;
						if (related->cache_unit != NULL)
							related->cache_unit->is_downloaded = 1;
						related->cache_unit = NULL;
					}

					if ((ret = transfer_to_remote(related, fds, nfd)) == -1) {
						printf("close client %d\n", fds[i].fd);
						close(related->client);
						fds[i].fd = -1;
						related->client = -1;
						related->is_cli_alive = 0;

						if (related->is_host_done == 1) {
							for (j = 0; j < nfd; j++) {
								if (fds[j].fd == related->remote_host) {
									close(fds[j].fd);
									related->remote_host = -1;
									fds[j].fd = -1;
								}
							}
							related->remote_host = -1;
							last = remove_conn_info(fds, i, prev, related, last);
						}
					}
					if (ret == 0) {
						//we added host
						related->is_host_done = 0;
						nfd++;
					}
				}
				else {
					//      printf("message came for %d (back to cli)\n\n", fds[i].fd);
					if (related->remote_host == fds[i].fd) {
						ret = transfer_back(related);
						if (ret != 2) {
							printf("host conn delete\n");
							close(fds[i].fd);
							fds[i].fd = -1;
							related->remote_host = -1;

							if (related->is_cli_alive == 0) {
								for (j = 0; j < nfd; j++) {
									if (fds[j].fd == related->remote_host) {
										close(fds[j].fd);
										related->remote_host = -1;
										fds[j].fd = -1;
									}
									if (fds[j].fd == related->client) {
										related->client = -1;
										close(fds[j].fd);
										fds[j].fd = -1;
									}
								}
								related->remote_host = -1;
								last = remove_conn_info(fds, i, prev, related, last);
							}
						}
					}
					else {
						printf("can't find info about thist desc\n");
					}
				}
			}
		}
		nfd = evacuate_fds(fds);
	}
	for (i = 0; i < nfd; i++) {
		if (fds[i].fd >= 0)
			close(fds[i].fd);
	}
	return 0;
}
