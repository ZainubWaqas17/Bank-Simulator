#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>

#include "hw.h"
#define P_READ 0
#define P_WRITE 1

// helper to make pipe || exit
void pipe_init(int p[2])
{
    int stat = pipe(p);
    bool unsucc = (stat == -1);

    unsucc ? (perror("pipe"), exit(EXIT_FAILURE))
           : (void)0;
}

// helper to do the fork || exit
pid_t apply_fork()
{
    pid_t outcome = fork();
    bool unsucc = (outcome < 0);

    unsucc ? (perror("fork"), exit(EXIT_FAILURE))
           : (void)0;
    return outcome;
}

// helper to close file des
void filedes_close(int f)
{
    int stat = close(f);
    bool unsucc = (stat == -1);

    unsucc ? (perror("close"), exit(EXIT_FAILURE))
           : (void)0;
}

// helper to manage the ATM child
void manage_achild(const char *original, int initial, int final, int id)
{
    int outcome = atm_run(original, initial, final, id);
    bool succ = (outcome == SUCCESS);

    succ ? (void)0
         : error_print();
    printf("atm %d: exit\n", id);

    exit(0);
}

// helper to manage the child logic
void manage_bchild(int sum_atm, int sum_acc, int in[], int out[])
{
    bank_open(sum_atm, sum_acc);

    int sim_success = run_bank(in, out);
    bool pass = (sim_success == SUCCESS);

    pass ? (void)0 : error_print();
    printf("bank: dump and close\n");

    bank_dump();
    bank_close();

    exit(0);
}

// This is the main driver file for the bank simulation.
// The `main` function takes one argument, the name of a
// trace file to use.  The test directory contains a
// short sample trace that exercises at least one interesting
// case.

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("usage: %s trace_file\n", argv[0]);
        exit(1);
    }

    int result = 0;
    int atm_count = 0;
    int account_count = 0;

    // open the trace file
    result = trace_open(argv[1]);
    if (result == -1)
    {
        printf("%s: could not open file %s\n", argv[0], argv[1]);
        exit(1);
    }

    // Get the number of ATMs and accounts:
    atm_count = trace_atm_count();
    account_count = trace_account_count();
    trace_close();
    printf("Main: ATM count = %d, Account count = %d\n", atm_count, account_count);

    // This is a table of atm_out file descriptors. It will be used by
    // the bank process to communicate to each of the ATM processes.
    int atm_out_fd[atm_count];

    // This is a table of bank_in file descriptors. It will be used by
    // the bank process to receive communication from each of the ATM processes.
    int bank_in_fd[atm_count];

    // TODO: ATM PROCESS FORKING

    const char *t_file = argv[1];

    for (int i = 0; i < atm_count; i++)
    {
        printf("fork atm %d\n", i);
        int atm_p[2];
        int bank_p[2];

        pipe_init(atm_p);
        pipe_init(bank_p);

        int atm_w = atm_p[P_WRITE];
        int atm_r = bank_p[P_READ];

        int bank_w = bank_p[P_WRITE];
        int bank_r = atm_p[P_READ];

        atm_out_fd[i] = bank_w;
        bank_in_fd[i] = bank_r;

        pid_t p_c = apply_fork();
        if (p_c == -1)
        {
            perror("fork");
            exit(1);
        }

        switch (p_c == 0)
        {
        case 1: // child process
            printf("atm %d: child process forked\n", i);
            filedes_close(atm_p[P_READ]);
            filedes_close(bank_p[P_WRITE]);
            manage_achild(t_file, atm_w, atm_r, i);
            break;

        default: // parent process
            filedes_close(atm_p[P_WRITE]);
            filedes_close(bank_p[P_READ]);
            break;
        }
    }

    // TODO: BANK PROCESS FORKING
    printf("fork bank proc\n");
    pid_t pid_b = apply_fork();
    if (pid_b == 0)
    {
        manage_bchild(atm_count, account_count, bank_in_fd, atm_out_fd);
    }

    // Wait for each of the child processes to complete. We include
    // atm_count to include the bank process (i.e., this is not a
    // fence post error!)
    printf("Main: waiting for %d children (ATMs + bank)...\n", atm_count + 1);
    for (int i = 0; i <= atm_count; i++)
    {
        wait(NULL);
    }
    printf("Main: all children finished. Exiting.\n");
    return 0;
}
