#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

void argument_stack(char **argv, int argc, struct intr_frame *if_);

// void exit (int status);
// void halt (void);
// bool create (const char *file, unsigned initial_size);
// bool remove (const char *file);
// void check_address(void *addr);
//int filesize (int fd);
// int write (int fd, const void *buffer, unsigned size);


#endif /* userprog/process.h */
