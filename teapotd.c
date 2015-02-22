#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>


#define CHK(cond, msg) if(cond){ perror("ERROR: " msg); return 1; }


char *gpio_path(char *envvar);
void set_led(char *path, bool value);
int set_timer(char*, char*);


int main(int argc, char **argv){
    puts("Teapot-UI");
    puts("Copyright Reuben D'Netto 2015");
    puts("Published under Apache 2.0");
    puts("");

    //parse command-line args
    bool background = false;

    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-B") == 0 || strcmp(argv[i], "--background") == 0){
            background = true;

        }else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
            puts("-h | --help           Display help text and exit");
            puts("-B | --background     Daemonize process");
            return 0;

        }else{
            printf("ERROR: Unknown command line argument: '%s'\n", argv[i]);
            return 1;
        } //end if
    } //end if

    if(background){
        //close stdin, but not stdout/stderr (prevents ssh from blocking for user input)
        int null = open("/dev/null", O_RDWR);
        CHK(dup2(null, STDIN_FILENO), "Failed to dup2 /dev/null");
        CHK(daemon(false, true), "Failed to daemonize");
    } //end if

    //get sysfs paths as defined by environment variables (loaded from /etc/default/gpio by initscript)
    char *error_led   = gpio_path("GPIO_ERROR");
    char *active_led  = gpio_path("GPIO_ACTIVE");
    char *button_gpio = gpio_path("GPIO_BUTTON");
    char *notify_user = getenv("NOTIFY_USER");
    char *notify_host = getenv("NOTIFY_HOST");

    if(!error_led || !active_led || !button_gpio || !notify_user || !notify_host){
        puts("ERROR: Undefined environment variable");
        return 1;
    } //end if

    //turn off error LED now that we're running
    set_led(error_led, false);

    //open file for polling
    int ret, value;
    const int bufn = 4;
    char buf[bufn];
    int button_fd = open(button_gpio, O_RDONLY);
    struct pollfd poll_data = {
        .fd = button_fd,
        .events = POLLPRI,
        .revents = 0,
    };

    CHK(button_fd == -1, "Unable to open GPIO_BUTTON");

    //main loop
    //POLLPRI indicates an interrupt (i.e. change in value)
    //POLLERR will be present whenever we read at EOF, so we seek to start before each read
    while((ret = poll(&poll_data, 1, -1)) != -1){
        //printf("\nPoll returned %i and data %i\n", ret, poll_data.revents);

        if(poll_data.revents & (POLLNVAL | POLLHUP)){
            printf("ERROR polling GPIO_BUTTON: received %i\n", poll_data.revents);
            return 1;
        } //end if

        if(poll_data.revents & POLLPRI){
            CHK(lseek(button_fd, 0, SEEK_SET) == -1, "Seeking GPIO_BUTTON");
            CHK((ret = read(button_fd, &buf, bufn)) == -1, "Reading GPIO_BUTTON");

            buf[ret] = '\0';
            ret = atoi(buf);

            //debouncing logic - only activate when button is pressed down
            //blocking while the active LED is on automatically debounces the signal
            if(ret && !value){
                set_led(active_led, true);
                set_led(error_led, !!set_timer(notify_host, notify_user));
                sleep(3);
                set_led(active_led, false);
            } //end if

            value = ret;
        } //end if

    } //end while

    perror("ERROR: polling GPIO_BUTTON");
    return 1;
}

/* Finds the sysfs path for a GPIO, as defined by the specified environment variable.
 * Result is heap allocated and must be freed, or NULL in an error condition.
 */
char *gpio_path(char *envvar){
    //Lookup environmental variable
    char *name = getenv(envvar);

    if(!name){
        printf("ERROR: Could not find environment variable: %s\n", envvar);
        return NULL;
    } //end if

    //Convert to sysfs path
    char *fmt = "/sys/class/gpio/%s/value";
    char *res = malloc(strlen(name) + strlen(fmt) - 1);

    if(! res){
        puts("ERROR: out of memory");
        return NULL;
    } //end if

    sprintf(res, fmt, name);

    //Make sure the file actually exists. Note that it will have read-write permissions irrespective of the direction.
    if(access(res, R_OK | W_OK)){
        perror("ERROR");
        return NULL;
    } //end if

    return res;
}

/* Turns an LED on/off, given its sysfs path. */
void set_led(char *path, bool value){
    //using open() here since we don't need the overhead of fopen()
    int fd = open(path, O_WRONLY);

    if(fd == -1){
        perror("ERROR setting LED - open()");
        return;
    } //end if

    char buf = '0' + value;
    ssize_t ret;

    do{
        ret = write(fd, &buf, 1);
    }while(ret == 0 || (ret == -1 && errno == EINTR));

    if(ret == -1)
        perror("ERROR setting LED - write()");

    if(close(fd))
        perror("ERROR setting LED - close()");
}

/* Sets the timer remotely. Called when user presses button.
 * Returns 0 on success. */
int set_timer(char* host, char *user){
    struct addrinfo *res;
    int ret = getaddrinfo(host, NULL, NULL, &res);

    //quick check to make sure the host is available
    if(ret){
        printf("ERROR: resolving host: %s (%i)\n", gai_strerror(ret), ret);
        return ret;
    }else{
        freeaddrinfo(res);
    } //end if

    //perform notification
    char *fmt = "ssh -y -i /etc/dropbear/dropbear_ecdsa_host_key %s@%s \"DISPLAY=:0 xdg-open 'https://www.google.com.au/search?q=set+timer+5+min'\"";
    char cmd[strlen(fmt) + strlen(user) + strlen(host)];
    sprintf(cmd, fmt, user, host);
    ret = system(cmd);

    if(ret)
        printf("ERROR: notifying user: %i\n", ret);

    return ret;
}
