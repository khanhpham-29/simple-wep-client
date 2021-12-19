#define MAX_REQUEST_LEN 1024
#include <arpa/inet.h>
#include <netdb.h> /* getprotobyname */
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

//check if header contain  "transfer-encoding: chunked"
bool is_chunked(char *header_buffer)
{
    char check_string[] = "transfer-encoding: chunked";
    char check_string2[] = "Transfer-Encoding: chunked";

    if (strstr(header_buffer, check_string) == NULL && strstr(header_buffer, check_string2) == NULL)
    {
        return false;
    }
    return true;
}

//recive and find lenght of one chunked
long getChunked_lenght(int socket_file_descriptor)
{
    int chunked_size = 0;
    char *chunked_buffer = (char *)calloc(1, sizeof(char));
    while (recv(socket_file_descriptor, chunked_buffer + chunked_size, 1, 0) > 0)
    {
        chunked_size++;
        chunked_buffer = (char *)realloc(chunked_buffer, (chunked_size + 1) * sizeof(char));
        if (strstr(chunked_buffer, "\r\n") != NULL)
        {
            break;
        }
    }

    chunked_buffer = strtok(chunked_buffer, "\r");
    long result = strtol(chunked_buffer, NULL, 16);
    free(chunked_buffer);
    return result;
}

bool recv_using_chunked(int socket_file_descriptor, char *output_file_name)
{
    long chunked_lenght = 0;
    FILE *file = fopen(output_file_name, "w");
    do
    {
        // plus 2 because we will recive \r\n at the end of each chunked then when we write to file we
        //just ignore 2 last character.
        chunked_lenght = getChunked_lenght(socket_file_descriptor) + 2;
        if (chunked_lenght == 2)
        {
            break;
        }

        //plus 1 to chunked_lenght just to add '\0'
        char *chunked_buffer = (char *)calloc(chunked_lenght, sizeof(char));
        int ret = 0;
        int check = 0;
        do
        {
            check = recv(socket_file_descriptor, chunked_buffer + ret, chunked_lenght - ret, 0);
            if (check == -1)
            {
                return false;
            }
            ret += check;
        } while (ret < chunked_lenght);

        chunked_buffer[chunked_lenght - 2] = '\0';
        fprintf(file, "%s", chunked_buffer);
        free(chunked_buffer);
    } while (chunked_lenght != 0);
    fclose(file);
    return true;
}

//check if header contain  "Content-Length:". If had find end return content length
long get_content_lenght(char *header_buffer)
{
    char check_string[] = "Content-Length:";
    char *temp = strstr(header_buffer, check_string);

    if (temp == NULL)
    {
        return -1;
    }

    char *size = strtok(temp + sizeof(check_string), "\r\n");
    return atol(size);
}

bool recv_using_content_lenght(int socket_file_descriptor, char *output_file_name, long content_lenght)
{
    //plus 1 to content_lenght just to add '\0'
    char* content_buffer = (char *)calloc(content_lenght + 1, sizeof(char));

    FILE *file = fopen(output_file_name, "w");

    int ret = 0;
    do
    {   
        int check = recv(socket_file_descriptor, content_buffer + ret, content_lenght - ret, 0);
        if(check == -1){
            return false;
        }
        ret += check;
    } while (ret < content_lenght);

    content_buffer[content_lenght] = '\0';
    fprintf(file, "%s", content_buffer);
    free(content_buffer);
    fclose(file);

    return true;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        perror("Argv");
        exit(EXIT_FAILURE);
    }

    /*build request*/
    //second arg is url
    char *url = argv[1];

    //parse url to get host name and resource path(which we need to load)
    char *hostname = strtok(url, "/");
    char *resource_path = strtok(NULL, "\0");

    //paste host name and resource_path to request string
    char request[MAX_REQUEST_LEN];
    int request_len;
    if (resource_path == NULL)
    {
        char request_template[] = "GET / HTTP/1.1\r\nHost: %s\r\n\r\n";
        request_len = snprintf(request, MAX_REQUEST_LEN, request_template, hostname);
    }
    else
    {
        char request_template_with_resource[] = "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n";
        request_len = snprintf(request, MAX_REQUEST_LEN, request_template_with_resource, resource_path, hostname);
    }

    if (request_len >= MAX_REQUEST_LEN)
    {
        fprintf(stderr, "request length large: %d\n", request_len);
        exit(EXIT_FAILURE);
    }

    /* Build the socket. */
    //protocol info contain name, protocol number
    struct protoent *protoent;
    //set protocol is tcp
    protoent = getprotobyname("tcp");
    if (protoent == NULL)
    {
        perror("getprotobyname");
        exit(EXIT_FAILURE);
    }

    //create socket
    int socket_file_descriptor = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
    if (socket_file_descriptor == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Build the address. */
    //help getaddr by host name
    struct hostent *hostent = gethostbyname(hostname);
    if (hostent == NULL)
    {
        fprintf(stderr, "error: gethostbyname(\"%s\")\n", hostname);
        exit(EXIT_FAILURE);
    }

    //in_addr_t is just an integer representing the ip address
    //inet_addr() converts a string representing an IPv4 Internet address (for example, “127.0.0.1”) into a numeric Internet address.
    //inet_ntoa() converts the Internet host address in, given in network byte order(which is in hostent->h_addr_list ), to a string in IPv4 dotted-decimal notation
    in_addr_t in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
    if (in_addr == (in_addr_t)-1)
    {
        fprintf(stderr, "error: inet_addr(\"%s\")\n", *(hostent->h_addr_list));
        exit(EXIT_FAILURE);
    }

    /* build port and addr of server*/
    struct sockaddr_in sockaddr_in;
    unsigned short server_port = 80;

    sockaddr_in.sin_family = AF_INET;
    sockaddr_in.sin_addr.s_addr = in_addr;
    sockaddr_in.sin_port = htons(server_port);

    /* connect. */
    if (connect(socket_file_descriptor, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    /* Send HTTP request. */
    ssize_t total, last;
    total = 0;
    while (total < request_len)
    {
        last = send(socket_file_descriptor, request + total, request_len - total, 0);
        if (last == -1)
        {
            perror("send");
            exit(EXIT_FAILURE);
        }
        total += last;
    }

    /* Read the response. */
    //get header
    int header_size = 0;
    char *header_buffer = (char *)malloc(0);
    while ((total = recv(socket_file_descriptor, header_buffer + header_size, 1, 0)) > 0)
    {
        header_size++;
        header_buffer = (char *)realloc(header_buffer, (header_size + 1) * sizeof(char));
        if (strstr(header_buffer, "\r\n\r\n") != NULL)
        {
            break;
        }
    }

    if (total == -1)
    {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    /*Get content of the web with two type content lenght and chunked  and write to a output file*/

    //second arg is file name
    char *output_file_name = argv[2];
    long content_lenght;

    if (is_chunked(header_buffer))
    {
        if (recv_using_chunked(socket_file_descriptor, output_file_name) == false)
        {
            perror("recv chunked");
            exit(EXIT_FAILURE);
        }
    }
    else if ((content_lenght = get_content_lenght(header_buffer)) > -1)
    {
        if (recv_using_content_lenght(socket_file_descriptor, output_file_name, content_lenght) == false)
        {
            perror("recv content lenght");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        perror("header");
        exit(EXIT_FAILURE);
    }

    free(header_buffer);
    close(socket_file_descriptor);
    exit(EXIT_SUCCESS);
}