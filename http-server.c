#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFERSIZE 4096
#define MAXCONNECTREQ 5

/*
 * Display error message and exit program
 */
static void die(const char *errmsg)
{
	perror(errmsg);
	exit(1);
}

/*
 * Initialize socket to listen from port
 */
static int initServSock(unsigned short portNum)
{
	struct sockaddr_in serverAddress;
	int serverSocket;
	
	/*
	 * socket: create parent socket
	 */
	if ((serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		die("Socket() failure");
	}

	//Make address struct
	memset(&serverAddress, 0, sizeof(serverAddress));
	
	//This is internet address family type
	serverAddress.sin_family = AF_INET;
	
	//Let system figure out IP Address
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//This is the port to listen on
	serverAddress.sin_port = htons(portNum);
	
	/*
	 * bind: associate parent socket with port
	 */
	if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
	{
		die("Bind() failure");
	}
	
	/*
	 * listen: make socket ready to accept any connection requests
	 */
	if (listen(serverSocket, MAXCONNECTREQ) < 0)
	{
		die("Listen() failure");
	}

	//return the socket number for server
	return (int)serverSocket;
}

/*
 * Completes any static file request and
 * returns the HTTP status code
 */
static int completeFileReq(const char *web_root, const char *requestURI, int clientSocket)
{
	FILE *file = NULL;
	int status;
	int endFile;
	char *file_path = NULL;
	char sendBuffer[BUFFERSIZE];
	struct stat fileSt;
	size_t num;
	
	if((file_path = (char *)malloc(strlen(requestURI)+strlen(web_root)+100)) == NULL)
	{
		die("Malloc() failure");
	}
	strcpy(file_path, web_root); //Copy web_root to file path
	strcat(file_path, requestURI); //Append requestURI to file path
	endFile = strlen(file_path) - 1;
	if(file_path[endFile] == '/')
	{
		strcat(file_path, "index.html"); //Add index.html if requestURI has / at end
	}
	
	/*
	 * Check if file is directory send Forbidden status if so
	 */
	if(S_ISDIR(fileSt.st_mode) && (stat(file_path, &fileSt) == 0))
	{
		status = 403;
		addHTTPStatus(status, clientSocket);
		goto next;
	}
	
	/*
	 * Open file and send Not Found status if error
	 */
	if((file = fopen(file_path, "rb")) == NULL)
	{
		status = 404;
		addHTTPStatus(status, clientSocket);
		goto next;
	}
	
	//Send OK status if no error
	status = 200;
	addHTTPStatus(status, clientSocket);
	
	while((num = fread(sendBuffer, 1, sizeof(sendBuffer), file)) > 0)
	{
		if(send(clientSocket, sendBuffer, num, 0) != num)
		{
			perror("\nSend() failure"); //Break out of loop but don't exit program
			break;
		}
	}

	//Check for fread error
	if(ferror(file))
	{
		perror("Fread() failure");
	}

//Skip to here
next:
	free(file_path);
	if(file)
	{
		fclose(file);
	}
	
	return status; //Return current status after completing file request
}

static inline const char *statusInfo(int status)
{
	switch(status)
	{
		case 200:
			return "OK";
			break;
		case 201:
			return "Created";
			break;
		case 202:
			return "Accepted";
			break;
		case 204:
			return "No Content";
			break;
		case 301:
			return "Moved Permanently";
			break;
		case 302:
			return "Moved Temporarily";
			break;
		case 304:
			return "Not Modified";
			break;
		case 400:
			return "Bad Request";
			break;
		case 401:
			return "Unauthorized";
			break;
		case 403:
			return "Forbidden";
			break;
		case 404:
			return "Not Found";
			break;
		case 500:
			return "Internal Server Error";
			break;
		case 501:
			return "Not Implemented";
			break;
		case 502:
			return "Bad Gateway";
			break;
		case 503:
			return "Service Unavailable";
			break;
		default:
			return "Unrecognized Status Code";
			break;
	}
}

/*
 * Modified send function that checks for errors and
 * returns -1 if there are any errors
 */
ssize_t errSend(int socket, const char *buffer)
{
	size_t buffer_length;
	ssize_t resource;
	buffer_length = strlen(buffer);
	if((resource = send(socket, buffer, buffer_length, 0)) != buffer_length)
	{
		perror("Send() failure");
		return -1;  //indicate there is an error
	}
	else
	{
		return resource;
	}
}

/*
 * Add status info and format as
 * HTML for everything but status 200
 */
void addHTTPStatus(int status, int clientSocket)
{
	const char *status_info = statusInfo(status);
	char statusBuffer[1000];

	sprintf(statusBuffer, "HTTP/1.0 %d ", status); //Add status code
	strcat(statusBuffer, status_info); //Add status info
	strcat(statusBuffer, "\r\n"); //Add blank line
	strcat(statusBuffer, "\r\n"); //Add blank line to show end of header

	if (status == 200)
	{
		errSend(clientSocket, statusBuffer);
	}
	else
	{
		//Add html body for status and info for everything but 200
		char htmlBody[1000];
		sprintf(htmlBody, "<html><body>\n""<h1>%d %s</h1>\n""</body></html>\n", status, status_info);
		strcat(statusBuffer, htmlBody); //Append htmlBody string to statusBuffer
		errSend(clientSocket, statusBuffer);
	}
}

/*
 * Initialize a socket to connect with the 
 * mdb-lookup-server
 */
static int initMdbSocket(const char *mdb, unsigned short mdbLookupPort)
{
	struct sockaddr_in mdbServerAddress;
	struct hostent *host;
	int mdbSocket;
	char *mdbServIP;
	
	/*
	 * Retrieve the server Internet Protocol from name
	 */
	host = gethostbyname(mdb);
	if (host == NULL)
	{
		die("Gethostbyname() failure");
	}
	mdbServIP = inet_ntoa(*(struct in_addr *)host->h_addr);
	
	/*
	 * Initialize persistent socket
	 */
	if((mdbSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		die("Socket() failure");
	}
	
	/*
	 * Create the mdb server address
	 */
	memset(&mdbServerAddress, 0, sizeof(mdbServerAddress));
	
	mdbServerAddress.sin_family = AF_INET;
	mdbServerAddress.sin_addr.s_addr = inet_addr(mdbServIP);
	mdbServerAddress.sin_port = htons(mdbLookupPort);
	
	/*
	 * Connect to persistent mdb socket
	 */
	if(connect(mdbSocket, (struct sockaddr *)&mdbServerAddress, sizeof(mdbServerAddress)) <0)
	{
		die("Connect() failure");
	}
	
	/*
	 * Return the socket number for use
	 */
	return mdbSocket;
}

/*
 * Completes any mdb-lookup or mdb-lookup?key= requests
 * by producing a table of mdb values
 * Returns the status number for error checking
 */
static int completeMdbReq(const char *requestURI, FILE *mdb, int clientSocket, int mdbLookupSocket)
{
	const char *form, *mdbKey, *mdbKeyFull, *htmlTableHeader, *htmlTableFooter, *htmlTableRow;
	char mdbLine[1000];
	int status, htmlRowNum;
	
	htmlRowNum = 1; //Row number in table
	status = 200; //Set http status
	addHTTPStatus(status, clientSocket);
	
	//HTML form for user to input requests
	form = 
		"<html><body>\n"
		"<h1>mdb-lookup</h1>\n"
		"<p>\n"
		"<form method=GET action=/mdb-lookup>\n"
		"lookup:  <input type=text name=key>\n"
		"<input type=submit>\n"
		"</form>\n"
		"<p>\n"
		;
	
	if(errSend(clientSocket, form) <0)
	{
		return status;
	}
	
	mdbKeyFull = "/mdb-lookup?key=";
	
	/*
	 * Check if requestURI has /mdb-lookup?key= and lookup that key
	 */
	if(strncmp(requestURI, mdbKeyFull, strlen(mdbKeyFull)) == 0)
	{
		mdbKey = (requestURI + strlen(mdbKeyFull));
		
		fprintf(stderr, "Looking for [%s]: ", mdbKey);
		
		if(errSend(mdbLookupSocket, mdbKey) <0)
		{
			return status;
		}
		
		if(errSend(mdbLookupSocket, "\n") <0)
		{
			return status;
		}
		
		htmlTableHeader = "<p><table border>";

		if(errSend(clientSocket, htmlTableHeader) <0)
		{
			return status;
		}
	
		while(1)
		{
			/*
			 * Read each line of mdb-lookup-server
			 */
			if(fgets(mdbLine, sizeof(mdbLine), mdb) == NULL)
			{
				if(ferror(mdb))
				{
					perror("\nmdb-lookup-server connection failure");
				}
				else
				{
					fprintf(stderr, "\nmdb-lookup-server connection terminated");
				}
				return status;
			}

			if(strcmp("\n", mdbLine) == 0)
			{
				break; //If there is a blank line then break loop
			}
			
			/*
			 * Format the HTML table with odd numbered
			 * rows in thistle color and even numbered
			 * rows in normal white color
			 */
			if(((htmlRowNum+=1)%2) == 1)
			{
				htmlTableRow = "\n<tr><td>";
			}
			else
			{
				htmlTableRow = "\n<tr><td bgcolor=thistle>";
			}
			
			if(errSend(clientSocket, htmlTableRow) <0)
			{
				return status;
			}
			if(errSend(clientSocket, mdbLine) <0)
			{
				return status;
			}
		}
		
		htmlTableFooter = "\n</table>\n";
		if(errSend(clientSocket, htmlTableFooter) <0)
		{
			return status;
		}
	}
	
	/*
	 * Send required end lines of HTML page
	 */
	if(errSend(clientSocket, "</body></html>\n") <0)
	{
		return status;
	}

return status;  //Return the status code that was sent to browser	
}

int main(int argc, char *argv[])
{
	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
	{
		die("Signal() failure");
	}

	if(argc != 5)
	{
		fprintf(stderr, "Usage: ./http-server <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>\n");
		exit(1);
	}
	
	unsigned short server_port = atoi(argv[1]); //port to listen on
	unsigned short mdbLookupPort = atoi(argv[4]); 
	const char *web_root = argv[2];
	const char *mdbLookupHost = argv[3];
	int serverSocket; //parent socket
	int clientSocket; //child socket
	int mdbSocket; //mdb-lookup socket
	int status; //status code number
	char lineBuf[1000];
	char reqLineBuf[1000];
	struct sockaddr_in clientAddress; //client's address
	unsigned int clientLength; //byte size of client's address
	FILE *clientFile;
	FILE *mdb; //mdb file
	
	
	mdbSocket = initMdbSocket(mdbLookupHost, mdbLookupPort); //Initialize mdb socket
	
	/*
	 * Open and check that mdb file is not NULL
	 */
	if((mdb = fdopen(mdbSocket, "r")) == NULL)
	{
		die("fdopen() failure");
	}
	
	serverSocket = initServSock(server_port); //Initialize server socket
	
	/*
	 * main loop: wait for connection request, complete, then close connection
	 */
	while(1)
	{
		clientLength = sizeof(clientAddress);

		/*
		 * accept: wait for a connection request
		 */
		if ((clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientLength)) < 0)
		{
			die("Accept() failure");
		}
	
		if ((clientFile = fdopen(clientSocket, "r")) == NULL)
		{
			die("fdopen() failure");
		}
		
		if (fgets(reqLineBuf, sizeof(reqLineBuf), clientFile) == NULL)
		{
			//socket is closed
			status = 400;
			goto next;
		}
	
		char *token_separators = "\t \r\n";
		char *method = strtok(reqLineBuf, token_separators);
		char *requestURI = strtok(NULL, token_separators);
		char *httpVersion = strtok(NULL, token_separators);
		char *moreOnReqLine = strtok(NULL, token_separators);
		
		/*
		 * check for only 3 items on req line
		 */
		if (!requestURI || !httpVersion || !method || moreOnReqLine)
		{
			status = 501;
			addHTTPStatus(status, clientSocket);
			goto next;
		}
	
		/*
		 * check that only GET method is used
		 */
		if (strcmp(method, "GET") != 0)
		{
			status = 501;
			addHTTPStatus(status, clientSocket);
			goto next;
		}
		
		/*
		 * check that only HTTP/1.1 or HTTP/1.0 is HTTP Version
		 */
		if (strcmp(httpVersion, "HTTP/1.1") != 0 && strcmp(httpVersion, "HTTP/1.0") != 0)
		{
			status = 501;
			addHTTPStatus(status, clientSocket);
			goto next;
		}
		
		/*
		 * check that requestURI starts with "/"
		 */
		if (*requestURI != '/' || !requestURI)
		{
			status = 400;
			addHTTPStatus(status, clientSocket);
			goto next;
		}
		
		/*
		 * skip through the headers
		 */
		while(1)
		{
			if(fgets(lineBuf, sizeof(lineBuf), clientFile) == NULL)
			{
				status = 400;
				goto next;
			}
			
			if (strcmp("\n", lineBuf) == 0 || strcmp("\r\n", lineBuf) == 0)
			{
				//No more headers to skip so leave loop
				break;
			}
		}
		
		/*
		 * Check if request is static file or mdb-lookup
		 */		
		if((strcmp(requestURI, "/mdb-lookup") == 0) || (strncmp(requestURI, "/mdb-lookup?", strlen("/mdb-lookup?")) == 0))
		{
			status = completeMdbReq(requestURI, mdb, clientSocket, mdbSocket); //Complete the mdb-lookup request
		}
		else
		{ 
			status = completeFileReq(web_root, requestURI, clientSocket); //Complete the static file request
		}

//Skip to here		
next:
		fprintf(stderr, "%s \"%s %s %s\" %d %s\n", inet_ntoa(clientAddress.sin_addr), method, requestURI, httpVersion, status, statusInfo(status));
		
		fclose(clientFile); //close socket
	}
	
	return 0;
}
