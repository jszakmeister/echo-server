#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>


static inline void
die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n" );
    va_end(ap);
    exit(EXIT_FAILURE);
}


static inline void
die_errno(int err)
{
    fprintf(stderr, "error: %s\n", strerror(err));
    exit(EXIT_FAILURE);
}


#ifdef ENABLE_ALARM
static int s_connection_count;


static void
sig_alarm(int sig)
{
    (void) sig;

    printf("Connection count: %d\n", s_connection_count);
}
#endif


#ifdef ENABLE_FORKING
static void
sig_chld(int sig)
{
    (void) sig;

    int status;

    /* More than one child could have exited, so reap them all. */
    for (; waitpid(-1, &status, WNOHANG) > 0;)
        ;
}
#endif


/** @brief Main program entry point.
    @param[in] argc  Number of arguments in @c argv.
    @param[in] argv  Command-line arguments.
    @retval 0
        Success.
*/
int
main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;

    char buffer[128];
    int server_fd = socket(PF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
        die_errno(errno);

    int on = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        die_errno(errno);

    struct sockaddr_in server;
    server.sin_family = PF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8888);

    if (bind(server_fd, (struct sockaddr *) &server, sizeof(server)))
        die_errno(errno);

    if (listen(server_fd, 0))
        die_errno(errno);

#ifdef ENABLE_ALARM
    {
        struct sigaction sa;

        sa.sa_handler = &sig_alarm;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGALRM, &sa, NULL) < 0)
            die_errno(errno);

        ualarm(1000000, 3000000);
    }
#endif

#ifdef ENABLE_FORKING
    {
        struct sigaction sa;

        sa.sa_handler = &sig_chld;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGCHLD, &sa, NULL) < 0)
            die_errno(errno);
    }
#endif

    for (;;)
    {
        struct sockaddr_in client;
        int client_fd;
        socklen_t sl = sizeof(client);
#ifdef ENABLE_FORKING
        pid_t pid;
#endif

        client_fd = accept(server_fd, (struct sockaddr *) &client, &sl);

        if (client_fd < 0)
        {
#ifdef ENABLE_ALARM
            if (errno == EINTR)
                continue;
#endif

            die_errno(errno);
        }

#ifdef ENABLE_ALARM
        s_connection_count++;
#endif

        ssize_t size, sent_size;

        char *client_ip = inet_ntoa(client.sin_addr);

        printf("Connection received from: %s\n", client_ip);

#ifdef ENABLE_FORKING
        /* Spawn off a new process to handle the client. */
        pid = fork();

        if (pid == 0)
        {
            /* This is the child. */
            close(server_fd);
#endif
            for (;;)
            {
                size = recv(client_fd, buffer, sizeof(buffer), 0);

                // Handle errors.
                if (size == -1)
                {
#ifdef ENABLE_ALARM
                    if (errno == EINTR)
                        continue;
#endif

                    die_errno(errno);
                }

                // OS is saying the other side was closed.
                if (size == 0)
                    break;

                printf("Recv'd %ld bytes:\n<<<<<<\n", size);
                write(1, buffer, size);
                printf(">>>>>>\n");

                char *p = buffer;

                while (size)
                {
                    sent_size = send(client_fd, p, size, 0);
                    if (sent_size == -1)
                    {
#ifdef ENABLE_ALARM
                        if (errno == EINTR)
                            continue;
#endif

                        die_errno(errno);
                    }

                    p += sent_size;
                    size -= sent_size;
                }
            }

            close(client_fd);

            printf("Client disconnected\n");

#ifdef ENABLE_FORKING
            exit(0);
        }
        else
        {
            /* Server doesn't need this. */
            close(client_fd);
        }
#endif
    }

    return 0;
}
