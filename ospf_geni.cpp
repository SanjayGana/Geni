#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include <pthread.h>
#include <string>
#include <queue>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>

#define MAX 100000
#define INF 1000000000

using namespace std;

int sock;
bool stop;

void raise_error(string msg)
{
	cout<<msg<<endl;
}

struct edge
{
	int j;
};

float max(int a, float b)
{
	if( a > b)
		return (float)a;
	else
		return b;
}

float timeDiff(struct timeval t1, struct timeval t2)
{
	return (t1.tv_sec - t2.tv_sec) + (t1.tv_usec - t2.tv_usec)/1000000;
}

string get32Bit(int x)
{
	string bit_string;
	int i;
	for( i = 0 ; i < 32; i++)
	{
		bit_string.push_back(((x>>(31 - i))&1) + 48);
	}
	return bit_string;
}

int getNum32Bit(string x)
{
	int num = 0;
	int i;
	for( i = 0; i < 32; i++)
	{
		num = num*2 + (x[i] - '0');
	}
	return num;
}

string getHostFromId(int id)
{
	char hostname[MAX];
	strcpy(hostname , "node-");
	char charid[MAX];
	sprintf(charid,"%d",id);
	strcat(hostname , charid);
	string host(hostname);

	return host;
}

int getIDfromHost(string host)
{
	return atoi(host.substr(5).c_str());
}

int getSrcId(char* hello)
{
	return atoi(hello + 5);
}

/*
   * Class router which contains all necessary data stuctures to perform the functions required of a router
   */
class Router 
{
	public:

		int id;
		int hello_int;
		int lsa_int;
		int spf_int;
		fstream infile, outfile;

		int N;
		vector<edge> neighbours;

		map<int,int> last_recv_LSA;
		map<int, map<int,int> > LSAinfo;
		map<int,long long int> send_time;


		Router()
		{
			id = 1;
			hello_int = 1;
			lsa_int = 5;
			spf_int = 6;
		}

		void constructNeighbours();
		void processReceivedMsg();
};

/*
   *Function which computes neighbour information for a node
   */
void Router::constructNeighbours()
{
	int m;
	infile>>N;
	infile>>m;
	int i;
	for( i = 0; i < m ; i++)
	{
		int u,v;
		infile>>u>>v;
		if( id == u || id == v)
		{
			edge e;
			e.j = (u == id)? v : u;
			neighbours.push_back(e);
		}
	}
}

/*
   * Function which processes received messages on port
   */
void Router::processReceivedMsg()
{

	struct sockaddr_in server_addr;
	struct hostent *host;

	server_addr.sin_port = htons(20027);
	server_addr.sin_family = AF_INET;
	bzero(&(server_addr.sin_zero), 8);

	unsigned int addr_len = sizeof (struct sockaddr);
	struct sockaddr_in client_addr;
	char recv_data[MAX];
	int i;
	int bytes_read = recvfrom(sock, recv_data, MAX, MSG_DONTWAIT,
			(struct sockaddr *) &client_addr, &addr_len);
	recv_data[bytes_read] = '\0';
	if(bytes_read > 0)

	{
		if( recv_data[0] == 'H' && recv_data[5] != 'R' )
		{
			char hello_reply[MAX];
			strcpy(hello_reply,"HELLOREPLY");
			int srcid = getSrcId(recv_data);
			int link_cost;
			for( i = 0 ; i < neighbours.size(); i++)
			{
				if( neighbours[i].j == srcid )
				{
					link_cost = 0 ;
				}
			}
			strcat(hello_reply,get32Bit(id).c_str());
			strcat(hello_reply,get32Bit(srcid).c_str());
			strcat(hello_reply,get32Bit(link_cost).c_str());

			host = (struct hostent *) gethostbyname((char*)(getHostFromId(srcid).c_str()));
			server_addr.sin_addr = *((struct in_addr *) host->h_addr);
			sendto(sock, hello_reply , strlen(hello_reply), 0,
					(struct sockaddr *) &server_addr, sizeof (struct sockaddr));
		}
		else if( recv_data[0] == 'H' && recv_data[5] == 'R')
		{
			string hello_reply(recv_data);
			int dstid = getNum32Bit( hello_reply.substr(10,32));
			int link_cost = getNum32Bit( hello_reply.substr(74,32));

			struct timeval recvtime;
			gettimeofday(&recvtime, NULL);
			long long int rtt = recvtime.tv_sec*1000000 + recvtime.tv_usec - send_time[dstid];
			LSAinfo[id][dstid] = rtt;
		}
		else if(recv_data[0] == 'L')
		{
			string lsa_packet(recv_data);
			bool forward = false;
			int interface_id = getIDfromHost(inet_ntoa(client_addr.sin_addr));
			int srcid = getNum32Bit( lsa_packet.substr(3,32) );				
			int seq_num = getNum32Bit( lsa_packet.substr(35,32) );
			if ( last_recv_LSA.count(srcid) == 0)
			{
				if( srcid != id)
				{
					last_recv_LSA[srcid] = seq_num;
					forward = true;
				}
			}
			else
			{
				if( last_recv_LSA[srcid] < seq_num)
				{
					last_recv_LSA[srcid] = seq_num;
					forward = true;
				}
			}
			if(forward)
			{
				int num_entries = getNum32Bit(lsa_packet.substr(67,32));
				for( i = 0; i < num_entries ; i++)
				{
					int node = getNum32Bit( lsa_packet.substr(99 + i*64,32));
					int cost = getNum32Bit( lsa_packet.substr(99 + 32 + i*64,32));
					LSAinfo[srcid][node] = cost;
				}

				for( i = 0 ; i < neighbours.size() ; i++)
				{
					if ( interface_id != neighbours[i].j )
					{
						host = (struct hostent *) gethostbyname((char*)(getHostFromId(neighbours[i].j).c_str()));
						server_addr.sin_addr = *((struct in_addr *) host->h_addr);
						sendto(sock, recv_data , strlen(recv_data), 0,
								(struct sockaddr *) &server_addr, sizeof (struct sockaddr));
					}
				}

			}

		}		

	}
}

/*
   * function to send Hello packet to neighbours
   */
void sendHello(void *arg)
{
	Router* r = (Router*) arg;
	struct sockaddr_in server_addr;
	struct hostent *host;

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(20027);
	bzero(&(server_addr.sin_zero), 8);
	char hello_packet[MAX];
	strcpy(hello_packet,"HELLO");
	char charid[MAX];
	sprintf(charid,"%d",r->id);
	strcat(hello_packet,charid);

	int i;

	for( i = 0 ; i < r->neighbours.size() ; i++)
	{
		host = (struct hostent *) gethostbyname((char*)(getHostFromId(r->neighbours[i].j).c_str()));
		server_addr.sin_addr = *((struct in_addr *) host->h_addr);
		sendto(sock, hello_packet , strlen(hello_packet), 0,
				(struct sockaddr *) &server_addr, sizeof (struct sockaddr));
		struct timeval sendtime;
		gettimeofday(&sendtime,NULL);
		r->send_time[r->neighbours[i].j] = sendtime.tv_sec*1000000 + sendtime.tv_usec;

	}
}

int seq_num = 0;
/*
   * function to send LSA packet to neighbours
   */
void sendLSA(void *arg)
{

	Router *r = (Router*) arg;
	struct sockaddr_in server_addr;
	struct hostent *host;

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(20027);
	bzero(&(server_addr.sin_zero), 8);

	char LSApacket[MAX];

	int i;
	strcpy(LSApacket,"LSA");
	strcat(LSApacket,get32Bit(r->id).c_str());
	strcat(LSApacket,get32Bit(seq_num).c_str());
	strcat(LSApacket,get32Bit(r->LSAinfo[r->id].size()).c_str());

	map<int,int>::iterator it;
	for( it = r->LSAinfo[r->id].begin() ; it != r->LSAinfo[r->id].end() ; ++it )
	{
		strcat(LSApacket,get32Bit(it->first).c_str());	
		strcat(LSApacket,get32Bit(it->second).c_str());	
	}

	for( i = 0 ; i < r->neighbours.size() ; i++)
	{
		host = (struct hostent *) gethostbyname((char*)(getHostFromId(r->neighbours[i].j).c_str()));
		server_addr.sin_addr = *((struct in_addr *) host->h_addr);
		sendto(sock, LSApacket , strlen(LSApacket), 0,
				(struct sockaddr *) &server_addr, sizeof (struct sockaddr));
	}
	seq_num++;
}

int Ntime = 0;
/*
   * function to send construct topology from LSA information
 */
void ospf(void *arg)
{
	Router *r = (Router*) arg;

	Ntime += r->spf_int;
	int i;

	map<int,pair<int,int> > djikstra_label;
	map<int,map<int,int> >::iterator it;
	map<int,int> distance;

	for( it = r->LSAinfo.begin(); it != r->LSAinfo.end() ; it++)
	{
		djikstra_label[it->first] = make_pair(INF , -1);
		distance[it->first] = INF;
	}
	djikstra_label[r->id] = make_pair(0,r->id);
	distance.erase(r->id);

	int next_add = r->id;
	int last_added = r->id;
	while( last_added != -1)
	{
		map<int,int>::iterator it1;
		for ( it1 = r->LSAinfo[last_added].begin() ; it1 != r->LSAinfo[last_added].end() ; ++it1 )
		{
			if( djikstra_label[last_added].first + it1->second < djikstra_label[it1->first].first )
			{
				djikstra_label[it1->first] = make_pair( it1->second + djikstra_label[last_added].first , last_added);
				distance[it1->first] = djikstra_label[it1->first].first;
			}
		}

		int min_distance = INF;
		next_add = -1;
		for ( it1 = distance.begin() ; it1 != distance.end() ; ++it1)
		{
			if(it1->second < min_distance)
			{
				min_distance = it1->second;
				next_add = it1->first;
			}
		}
		last_added = next_add;
		if(last_added != -1)
			distance.erase(last_added);
	}


	cout<<"Routing Table for Node "<<r->id<<" at Time "<<Ntime<<endl;
	r->outfile<<"Routing Table for Node "<<r->id<<" at Time "<<Ntime<<endl;

	cout<<"Destination Cost       Path"<<endl;
	r->outfile<<"Destination Cost       Path"<<endl;
	for( it = r->LSAinfo.begin() ; it != r->LSAinfo.end() ; it++)
	{
		if( it->first != r->id)
		{
			if( djikstra_label[it->first].first != INF)
			{
				char charid[MAX]; 
				int node = it->first;
				string path;
				while( djikstra_label[node].second != node )
				{
					sprintf(charid,"%d",node);
					path.append(charid);
					path.push_back('-');
					node = djikstra_label[node].second;
				}
				sprintf(charid,"%d",node);
				path.append(charid);

				cout<<it->first<<"             "<<djikstra_label[it->first].first<<"        "<<path<<endl;
				r->outfile<<it->first<<"             "<<djikstra_label[it->first].first<<"        "<<path<<endl;
			}
		}
	}


}
			

int main(int argc,char *argv[])
{
	int i;
    struct hostent *host;
    char send_data[1024],recv_data[1024];

	srand(time(NULL));

	Router *r = new Router();

	// parse the command line arguments
	for( i = 1; i < argc ; i+=2)
	{
		if (strcmp(argv[i],"-i") == 0)
		{
			r->id = atoi(argv[i+1]);
			
		}
		else if (strcmp(argv[i],"-f") == 0)
		{
			FILE *f = fopen(argv[i+1],"r");
			if( f == NULL)
			{
				raise_error("Infile missing!");
				return 0;
			}
			fclose(f);
			r->infile.open(argv[i+1],ios::in);
		}
		else if (strcmp(argv[i],"-o") == 0)
		{
			char outfile_name[MAX];
			strcpy(outfile_name,argv[i+1]);
			strcat(outfile_name,"-");
			char charid[MAX];
			sprintf(charid,"%d",i);
			strcat(outfile_name,charid);
			r->outfile.open(argv[i+1],ios::out);
		}
		else if (strcmp(argv[i],"-h") == 0)
		{
			if(r->hello_int <= 0)
			{
				raise_error("Invalid interval for hello");
		    	return 0;
			}
			r->hello_int = atoi(argv[i+1]);
		}
		else if (strcmp(argv[i],"-a") == 0)
		{
			if(r->hello_int <= 0)
			{
				raise_error("Invalid interval for lsa");
		    	return 0;
			}
			r->lsa_int = atoi(argv[i+1]);
		}
		else if (strcmp(argv[i],"-s") == 0)
		{
			if(r->hello_int <= 0)
			{
				raise_error("Invalid interval for spf");
		    	return 0;
			}
			r->spf_int = atoi(argv[i+1]);
		}
		else
		{
			raise_error("Error in arguments");
			return 0;
		}
	}

	r->constructNeighbours();
   
   	// open the socket	
	
	struct sockaddr_in server_addr;
	int addr_len;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        raise_error("Socket Error");
        exit(1);
    }
	int port = 20027;
	
	host = (struct hostent *) gethostbyname((char*)(getHostFromId(r->id).c_str()));
    server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    bzero(&(server_addr.sin_zero), 8);

    if (bind(sock, (struct sockaddr *) &server_addr,
            sizeof (struct sockaddr)) == -1) {
        raise_error("Bind Error");
        exit(1);
    }


    printf("Router %d using port %d\n",r->id, port);
    fflush(stdout);
	stop = false;	

	
	struct timeval start,now;
	struct timeval next_hello,next_lsa,next_ospf;
	gettimeofday(&start,NULL);
	next_hello.tv_sec = start.tv_sec + r->hello_int;
	next_lsa.tv_sec = start.tv_sec + r->lsa_int;
	next_ospf.tv_sec = start.tv_sec + r->spf_int;
	while(true)
	{
		gettimeofday(&now,NULL);
		if( now.tv_sec - start.tv_sec >= 300)
		{
			stop = true;
			break;
		}
		if( timeDiff(now,next_hello) >= 0)
		{
			
			next_hello.tv_sec = now.tv_sec + r->hello_int;
			sendHello((void *)r);
		}
		if( timeDiff(now,next_lsa) >= 0)
		{
			next_lsa.tv_sec = now.tv_sec + r->lsa_int;
			sendLSA((void *)r);
		}
		if( timeDiff(now,next_ospf) >= 0)
		{
			next_ospf.tv_sec = now.tv_sec +  r->spf_int;
			ospf((void *)r);
		}
		r->processReceivedMsg();
	}


	delete r;	
	return 0;

}
