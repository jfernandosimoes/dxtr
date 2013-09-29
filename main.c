#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <pthread.h> /* POSIX threads */
#include <stdint.h>  /* Data conventions */
#include <sys/time.h>

// ----------------------------------------------------------------------------

#define bool uint8_t
#define false (0)
#define true (1)

// ----------------------------------------------------------------------------

static void local_config_port ( int uart);
static void* local_thread_read ( void* param);
static void local_display_usage ( void);
static void local_reset_last_communication_timer ( void);
static uint32_t local_miliseconds_since_last_communication ( void);
static bool local_send_file_uart ( char *filename, int uart);

// ----------------------------------------------------------------------------

static bool _shutdown_request = 0;
uint32_t _last_communication_timestamp_miliseconds = 0;

// ----------------------------------------------------------------------------

int main ( int argc , char **argv )
{
    int return_value = -1;

    uint32_t timeout_ms = 2000;

    puts("Connecting to PLC-ART");

    int uart = open("/dev/tty.usbserial", O_RDWR | O_NOCTTY | O_NDELAY);

    if (uart == -1)
    {
        perror("Unable to open /dev/tty.usbserial");
    }
    else if (argc > 1)
    {
        puts ("Successfully opened.");

        local_config_port(uart);
        local_reset_last_communication_timer();

        /*
         * Start a new reading thread.
         */
        pthread_t thread_receive;
        pthread_create(&thread_receive, NULL, local_thread_read, &uart);

        write(uart, "\r", 1);

        /*
         * First argument is filename.
         */
        if (local_send_file_uart(argv [1], uart))
        {
            /*
             * Wait until timeout.
             */
            while (local_miliseconds_since_last_communication() < timeout_ms)
            {
                sleep(1);
            }
        }

        /*
         * Shutdown reading thread and wait.
         */
        _shutdown_request = true;
        pthread_join(thread_receive, NULL);

        close(uart);
        return_value = 0;
    }

    if (return_value != 0)
    {
        local_display_usage();
    }

    return return_value;
}
// ----------------------------------------------------------------------------

static void local_display_usage ( void )
{
    puts("");
    puts("You doesn't seem to have any idea of what you are doing.");
    puts("");
    puts("The purpose of this program is write a file on the UART. This can be handy when you ");
    puts("want to use a decent IDE like EMACS or even Eclipse to write PLC scripts instead of typing");
    puts("each line on the prompt or switching pen drives like a monkey.");
    puts("");
    puts("So basically the usage is:    dxtr filename [-t timeout seconds] [-w]");
    puts("");
    puts("It will open /dev/tty.usbserial and dump the file there. Whenever the PLC takes more");
    puts("than timeout (n) seconds - default is two seconds - to print anything back, it will close.");
    puts("");
    puts("If you used -w, it will use the Disaster Recovery protocol (see tech spec ART 1309280001.1)");
    puts("and will first send a 'hey im uploading shit' byte followed by a 32-bit LE integer with");
    puts("the size of the incoming data, not including the first 5 bytes. I hope you don't need to");
    puts("use -w, that means I screwed up the code enabling you to brick the PLC using LUA.");
    puts("");
    puts("Obviously this won't work on Windows. If you are using Windows, try your Ask toolbar in");
    puts("the Internet Explorer to find something similar to download on the Internet. Or go ahead");
    puts("and code it yourself, this one here took a couple of hours on a Sunday morning. Instead");
    puts("of going to the church or sleeping your hangover in, wake up and do something useful.");
    puts("");
    puts("Thanks for reading until here. Seems you got plenty of spare time in your hands. If I need");
    puts("to print this again I'll consider deleting myself to prevent you from doing even more damage.");
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

static void* local_thread_read ( void* param )
{
    int uart = *((int*) (param));
    char buffer [256];

    local_reset_last_communication_timer();
    while (_shutdown_request == false)
    {
        memset(buffer, 0, sizeof(buffer));
        int amount_read = read(uart, buffer, sizeof(buffer));
        if (amount_read > 0)
        {
            local_reset_last_communication_timer();
            buffer [amount_read] = 0;
            printf("%s", buffer);
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

static bool local_send_file_uart ( char *filename, int uart )
{
    bool success = false;

    FILE *file = fopen(filename, "r");
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
        while ((data = fgetc(file)) != EOF)
        {
            if (data != '\n')
            {
                write (uart, &data, 1);
            }
            else if (lastchar != '\r') // char is '\n'
            {
                write (uart, "\r", 1);
            }

            nanosleep((struct timespec[]){{0, 12000000}}, NULL);

            lastchar = data;
            local_reset_last_communication_timer();
        }

        success = true;

        fclose(file);
    }

    return success;
}
// ----------------------------------------------------------------------------
