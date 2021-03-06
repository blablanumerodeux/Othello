#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

int port; 
int pid;

// Methods header
void gameOn(int args[2]);
int openPipeServerToGui();
void sendMessageByPipe(int descPipe, char* msg);
static void stopChild(int signo); 
int descServerToGui;


int main(int argc, char **argv)
{
	int sockfd, new_fd, rv, sin_size, numbytes; 
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr their_adr;
	char buf[100];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; 	// use my IP


	if (signal(SIGTERM, stopChild) == SIG_ERR) {
		printf("Could not attach signal handler\n");
		return EXIT_FAILURE;
	}

	port=atoi(argv[1]);

	rv = getaddrinfo(NULL, argv[1], &hints, &servinfo);

	if (rv != 0) 
	{
		fprintf(stderr, "\ngetaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// Creating socket and linking
	for(p = servinfo; p != NULL; p = p->ai_next) 
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			perror("\nserver: socket\n");
			continue;
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			perror("\nserver: bind\n");
			continue;
		}
		break;

	}

	if (p == NULL)
	{
		fprintf(stderr, "\nserver: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); 	// free struct

	listen(sockfd, 5);

	while(1)
	{

		printf("Server : Waiting for a connexion\n");
		fflush(stdout);

		sin_size = sizeof(their_adr);
		new_fd = accept(sockfd, &their_adr, &sin_size);

		// We create a new processus
		if(!fork())
		{
			// I am your father
			// for now we only accept one connection
			// we should use the select method of the socket
			// we quit the while
			break;
		}
		else
		{
			// We stop receiving from the socket of the main server
			// because the son doesn't need to access to the main socket
			close(sockfd);

			int arguments[2] = {atoi(argv[1]), new_fd};

			// We respond to the opponent and launch the game !!
			gameOn(arguments);

			close(new_fd);
			exit(0);
		} 
	}
	exit(0);
}

void gameOn(int args[2])
{

	int numbytes; 
	char buf[100];

	// We receive the first message from the opponent
	if((numbytes = recv(args[1], buf, 100-1, 0)) == -1)
	{
		perror("recv");
		exit(1);
	}

	// The first packet we receive can be either an acknoledgment or a demand of connection
	// so, we create the client ONLY IF it's a demand of connection
	buf[numbytes] = '\0';

	char* token = strtok (buf,",");	
	char* cmd = token;
	token = strtok(NULL, buf);
	char* portOponent = token;
	token = strtok(NULL, buf);
	
	if (strcmp(cmd, "demande")==0)
	{
		printf("Server : Receipt of a demand of connection\n");
		fflush(stdout);

		char portInChar[6]; 
		sprintf(portInChar, "%d", port);
		// We send an acknoledgment
		pid_t pid_serv = fork();
		if(pid_serv != 0)
		{       
			// I am the father
			pid = (int) pid_serv;
		}
		else
		{
			if (execlp("./client.o", "client.o", portOponent, portInChar, "1", NULL))
			{
				printf("Server : Execlp didn't work\n");
				fflush(stdout);
				strerror(errno);
			}
		}
		
		// We notify the GUI that the current player is J2 (since he receives the demand)
		descServerToGui = openPipeServerToGui(); 	// Open the communication pipe
		char msg[5] = "j-J2";						// set the message to send
		sendMessageByPipe(descServerToGui, msg);	// process to the sending
	}
	// else it's an acknoledgement 
	else if (strcmp(cmd, "ack")==0)
	{
		printf("Server : Receipt of an ack of connection\n");
		fflush(stdout);

		descServerToGui = openPipeServerToGui();
	}
	else 
	{
		printf("Server : Wrong message : %s \n", buf);
		fflush(stdout);
		//TODO : send a message to the interface and exit properly 
		exit(0);
	}


	printf("Server : Connected\n");
	fflush(stdout);

	// We open a pipe to send commands to the gui
	descServerToGui = openPipeServerToGui();

	// With this we can notify the gui that the connection is established 
	// now we can receive all the moves of the opponent
	while (1)
	{
		// We flush the buffer before refilling it
		if((numbytes = recv(args[1], buf, 100-1, 0)) == -1)
		{
			perror("recv");
			exit(1);
		}
		if (numbytes>0)
		{
			buf[numbytes] = '\0';	

			// Here we'll notify the gui about all the moves of the opponent
			sendMessageByPipe(descServerToGui, buf);
		}
	}
}

int openPipeServerToGui(){
	// We open a pipe to send commands to the gui
	char serverToGui[] = "serverToGui.fifo";
	if((descServerToGui = open(serverToGui, O_WRONLY)) == -1) 
	{
		fprintf(stderr, "Unable to open the entrance of the named pipe.\n");
		perror("open");
		exit(EXIT_FAILURE);
	}
	
	return descServerToGui;
}

void sendMessageByPipe(int descPipe, char* msg){
	write(descPipe, msg, strlen(msg));
	memset(msg, 0, sizeof(msg));
} 

static void stopChild(int signo)
{
	printf("Server : Closing server...\n");
	fflush(stdout);
	
	int res;
	if((res = close(descServerToGui))==-1)
	{
		perror("Server : close");
		exit(EXIT_FAILURE);
	}

	if (pid !=0 ) 
	{
		int resk;
		int status = 0;
		pid_t w;
		if (( resk = kill(pid, SIGTERM)) == -1) {
			perror("kill ");
			exit(EXIT_FAILURE); 
		}
		if ((w = waitpid(pid, &status, 0)) == -1) {
			printf("GUI : waitpid on pidClient error\n");
			fflush(stdout);
		}

	}

	printf("Server : Stopped\n");
	fflush(stdout);
	exit(0);
}
