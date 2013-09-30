#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <pthread.h> /* POSIX threads */
#include <stdint.h>  /* Data conventions */
#include <sys/time.h>
#include <stdbool.h>
#include <stdlib.h>

// ----------------------------------------------------------------------------

typedef struct
{
    char* filename;
    bool binary;
    char* uart_address;
    int uart_descriptor;
    int32_t timeout;
    bool timeout_activated;
    bool send_descriptor;

} parameters_context_t;

// ----------------------------------------------------------------------------

static bool local_get_context ( int argc , char **argv, parameters_context_t *context );
static void* local_thread_read ( void* context);
static void local_config_port ( int uart);
static void local_display_usage ( void);
static void local_reset_last_communication_timer ( void);
static uint32_t local_miliseconds_since_last_communication ( void);
static bool local_send_file_uart ( parameters_context_t* context);

// ----------------------------------------------------------------------------

static bool _shutdown_request = 0;
uint32_t _last_communication_timestamp_miliseconds = 0;

// ----------------------------------------------------------------------------

int main ( int argc , char **argv )
{
    int return_value = -1;

    parameters_context_t context;
    if (local_get_context(argc, argv, &context))
    {
        puts("Connecting to PLC-ART...");

        context.uart_descriptor = open(context.uart_address, O_RDWR | O_NOCTTY | O_NDELAY);

        if (context.uart_descriptor == -1)
        {
            fprintf(stderr, "Unable to open %s.", context.uart_address);
        }
        else if (argc > 1)
        {
            puts ("Successfully opened.");

            local_config_port(context.uart_descriptor);
            local_reset_last_communication_timer();

            /*
             * Start a new reading thread.
             */
            pthread_t thread_receive;
            pthread_create(&thread_receive, NULL, local_thread_read, &context);

            /*
             * First argument is filename.
             */
            if (local_send_file_uart(&context))
            {
                /*
                 * Wait until timeout.
                 */
                if (context.timeout_activated)
                {
                    while (local_miliseconds_since_last_communication() < context.timeout)
                    {
                        sleep(1);
                    }
                }
            }

            /*
             * Shutdown reading thread and wait.
             */
            _shutdown_request = true;
            pthread_join(thread_receive, NULL);

            close(context.uart_descriptor);
            return_value = 0;
        }
    }

    /*
     * Display help to the user if something went wrong.
     */
    if (return_value != 0)
    {
        local_display_usage();
    }

    return return_value;
}
// ----------------------------------------------------------------------------

/**
 * Read command-line parameters and fill the parameter context structure.
 *
 */
static bool local_get_context ( int argc , char **argv, parameters_context_t *context )
{
    bool result = true;

    /*
     * Default values.
     */
    context->filename = NULL;
    context->binary = false;
    context->uart_address = "/dev/tty.usbserial";
    context->uart_descriptor = 0;
    context->timeout = 2000;
    context->timeout_activated = true;
    context->send_descriptor = false;

    /*
     * Check if there is a filename, if exists and if is a LUA file.
     */
    for (int pos = 1; pos < argc; pos++)
    {
        if (strcmp(argv[pos], "-t") == 0) // timeout
        {
            if (pos == argc - 1)
            {
                result = false;
                break;
            }

            context->timeout = atoi(argv[pos + 1]);
            context->timeout_activated = (context->timeout > 0) ? true : false;
            pos++;

        }
        else if (strcmp(argv[pos], "-u") == 0) // uart address
        {
            if (pos == argc - 1)
            {
                result = false;
                break;
            }

            context->uart_address = argv[pos + 1];
            pos++;
        }
        else if (strcmp(argv[pos], "-d") == 0) // recovery mode
        {
            context->send_descriptor = true;
        }
        else if (strcmp(argv[pos], "-h") == 0) // help
        {
            result = false;
            break;
        }
        else if (strcmp(argv[pos], "-b") == 0) // binary mode
        {
            context->binary = true;
        }
        else // filename?
        {
            context->filename = argv[pos];
        }
    }

    if (result)
    {
        printf("filename:%s\r\n", context->filename);
        printf("binary mode:%s\r\n", (context->binary) ? "true" : "false");
        printf("uart address:%s\r\n", context->uart_address);
        printf("timeout:%s\r\n", (context->timeout_activated) ? "true" : "false");
        if (context->timeout_activated)
            printf("        %d ms\r\n", context->timeout);
//        printf("recovery mode:%s\r\n", (context->send_descriptor) ? "true" : "false");
    }

    return result;
}
// ----------------------------------------------------------------------------

static void local_display_usage ( void )
{
    puts("");
    puts("The purpose of this program is write a file on the UART. This can be handy when");
    puts("you want to use a IDE like EMACS or even Eclipse to write PLC scripts instead");
    puts("of typing each line in the prompt or switching pen drives.");
    puts("");
    puts("USAGE");
    puts("");
    puts("    dxtr filename [-t timeout seconds] [-u uart address] [-d]");
    puts("");
    puts("It will open /dev/tty.usbserial and dump the file there. Whenever the PLC takes");
    puts("more than timeout (n) seconds - default is two seconds - to print anything back,");
    puts("execution will be finished.");
    puts("");
    puts("If -d is used it will use the Recovery Protocol (tech spec ART 1309280001.1)");
    puts("and will first send a notification byte followed by a 32-bit LE integer with the");
    puts("amount of incoming data, not including the first 5 bytes.");
    puts("");
}
// ----------------------------------------------------------------------------

static void local_config_port ( int uart )
{
    /*
     * Read will return immediately.
     */
    fcntl(uart, F_SETFL, FNDELAY);

    /*
     * Set speed to 57 kbps
     */
    struct termios options;
    tcgetattr(uart, &options);
    cfsetispeed(&options, B57600);
    cfsetospeed(&options, B57600);
    options.c_cflag |= (CLOCAL | CREAD);
    tcsetattr(uart, TCSANOW, &options);
}
// ----------------------------------------------------------------------------

static void* local_thread_read ( void* context_address )
{
    parameters_context_t *context = (parameters_context_t*)context_address;

    char buffer [256];

    local_reset_last_communication_timer();
    while (_shutdown_request == false)
    {
        memset(buffer, 0, sizeof(buffer));
        int amount_read = read(context->uart_descriptor, buffer, sizeof(buffer));
        if (amount_read > 0)
        {
            local_reset_last_communication_timer();
            buffer [amount_read] = 0;
            printf("%s", buffer);
            fflush(stdout);
        }
        else // nothing read, sleep for a bit
        {
            nanosleep((struct timespec[]){{0, 12000000}}, NULL);
        }
    }

    pthread_exit(NULL);
    return NULL;
}
// ----------------------------------------------------------------------------

static void local_reset_last_communication_timer ( void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    _last_communication_timestamp_miliseconds = tv.tv_sec * 1000;
    _last_communication_timestamp_miliseconds += (tv.tv_usec / 1000000);
}
// ----------------------------------------------------------------------------

static uint32_t local_miliseconds_since_last_communication( void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint32_t timestamp_ms = tv.tv_sec * 1000;
    timestamp_ms += (tv.tv_usec / 1000000);

    uint32_t elapsed_ms = (timestamp_ms - _last_communication_timestamp_miliseconds);

    return elapsed_ms;
}
// ----------------------------------------------------------------------------

static bool local_send_file_uart ( parameters_context_t* context)
{
    bool success = false;

    FILE *file = fopen(context->filename, "rb");
    if (file == NULL)
    {
        perror("Could not find/open the file.");
    }
    else
    {
        /*
         * VT100 uses '\r' as EOL
         * unix files sometimes have just '\n' as EOL
         * windows files will have '\r\n'
         *
         * Do not send '\n'
         * Always use '\r'
         *
         */
        char data;
        char lastchar = 0;
        while (!feof(file))
        {
            fread(&data, sizeof(char), 1, file);

            if (context->binary)
            {
                write (context->uart_descriptor, &data, 1);
            }
            else // text
            {
                if (data != '\n')
                {
                    write (context->uart_descriptor, &data, 1);
                }
                else if (lastchar != '\r') // char is '\n'
                {
                    write (context->uart_descriptor, "\r", 1);
                }
                lastchar = data;
            }

            uint32_t sleep_ms = 10;
            nanosleep((struct timespec[]){{0, sleep_ms * 1000000}}, NULL);

            local_reset_last_communication_timer();
        }

        success = true;

        fclose(file);
    }

    return success;
}
// ----------------------------------------------------------------------------
