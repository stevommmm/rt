#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

const char* bash[] = {"/bin/bash", 0};

struct termios default_flags;

typedef struct rt_pty {
  int fd;
  char *name; // don't free name from ptsname()
} rt_pty;


void rt_term_set() {
  // Save current attributes for later
  tcgetattr(STDIN_FILENO, &default_flags);

  struct termios raw;
  tcgetattr(STDIN_FILENO, &raw);
  // don't mask ISIG as we want it passed to children, use signal() later
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void rt_term_unset() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &default_flags);
}

char *rt_syslog_ident() {
  char *username;
  char *logname;
  if ((username = getlogin()) != NULL) {
    logname = malloc(strlen(username) + 8);
    sprintf(logname, "rt-ses-%s", username);
    return logname;
  } else {
    fprintf(stderr, "Error resolving username: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
}


rt_pty *rt_open_pty() {
  rt_pty *pty = malloc(sizeof(rt_pty));

  pty->fd = posix_openpt(O_RDWR);
  if (pty->fd < 0) {
    fprintf(stderr, "Error opening pty: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  pty->name = ptsname(pty->fd);
  printf("pty[%d], name[%s]\n", pty->fd, pty->name);

  if (grantpt(pty->fd) < 0) {
    fprintf(stderr, "grantpt failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (unlockpt(pty->fd) < 0) {
    fprintf(stderr, "unlockpt failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  return pty;
}

void subshell(const char *pty_name) {
  // Connect up child to pty and run
  int slave_fd = open(pty_name, O_RDWR);

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  dup(slave_fd); // STDIN_FILENO
  dup(slave_fd); // STDOUT_FILENO
  dup(slave_fd); // STDERR_FILENO

  execvp((char*)bash[0], (char**)bash);
  perror("execvp");
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  pid_t cpid;
  int cpid_status;

  rt_pty *pty = rt_open_pty();

  cpid = fork();
  if (cpid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else if (cpid == 0) {
    // Connect up child to pty and run
    subshell(pty->name);
  }

  signal(SIGINT, SIG_IGN);

  char *syslog_ident = rt_syslog_ident();
  openlog(syslog_ident, LOG_PID, LOG_AUTH);
  syslog(LOG_INFO, "rt session beginning");

  int i;
  struct epoll_event event;
  memset(&event, 0, sizeof event); // keep valgrind quiet

  struct epoll_event *events;
  events = calloc(10, sizeof event);

  int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  event.events = EPOLLIN;
  event.data.fd = STDIN_FILENO;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);

  event.events = EPOLLIN;
  event.data.fd = pty->fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pty->fd, &event);

  // epoll read buffer
  char buffer[1];
  ssize_t byte_count;

  // resizing buffer to store lines of text for syslogging
  unsigned int syslog_buffer_size = 128;
  char *syslog_buffer = malloc(syslog_buffer_size * sizeof(char));
  syslog_buffer[0] = '\0';

  rt_term_set();

  while (waitpid(cpid, &cpid_status, WNOHANG) == 0) {
    int event_count = epoll_wait(epoll_fd, events, 10 /* max events */, -1);

    for(i = 0; i < event_count; i++) {
      byte_count = read(events[i].data.fd, buffer, 1);

      if (byte_count > 0 && events[i].data.fd == STDIN_FILENO) {
        // Anything typed into stdin gets sent on to our child process
        write(pty->fd, buffer, byte_count);
      } else if (byte_count > 0) {
        // Any other epoll reads are from child process, output to term and record
        write(STDOUT_FILENO, buffer, byte_count);

        if (strncmp(buffer, "\n", 1) == 0) {
          // on newline, log whatever we've got in our buffer
          if(strlen(syslog_buffer) > 0) {
            syslog(LOG_INFO, "%s", syslog_buffer);
            // syslog_buffer[0] = '\0';
            memset(syslog_buffer, 0, syslog_buffer_size * sizeof(char));
          }
        } else if (isprint(buffer[0])) {
          // If we're at the end of our buffer resize it
          if ((strlen(syslog_buffer) + 1 ) >= syslog_buffer_size) {
            syslog_buffer_size += 128;
            // @TODO don't throw away the original pointer
            if ((syslog_buffer = realloc(syslog_buffer, syslog_buffer_size * sizeof(char))) == NULL) {
              fprintf(stderr, "realloc failed: %s\n", strerror(errno));
              exit(EXIT_FAILURE);
            }
          }
          // Append the one char in our 'buffer'
          strncat(syslog_buffer, buffer, 1);
        }
      }
    }
  }

  free(syslog_buffer);

  close(epoll_fd);
  close(pty->fd);

  free(events);

  free(pty);
  kill(cpid, SIGKILL);

  rt_term_unset();
  closelog();
  free(syslog_ident);
  exit(EXIT_SUCCESS);
}
