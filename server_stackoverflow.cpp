#include <netinet/in.h> // sockaddr_in struct
#include <arpa/inet.h>  //inet_addr()
#include <sys/socket.h>
#include <errno.h>     //errors
#include <stdio.h>     //perror()
#include <cstdlib>     //EXIT_FAILURE
#include <sys/ioctl.h> //FIONBIO
#include <unistd.h>    //close file descriptor
#include <fcntl.h>     //make non blocking
#include <poll.h>      //poll stuff
#include <string.h>    //memset

#define SERVER_PORT 12345

int main()
{
    int s = -1;
    int rc;
    int optval = 1;
    int timeout;
    bool end_server = false; // because we need to log if EWOULDBLOCK is true...

    struct pollfd fds[200]; // initialize pollfd struct
    int nfds = 1;           // nfds_t really set to 1 else it will be 199 once we pass it to poll....

    int current_size = 0;

    int new_s = -1;

    int close_conn;

    char *buff;

    int len;

    bool compress_array;

    s = socket(AF_INET, SOCK_STREAM, 0);

    // make socket description reusable with SO_REUSEADDR
    rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&optval), sizeof(optval));
    if (rc < 0)
    {
        perror("setsockopt()");
        close(s);
        exit(EXIT_FAILURE);
    }

    // make socket non-blocking
    // rc = ioctl(s, FIONBIO, reinterpret_cast<char*>(&optval));
    // if(rc < 0)
    // {
    //    perror("ioctl()");
    //    close(s);
    //    exit(EXIT_FAILURE);
    // }

    fcntl(s, F_SETFL, O_NONBLOCK);

    struct sockaddr_in6 addr;
    // initialize sockaddr_in struct
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
    addr.sin6_port = htons(SERVER_PORT);

    rc = bind(s,
              (struct sockaddr *)&addr, sizeof(addr));

    if (rc < 0)
    {
        perror("bind()");
        exit(EXIT_FAILURE);
    }

    rc = listen(s, 32);
    if (rc < 0)
    {
        perror("listen() failed");
        close(s);
        exit(EXIT_FAILURE);
    }

    // initialize fds struct
    memset(&fds, 0, sizeof(fds));

    fds[0].fd = s;
    fds[0].events = POLLIN; // check if data to read

    // initialize timeout value to 3 mins based on millisecs
    // timeout = (3 * 60 * 1000); // because function will be like sleep() that uses millisecs
    timeout = 10000;

    do
    {
        // call poll() and wait 3 mins to complete because of timeout
        printf("Waiting on poll()...\n");
        rc = poll(fds, nfds, timeout);

        if (rc < 0)
        {
            perror("poll() failed");
            exit(EXIT_FAILURE);
        }

        // check if 3 minutes timeout expired
        if (rc == 0)
        {
            printf("poll() timed out ending program...\n");
            exit(EXIT_FAILURE);
        }

        current_size = nfds;
        for (int i = 0; i < current_size; i++)
        {
            // loop thru fds and check if revents returns POLLIN, means the fd have data to read...
            if (fds[i].revents == 0)
                continue;

            // if revents is not POLLIN then exit program and log
            if (fds[i].revents != POLLIN)
            {
                printf("revents != POLLIN, revents = %d\n", fds[i].revents);
                // end_server = true;
                // break;
                // perror("revents unknown");
                // exit(EXIT_FAILURE);
                close(fds[i].fd);
                fds[i].fd = -1;
                break;
            }

            if (fds[i].fd == s)
            {
                printf("Listening socket available\n");

                do
                {
                    // accept each new incoming connections
                    new_s = accept(s, NULL, NULL);
                    if (new_s < 0)
                    {
                        if (errno != EWOULDBLOCK)
                        {
                            perror("accept() failed because of socket would block");
                            end_server = true;
                        }
                        // printf("something else wrong with accept()\n");
                        break;
                    }

                    // add new incoming connection
                    printf("new incoming connection - nfds: %d\n", new_s);
                    fds[nfds].fd = new_s;
                    fds[nfds].events = POLLIN;
                    nfds++;
                    // continue;
                    // loop back up and accept another connection

                } while (new_s != -1);
            }
            // file descriptor is readable because its now new_s instead of s
            else
            {
                printf("descriptor %d is readable\n", fds[i].fd);
                close_conn = false;
                // receive all data on this connection till we go back and poll again
                do
                {

                    rc = recv(fds[i].fd, reinterpret_cast<void *>(&buff), sizeof(buff), 0);
                    if (rc < 0)
                    {
                        if (errno != EWOULDBLOCK)
                        {
                            perror("recv() failed");
                            close_conn = true;
                        }
                        break;
                    }

                    // check if conn was closed by client
                    if (rc == 0)
                    {
                        printf("connection closed");
                        close_conn = true;
                        break;
                    }

                    // data was received
                    len = rc;
                    printf("%d bytes received", len);

                    // process stuff or echo data back to client
                    rc = send(fds[i].fd, reinterpret_cast<void *>(&buff), sizeof(buff), 0);
                    if (rc < 0)
                    {
                        perror("send() failed");
                        close_conn = true;
                        break;
                    }
                    memset(&buff, 0, sizeof(buff));

                } while (true);
                if (close_conn)
                {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    compress_array = true;
                }
            }
        }
        if (compress_array)
        {
            compress_array = false;
            int i = 0;
            for (i = 0; i < nfds; i++)
            {
                if (fds[i].fd == -1)
                {
                    for (int j = i; j < nfds; j++)
                    {
                        fds[j].fd = fds[j + 1].fd;
                    }
                    i--;
                    nfds--;
                }
            }
        }

    } while (end_server == false);

    // clean all sockets that are open
    for (int i = 0; i < nfds; i++)
    {
        if (fds[i].fd > 0)
        { // if already -1 don't need to close socket
            close(fds[i].fd);
            fds[i].fd = -1;
        }
    }

    return 0;
}