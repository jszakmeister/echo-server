#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef ENABLE_THREADING
#include <pthread.h>
#endif

#ifdef ENABLE_DAEMON
#include <fcntl.h>
#endif

#ifdef ENABLE_FORKING
#ifdef ENABLE_THREADING
#error "You can't enable both forking and threading."
#endif
#endif


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


static void
handle_client(int client_fd)
{
    char buffer[128];
    ssize_t size, sent_size;

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
}


#ifdef ENABLE_THREADING
static void *
client_thread_routine(void *arg)
{
    int *client_fd = arg;

    handle_client(*client_fd);

    return NULL;
}


static void
start_thread(int client_fd)
{
    pthread_t client_thread;

    int err = pthread_create(&client_thread, NULL,
                             client_thread_routine, &client_fd);
    if (err != 0)
        die_errno(err);

    err = pthread_detach(client_thread);
    if (err != 0)
        die_errno(err);
}
#endif


#ifdef ENABLE_FORKING
static void
start_fork(int server_fd, int client_fd)
{
    pid_t pid;

    /* Spawn off a new process to handle the client. */
    pid = fork();

    if (pid == 0)
    {
        /* This is the child. */
        close(server_fd);

        handle_client(client_fd);

        exit(0);
    }
    else
    {
        /* Server doesn't need this. */
        close(client_fd);
    }
}
#endif


#ifdef ENABLE_DAEMON
/** @brief Daemonize this process.

    You'll want to keep this function around.  It's handy for daemonizing
    a process.  The only other thing you might want to do is close the first
    100 descriptors or so, just to make sure you don't ruin something
    accidentally.
*/
static void
daemonize(void)
{
    /*
        Change to the root directory to prevent the file system
        from hanging on to removed directories.
    */
    if (chdir("/") == -1)
        die_errno(errno);

    /* Close the standard file descriptors. */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /*
        Re-open them attached to /dev/null.  This may not do us much good
        since we're closing the descriptors, however it's the last (stderr)
        that matters for error reporting.
    */
    if (open("/dev/null", O_RDONLY) == -1) {
        die("failed to reopen stdin (%s)", strerror(errno));
    }

    if (open("/dev/null", O_WRONLY) == -1) {
        die("failed to reopen stdout (%s)", strerror(errno));
    }

    if (open("/dev/null", O_RDWR) == -1) {
        die("failed to reopen stderr (%s)", strerror(errno));
    }

    /* This is the first step in disconnecting from the terminal. */
    pid_t pid = fork();

    if (pid != 0)
        exit(0);

    /* Start a session. */
    if (setsid() == -1)
    {
        die_errno(errno);
    }

    /* Block the terminal hangup signal. */
    signal(SIGHUP, SIG_IGN);

    /*
        This is the next step to guarantee that we're disconnected from
        the terminal.
    */
    pid = fork();

    if (pid != 0)
        exit(0);

    /* Now we're fully daemonized. */
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

#ifdef ENABLE_DAEMON
    daemonize();
#endif

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

        char *client_ip = inet_ntoa(client.sin_addr);

        printf("Connection received from: %s\n", client_ip);

#if defined(ENABLE_FORKING)
        start_fork(server_fd, client_fd);
#elif defined(ENABLE_THREADING)
        start_thread(client_fd);
#else
        handle_client(client_fd);
#endif
    }

    return 0;
}
