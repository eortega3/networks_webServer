/**
 * ToreroServe: A Lean Web Server
 * COMP 375 - Project 02
 *
 * This program should take two arguments:
 * 	1. The port number on which to bind and listen for connections
 * 	2. The directory out of which to serve files.
 *
 * Author 1: Eduardo Ortega
 * Author 2: Cecilia Barnhill
 */

// standard C libraries
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

// operating system specific libraries
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <bits/stdc++.h>

// C++ standard libraries
#include <vector>
#include <thread>
#include <string>
#include <iostream>
#include <fstream>
#include <system_error>
#include <regex>

#include "BoundedBuffer.hpp"

// Import Filesystem and shorten its namespace to "fs"
#include <filesystem>
namespace fs = std::filesystem;

using std::cout;
using std::string;
using std::vector;
using std::thread;
using std::regex;
using std::smatch;

// This will limit how many clients can be waiting for a connection.
static const int BACKLOG = 10;
static const int BUFFER_SIZE = 10;
static const int NUM_CONSUMERS = 8;

// forward declarations from started code
int createSocketAndListen(const int port_num);
void acceptConnections(const int server_sock, string rootDir);
void handleClient(const int client_sock, string rootDir);
void sendData(int socked_fd, const char *data, size_t data_length);
int receiveData(int socked_fd, char *dest, size_t buff_size);

//forward declarations from functions we add in
void sendHTTP400(string version, const int client_sock);
void sendHTTP404(string version, const int client_sock);
void sendHTTP200(string version, const int client_sock, string fileName);
string regexCheck(string request_string, string format);
string getVer(string requestChecked);
string getObj(string requestChecked);
void sendHead(string object, const int client_sock);
void sendObj(string object, const int client_sock);
string fileType(string fileName);
bool checkFile(string fileName);
bool checkDir(string thePath);
void createAndSendIndexAndHTTP200(string theDirectory, string version, const int client_sock);
void consumerThread(BoundedBuffer &buffer, string rootDir);

int main(int argc, char** argv) {

	/* Make sure the user called our program correctly. */
	if (argc != 3) {
		//print a proper error message informing user of proper usage
		cout << "INCORRECT USAGE!\n";
		cout << "Proper Format: ./(insert executable) (port #) (root directory)\n";
		cout << "Example: ./torero-serve 7101 WWW\n";
		exit(1);
	}

    //* Read the port number from the first command line argument. */
    int port = std::stoi(argv[1]);
	string rootDir = std::string(argv[2]);
	/* Create a socket and start listening for new connections on the
	 * specified port. */
	int server_sock = createSocketAndListen(port);

	/* Now let's start accepting connections. */
	acceptConnections(server_sock, rootDir);

    close(server_sock);

	return 0;
}

/**
 * Sends message over given socket, raising an exception if there was a problem
 * sending.
 *
 * @param socket_fd The socket to send data over.
 * @param data The data to send.
 * @param data_length Number of bytes of data to send.
 */
void sendData(int socked_fd, const char *data, size_t data_length) {
	//zero initialization
	int num_bytes_sent(0);
	int convertDataLength = static_cast<int>(data_length); //converting to compare ints
	//while loop to get to the data_length threshold and the num bytes sent is
	//positve
	while(num_bytes_sent < convertDataLength){
		int sent = send(socked_fd, data, data_length, 0);
		if (sent == -1) {
			std::error_code ec(errno, std::generic_category());
			throw std::system_error(ec, "send failed");
			exit(1);
		}
		else if(sent == 0)
		{
			cout << "Server connection closed unexpectedly. Goodbye. \n";
			exit(1);			
		}
		else
		{
			num_bytes_sent += sent;
		}
	}
}

/**
 * Receives message over given socket, raising an exception if there was an
 * error in receiving.
 *
 * @param socket_fd The socket to send data over.
 * @param dest The buffer where we will store the received data.
 * @param buff_size Number of bytes in the buffer.
 * @return The number of bytes received and written to the destination buffer.
 */
int receiveData(int socked_fd, char *dest, size_t buff_size) {
	int num_bytes_received = recv(socked_fd, dest, buff_size, 0);
	if (num_bytes_received == -1) {
		std::error_code ec(errno, std::generic_category());
		throw std::system_error(ec, "recv failed");
	}

	return num_bytes_received;
}

/**
 * Receives a request from a connected HTTP client and sends back the
 * appropriate response.
 *
 * @note After this function returns, client_sock will have been closed (i.e.
 * may not be used again).
 *
 * @param client_sock The client's socket file descriptor.
 * @param rootDir The root Directory entered in the command line arguments
 */
void handleClient(const int client_sock, string rootDir) {
	// Step 1: Receive the request message from the client
	char received_data[2048];
	int bytes_received = receiveData(client_sock, received_data, 2048);
	
	// Turn the char array into a C++ string for easier processing.
	string request_string(received_data, bytes_received);
		
	// Step 2: Parse the request string to determine what response to generate.
	// I recommend using regular expressions (specifically C++'s std::regex) to
	// determine if a request is properly formatted.

	//check the format of the request_string	
	string format("(GET\\s[\\w\\-\\./]*\\sHTTP/\\d\\.\\d)");
	string requestChecked(regexCheck(request_string, format));

	//from the checked request_string to requestChecked, obtain the object and
	//version of the request
	string version(getVer(requestChecked));
	string object(getObj(requestChecked));
	
	// Step 3: Generate HTTP response message based on the request you received.
	
	//check if the request is bad
	if ((requestChecked == "empty") || (version == "empty") || (object == "empty"))
	{
		sendHTTP400(version, client_sock);
	}
	else //means that request if good
	{
		object = rootDir + object;
		if ((checkDir(object)) && (object[object.length() - 1] == '/')) //checks the path to the object of interest to see if it is a directory
		{
			string indexToCheck(object + "index.html");
			if(checkFile(indexToCheck)) //checks if index.html exists
			{
				//index exists in directory or it is specified so we send the
				//200 OK response
				sendHTTP200(version, client_sock, (object + "index.html"));
			}
			else
			{
				//creates index.html and replaces the object being sent
				createAndSendIndexAndHTTP200(object, version, client_sock);
			}
		}
		else if(checkFile(object)) //checks if file exists
		{
			//file exists so we send the 200 OK response
			sendHTTP200(version, client_sock, object);
		}
		else
		{
			//request is not compatble or not found in the diretcory or not
			//specified, send the 404 not found error
			sendHTTP404(version, client_sock);
		}
	}	
	
	// Close connection with client.
	close(client_sock);
}

/**
 * Creates a new socket and starts listening on that socket for new
 * connections.
 *
 * @param port_num The port number on which to listen for connections.
 * @returns The socket file descriptor
 */
int createSocketAndListen(const int port_num) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Creating socket failed");
        exit(1);
    }

    /* 
	 * A server socket is bound to a port, which it will listen on for incoming
     * connections.  By default, when a bound socket is closed, the OS waits a
     * couple of minutes before allowing the port to be re-used.  This is
     * inconvenient when you're developing an application, since it means that
     * you have to wait a minute or two after you run to try things again, so
     * we can disable the wait time by setting a socket option called
     * SO_REUSEADDR, which tells the OS that we want to be able to immediately
     * re-bind to that same port. See the socket(7) man page ("man 7 socket")
     * and setsockopt(2) pages for more details about socket options.
	 */
    int reuse_true = 1;

	int retval; // for checking return values

    retval = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true,
                        sizeof(reuse_true));

    if (retval < 0) {
        perror("Setting socket option failed");
        exit(1);
    }

    /*
	 * Create an address structure.  This is very similar to what we saw on the
     * client side, only this time, we're not telling the OS where to connect,
     * we're telling it to bind to a particular address and port to receive
     * incoming connections.  Like the client side, we must use htons() to put
     * the port number in network byte order.  When specifying the IP address,
     * we use a special constant, INADDR_ANY, which tells the OS to bind to all
     * of the system's addresses.  If your machine has multiple network
     * interfaces, and you only wanted to accept connections from one of them,
     * you could supply the address of the interface you wanted to use here.
	 */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_num);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* 
	 * As its name implies, this system call asks the OS to bind the socket to
     * address and port specified above.
	 */
    retval = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (retval < 0) {
        perror("Error binding to port");
        exit(1);
    }

    /* 
	 * Now that we've bound to an address and port, we tell the OS that we're
     * ready to start listening for client connections. This effectively
	 * activates the server socket. BACKLOG (a global constant defined above)
	 * tells the OS how much space to reserve for incoming connections that have
	 * not yet been accepted.
	 */
    retval = listen(sock, BACKLOG);
    if (retval < 0) {
        perror("Error listening for connections");
        exit(1);
    }

	return sock;
}

/**
 * Sit around forever accepting new connections from client.
 *
 * @param server_sock The socket used by the server.
 * @param rootDir The root Directory entered in the command line arguments
 */
void acceptConnections(const int server_sock, string rootDir) {
 	BoundedBuffer buff(BUFFER_SIZE);
	for(size_t i = 0; i < NUM_CONSUMERS; ++i)
	{
		std::thread consumer(consumerThread, std::ref(buff), rootDir);
		consumer.detach();
	}

    while (true) {
        // Declare a socket for the client connection.
        int sock;

        /* 
		 * Another address structure.  This time, the system will automatically
         * fill it in, when we accept a connection, to tell us where the
         * connection came from.
		 */
        struct sockaddr_in remote_addr;
        unsigned int socklen = sizeof(remote_addr); 

        /* 
		 * Accept the first waiting connection from the server socket and
         * populate the address information.  The result (sock) is a socket
         * descriptor for the conversation with the newly connected client.  If
         * there are no pending connections in the back log, this function will
         * block indefinitely while waiting for a client connection to be made.
         */
        sock = accept(server_sock, (struct sockaddr*) &remote_addr, &socklen);
        if (sock < 0) {
            perror("Error accepting connection");
            exit(1);
        }

    	buff.putItem(sock);
	}
}

/**
 * Generates and sends a 400 error code and message.
 *
 * @param version the HTTP version to use
 * @param client_sock the socket to send the HTTP response to
 */
void sendHTTP400(string version, const int client_sock)
{
	string response400(version + " 400 BAD REQUEST\r\n\r\n");	
	sendData(client_sock, response400.c_str(), response400.length());
}

/**
 * Generates and sends a 404 error code and HTML code to display.
 *
 * @param version the HTTP version to use
 * @parameter client_sock the socket to send the HTTP response to
 */
void sendHTTP404(string version, const int client_sock)
{
	string response404(version + " 404 Not Found\r\n");
	sendData(client_sock, response404.c_str(), response404.length());
	
	string HTMLObject("<html><head><title>Ruh-roh! Page not found!</title></head><body><h1>404 Page Not Found! :'( :'( :'(</h1></body></html>");	
	
	//send the header	
	string header("Content-Length: " + std::to_string(HTMLObject.size()) + "\r\nContent-Type: text/html\r\n\r\n");
	sendData(client_sock, header.c_str(), header.length());
	
	//send the object
	string obj(HTMLObject + "\r\n\r\n");
	string fullResponse = response404 + header + obj;
	sendData(client_sock, obj.c_str(), obj.length());	
}

/**
 * Generates and sends a 200 message and adds objects to be displayed.
 *
 * @param version the HTTP version to use
 * @param client_sock the socket to send the HTTP response to
 */
void sendHTTP200(string version, const int client_sock, string fileName)
{
	string response200(version + " 200 OK \r\n");	
	sendData(client_sock, response200.c_str(), response200.length());
	sendHead(fileName, client_sock);
	sendObj(fileName, client_sock);	
}

/*
 * create the index page because it is not there and send the HTTP 200 response
 *
 * @param theDirectory 	the directory in which the index.html cannot be found
 * @param version 		the version of HTML to send the 200 OK response with
 * @param client_sock	the socket reference to which we are sending our data 
 */
void createAndSendIndexAndHTTP200(string theDirectory, string version, const int client_sock)
{
	//send the response200
	string response200(version + " 200 OK \r\n");
	sendData(client_sock, response200.c_str(), response200.length());

	//Creating HTML object to be sent
	string HTMLObject("");
	HTMLObject += "<html><body><ul>";
	for (fs::directory_iterator theCurrDir(theDirectory); theCurrDir != fs::directory_iterator(); ++theCurrDir)
	{
		string fileName = theCurrDir->path().filename().string();
		if (checkDir(theDirectory + "/" + fileName))
		{
			fileName += "/";
		}
		HTMLObject += "<li><a href=\"" + fileName + "\">";
		HTMLObject += fileName + "</a></li>";
	}	
	HTMLObject += "</ul></body></html>";

	//header and object to send out
	string header("Content-Length: " + std::to_string(HTMLObject.size()) + "\r\nContent-Type: text/html\r\n\r\n");
	string objToSend(HTMLObject + "\r\n\r\n");
	//sending header and object
	sendData(client_sock, header.c_str(), header.length());
	sendData(client_sock, objToSend.c_str(), objToSend.length());
}

/*
 * Checks the regex match given a specifific format using both the match and
 * the 
 *
 * @param request_string	the request from the browser in a c++ string
 * @param format 			the format one needs to follow to check for
 * @return => either empty or match
 */
string regexCheck(string request_string, string format)
{
	//take in format into regex type object and runs a regex_search on it to
	//find a match
	regex regForm(format);
	smatch potentialMatch;
	std::regex_search(request_string, potentialMatch, regForm);

	//if no matches => returns empty, else returns the match
	if(potentialMatch.empty())
	{
		return "empty";
	} 
	else 
	{
		return potentialMatch[0];
	}
}

/*
 * gets the version of the HTTP used in the request
 *
 * @param requestChecked	the string that was verified to be valid
 * @return => will return the regexCheck function return to match the string
 * to the needed regex needed
 */
string getVer(string requestChecked)
{
	return regexCheck(requestChecked, "(HTTP/\\d\\.\\d)");
}


/*
 * gets the objects name string which will be validated through path/directory
 * checking
 *
 * @param requestChecked	the string that was 
 * @return => will return the regexCheck function return to match the string
 * to the needed regex needed
 */
string getObj(string requestChecked)
{
	return regexCheck(requestChecked, "(/[\\w\\./\\-]*)");
}

/*
 * Sends the Header to the server
 *
 * @param object		the string that will be checked for the file type
 * @param cleint_sock	the client socket to which we must send something
 */
void sendHead(string fileName, const int client_sock)
{
	string header("");
	header += "Content-Length: ";
	header += std::to_string(fs::file_size(fileName));
	header += "\r\n";
	header += "Content-Type: ";
	header += fileType(fileName);
	header += "\r\n\r\n";
	
	sendData(client_sock, header.c_str(), header.length());	
}

/*
 * Sends the object to the server
 *
 * @param fileName		the name of the object we need to send
 * @param client_sock	the client sock to which we must send something 
 */
void sendObj(string fileName, const int client_sock)
{
	std::ifstream file(fileName, std::ios::binary);
	const unsigned int buffer_size = 4098;
	char file_data[buffer_size];
	while(!file.eof())
	{
		file.read(file_data, buffer_size);
		int bytesRead = file.gcount();
		sendData(client_sock, file_data, bytesRead);
		memset(file_data, 0, buffer_size);	
	}
	string end("\r\n\r\n");
	sendData(client_sock, end.c_str(), end.length());	
	file.close();
}


/*
 * a function to determine the file type
 *
 * @param fileName name of object extensions
 * @return the string of the content/type for header that will be sent
 */
string fileType(string fileName)
{
	string ext = fs::path(fileName).extension();
	string type;
	if(0 == ext.compare(".html"))
	{
		type = "text/html";
	}
	else if(0 == ext.compare(".css"))
	{
		type = "text/css";
	}
	else if(0 == ext.compare(".txt"))
	{
		type = "text/plain";
	}
	else if(0 == ext.compare(".jpg"))
	{
		type = "image/jpeg";
	}
	else if(0 == ext.compare(".gif"))
	{
		type = "image/gif";
	}
	else if(0 == ext.compare(".png"))
	{
		type = "image/png";
	}
	else if(0 == ext.compare(".pdf"))
	{
		type = "application/pdf";
	}
	else
	{	//TODO - usupported filetype
		type = "other";
	}	
	return type;
}

/*
 * Checks if the file is in the working directory
 *
 * @param	fileName	name of the file in the working directory
 * @return 	return true of false dependant if the file exists
 */
bool checkFile(string fileName)
{
	fs::path nameOfFile(fileName);
	return fs::is_regular_file(nameOfFile);
}

/*
 * Checks of the Directory is valid (path checking)
 *
 * @param 	thePath		name of the path we need to check
 * @return	return the status (true of false) if the path is valid
 */
bool checkDir(string thePath)
{
	fs::path nameOfPath(thePath);
	return fs::is_directory(nameOfPath);
}

/*
 * the loop for consumer threads to run - gets the new socket from the buffer and calls
 * handleClient  
 *
 * @param buffer 	the shared buffer that contains socket numbers
 * @param rootDir 	the root directory provided by the user in the command
 * line arguments 
 */
void consumerThread(BoundedBuffer &buffer, string rootDir)
{
	while(true)
	{
		const int client_sock = buffer.getItem();
		handleClient(client_sock, rootDir);
	}
}
