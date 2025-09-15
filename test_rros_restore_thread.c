#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

/* System call number for rros_restore_thread */
#define __NR_rros_restore_thread 447

/**
 * test_rros_restore_thread - Test program for rros_restore_thread system call
 * 
 * This program demonstrates how to call the rros_restore_thread system call
 * from user space.
 */
int main(int argc, char *argv[])
{
    long ret;
    
    printf("Testing RROS restore thread system call...\n");
    printf("System call number: %d\n", __NR_rros_restore_thread);
    
    /* Call the system call */
    ret = syscall(__NR_rros_restore_thread);
    
    if (ret == 0) {
        printf("SUCCESS: rros_restore_thread system call completed successfully\n");
    } else {
        printf("ERROR: rros_restore_thread system call failed with return code: %ld\n", ret);
        printf("Error: %s\n", strerror(errno));
        
        switch (errno) {
            case EPERM:
                printf("Hint: This system call requires root privileges (CAP_SYS_ADMIN)\n");
                break;
            case ENOMEM:
                printf("Hint: Memory allocation failed\n");
                break;
            case EFAULT:
                printf("Hint: Failed to map DTS reserved memory\n");
                break;
            case ENODATA:
                printf("Hint: No crash kernel data found in reserved memory\n");
                break;
            case EINVAL:
                printf("Hint: Invalid pointer in reserved memory\n");
                break;
            case ENOSYS:
                printf("Hint: System call not implemented or not available\n");
                break;
            default:
                printf("Hint: Unknown error\n");
                break;
        }
    }
    
    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}