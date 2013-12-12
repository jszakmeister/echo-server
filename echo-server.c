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
#ifdef SHOW_BUG
#include <time.h>
#endif
#endif

#ifdef ENABLE_DAEMON
#include <fcntl.h>
#endif

#ifdef ENABLE_FORKING
#ifdef ENABLE_THREADING
#error "You can't enable both forking and threading."
#endif
#endif

#ifdef ENABLE_PRIV
#include <inttypes.h>
#include <sys/types.h>
#include <pwd.h>
#endif

#ifdef ENABLE_OPTS
#include <getopt.h>
#include <limits.h>
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


static inline void
die_errno_msg(int err, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, " (%s)\n", strerror(err));
    va_end(ap);
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

    printf("Handling fd: %d\n", client_fd);

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

            die_errno_msg(errno, "recv failed (%d)", client_fd);
        }

        // OS is saying the other side was closed.
        if (size == 0)
            break;

        printf("%d: Recv'd %ld bytes:\n<<<<<<\n", client_fd, (long) size);
        (void) write(1, buffer, size);
        printf(">>>>>>\n");

        char *p = buffer;

        while (size)
        {
            sent_size = send(client_fd, p, size, 0);
            if (sent_size == -1)
            {
#ifdef ENABLE_ALARM
                if (err == EINTR)
                    continue;
#endif

                die_errno_msg(errno, "send failed (%d)", client_fd);
            }

            p += sent_size;
            size -= sent_size;
        }
    }

    close(client_fd);

    printf("Client disconnected (%d)\n", client_fd);
}


#ifdef ENABLE_THREADING
static void *
client_thread_routine(void *arg)
{
    int *client_fd = arg;

    /* ### We have a problem.  See the comment in start_thread(). */
    handle_client(*client_fd);

    return NULL;
}


#ifdef SHOW_BUG
static void
consume_some_stack(void)
{
    long buffer[100];
    int i;
    long new_seed = 0;

    for (i = 0; i < 100; i++)
        buffer[i] = random();

    for (i = 0; i < 100; i++)
        new_seed += buffer[i];

    new_seed /= 100;

    /*
        This is here to actually use something in the routine so gcc doesn't
        optimize it away.
    */
    srandom(new_seed);
}

static void *
client_thread_routine_sleep(void *arg)
{
    sleep((int) (random() % 5));

    return client_thread_routine(arg);
}
#endif


static void
start_thread(int client_fd)
{
    pthread_t client_thread;

    /*
        ### The cat is out of the bag.  There's an intentional bug here.
        client_fd is being stored on the stack.  We're passing a pointer to it
        for use in client_thread_routine(), but that function can execute at
        some arbitrary time in the future.  In the meantime, this function could
        exit and some other functions be allowed to execute and corrupting this
        position on the stack.  All kinds of nasty things could happen... but
        not always.  Most of the time, it works just fine. :-(
    */
#ifdef SHOW_BUG
    int err = pthread_create(&client_thread, NULL,
                             client_thread_routine_sleep, &client_fd);
#else
    int err = pthread_create(&client_thread, NULL,
                             client_thread_routine, &client_fd);
#endif

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


#ifdef ENABLE_PRIV
uid_t
getuid_for_name(const char *name)
{
    struct passwd *p;
    char *endptr;
    uid_t u;

    if (! name || *name == '\0')
        return -1;

    u = strtol(name, &endptr, 10);
    if (*endptr == '\0')
        return u;

    p = getpwnam(name);
    return p->pw_uid;
}
#endif


#ifdef ENABLE_OPTS
int
convert_int(const char *str)
{
    char *endptr;
    long int val;

    errno = 0;    /* To distinguish success/failure after call */
    val = strtol(str, &endptr, 10);

    /* Check for various possible errors */

    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
        || (errno != 0 && val == 0)
        || (*endptr != '\0'))
    {
        die_errno_msg(errno, "could not convert %s to a number", str);
    }

    if (endptr == str)
    {
        die("no number given");
    }

    return (int) val;
}


static void
show_help_and_exit(void)
{
    printf(
        "usage: echo-server [options]\n"
        "\n"
        "Options:\n"
        "  -p PORT, --port PORT     The port number to listen on.  Defaults to\n"
        "                           8888.\n"
        "  -b ADDR, --bind ADDR     The address to listen on.  Defaults to\n"
        "                           0.0.0.0.\n"
#ifdef ENABLE_DAEMON
        "  --foreground             Run the server in the foreground.\n"
#endif
        "\n");
    exit(0);
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
    struct sockaddr_in server;
    server.sin_family = PF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8888);

#ifdef ENABLE_OPTS
#ifdef ENABLE_DAEMON
    int foreground = 0;
#endif
    int show_help = 0;
    struct option long_options[] =
    {
        {"port",        required_argument,  NULL,           'p'},
        {"bind",        required_argument,  NULL,           'b'},
        {"help",        no_argument,        &show_help,     1},
#ifdef ENABLE_DAEMON
        {"foreground",  no_argument,        &foreground,    1},
#endif
        {0, 0, 0, 0}
    };

    for (;;)
    {
        int c;
        int option_index = 0;

        c = getopt_long(argc, argv, "ha:p:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {
        case 0:
            if (long_options[option_index].flag != NULL)
                break;
            die("failed to handle option: %s\n",
                long_options[option_index].name);
            break;

        case 'h':
            show_help = 1;
            break;

        case 'b':
            if (! inet_aton(optarg, &server.sin_addr))
                die("unable to parse addr: %s", optarg);
            break;

        case 'p':
            server.sin_port = htons((short) convert_int(optarg));
            break;

        default:
            /* getopt already print an error message for us. */
            exit(EXIT_FAILURE);
            break;
        }
    }

    if (show_help)
        show_help_and_exit();
#else
    (void) argc;
    (void) argv;
#endif

#ifdef SHOW_BUG
    srandom((long int) time(NULL));
#endif

    int server_fd = socket(PF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
        die_errno(errno);

    int on = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        die_errno(errno);

    if (bind(server_fd, (struct sockaddr *) &server, sizeof(server)))
        die_errno_msg(errno, "cannot open port");

#ifdef ENABLE_PRIV
    if (getuid() == 0)
    {
        uid_t u = getuid_for_name("nobody");
        if (u == (uid_t) -1)
            die("unable to get uid for 'nobody'");
        setuid(u);
    }
#endif

#ifdef ENABLE_DAEMON
#ifdef ENABLE_OPTS
    if (! foreground)
#endif
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

        printf("Connection received from: %s (%d)\n", client_ip, client_fd);

#if defined(ENABLE_FORKING)
        start_fork(server_fd, client_fd);
#elif defined(ENABLE_THREADING)
#ifdef SHOW_BUG
        start_thread(client_fd);
        consume_some_stack();
#else
        start_thread(client_fd);
#endif
#else
        handle_client(client_fd);
#endif
    }

    return 0;
}
