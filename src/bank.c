#include "bank.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "command.h"
#include "errors.h"
#include <stdbool.h>

// The account balances are represented by an array.
static int *accounts = NULL;

// The number of accounts.
static int account_count = 0;

// The number of ATMs.
static int atm_count = 0;

// This is used just for testing.
int *get_accounts() { return accounts; }

// Performs a `write` call, checking for errors and handlings
// partial writes. If there was an error it returns ERR_PIPE_WRITE_ERR.
// Note: data is void * since the actual type being written does not matter.

static int checked_write(int fd, void *data, int n)
{
  char *d = (char *)data;
  while (n > 0)
  {
    int result = write(fd, d, n);
    if (result >= 0)
    {
      // this approach handles both complete and partial writes
      d += result;
      n -= result;
    }
    else
    {
      error_msg(ERR_PIPE_WRITE_ERR, "could not write message to atm");
      perror("write");

      return ERR_PIPE_WRITE_ERR;
    }
  }
  return SUCCESS;
}

// Performs a `read` call, checking for errors and handlings
// partial read. If there was an error it returns ERR_PIPE_READ_ERR.
// Note: data is void * since the actual type being read does not matter.

static int checked_read(int fd, void *data, int n)
{
  char *d = (char *)data;
  while (n > 0)
  {
    int result = read(fd, d, n);

    if (result > 0)
    {
      // this approach handles both complete and partial reads
      d += result;
      n -= result;
      continue;
    }

    if (result == 0)
    {
      // indicates EOF
      return ERR_ATM_CLOSED;
    }

    error_msg(ERR_PIPE_READ_ERR, "could not read message from atm");
    return ERR_PIPE_READ_ERR;
  }
  return SUCCESS;
}

// Checks to make sure that the ATM id is a valid ID.

static int check_valid_atm(int atmid)
{
  if (0 <= atmid && atmid < atm_count)
  {
    return SUCCESS;
  }
  else
  {
    error_msg(ERR_UNKNOWN_ATM, "message received from unknown ATM");
    return ERR_UNKNOWN_ATM;
  }
}

// Checks to make sure the account ID is a valid account ID.

static int check_valid_account(int accountid)
{
  if (0 <= accountid && accountid < account_count)
  {
    return SUCCESS;
  }
  else
  {
    error_msg(ERR_UNKNOWN_ACCOUNT, "message received for unknown account");
    return ERR_UNKNOWN_ACCOUNT;
  }
}

// Opens a bank for business. It is provided the number of ATMs and
// the number of accounts.

void bank_open(int atm_cnt, int account_cnt)
{
  atm_count = atm_cnt;
  // Create the accounts:
  accounts = (int *)malloc(sizeof(int) * account_cnt);
  for (int i = 0; i < account_cnt; i++)
  {
    accounts[i] = 0;
  }
  account_count = account_cnt;
}

// Closes a bank.

void bank_close() { free(accounts); }

// Dumps out the accounts balances.

void bank_dump()
{
  for (int i = 0; i < account_count; i++)
  {
    printf("Account %d: %d\n", i, accounts[i]);
  }
}

// helper to prepare and send OK res
static int OK_send(int out, int i, int initial, int final, int total)
{
  Command outcome;
  MSG_OK(&outcome, i, initial, final, total);
  int resultant = checked_write(out, &outcome, MESSAGE_SIZE);
  return resultant == SUCCESS ? resultant : (error_print(), resultant);
}

// helper to prepare and send ACCUMKN response
static int accunkun_send(int out, int initial, int final, int total)
{
  Command outcome;
  MSG_ACCUNKN(&outcome, 0, total);
  int resultant = checked_write(out, &outcome, MESSAGE_SIZE);
  return resultant == SUCCESS ? resultant : (error_print(), resultant);
}

// helper to prepare and send NOFUNDS response
static int nofunds_send(int out, int initial, int final, int total)
{
  Command outcome;
  MSG_NOFUNDS(&outcome, 0, initial, total);
  int resultant = checked_write(out, &outcome, MESSAGE_SIZE);
  return resultant == SUCCESS ? resultant : (error_print(), resultant);
}

// helper to validate account for errors
static bool is_acc_val(int id_no, int out, int initial, int final, int total, int *outcome)
{
  int auth = check_valid_account(id_no);
  bool is_correct = (auth == SUCCESS);

  *outcome = is_correct
                 ? SUCCESS
                 : accunkun_send(out, initial, final, total);

  return is_correct;
}

// // helper to manage Connection
int conn_manage(int out, int i, int initial, int final)
{
  Command res_ok;
  MSG_OK(&res_ok, i, -1, -1, -1);
  cmd_dump("bank to atm", i, &res_ok);
  printf("BANK: sending OK to ATM %d on fd %d\n", i, out);

  int outcome = checked_write(out, &res_ok, MESSAGE_SIZE);
  bool succ = (outcome == SUCCESS);

  return outcome ? SUCCESS : (error_print(), outcome);
}

// helper to take care of exit
static int exit_manage(int out, int i, int initial, int final, int total, int *left)
{
  int rem = --(*left);
  (void)rem;

  int outcome = OK_send(out, i, initial, final, total);
  bool success = (outcome == SUCCESS);

  return success ? outcome : (error_print(), outcome);
}

// helper to manage money deposition
static int add_manage(int out, int i, int initial, int final, int total, int *outcome)
{
  bool val_acc = is_acc_val(final, out, initial, final, total, outcome);

  int resultant = val_acc
                      ? (accounts[final] += total, OK_send(out, i, initial, final, total))
                      : *outcome;

  return resultant;
}

// helper to manage money withdrawal
static int minus_manage(int out, int i, int initial, int final, int total, int *outcome)
{
  bool val_acc = is_acc_val(initial, out, initial, final, total, outcome);
  if (!val_acc)
    return *outcome;

  bool has_funds = accounts[initial] >= total;

  int resultant = has_funds
                      ? (accounts[initial] -= total, OK_send(out, i, initial, final, total))
                      : nofunds_send(out, initial, final, total);

  return resultant;
}

// helper to manage transfer of funds
static int trans_manage(int out, int i, int initial, int final, int total, int *outcome)
{
  bool check_from = is_acc_val(initial, out, initial, final, total, outcome);
  if (!check_from)
    return *outcome;

  bool check_to = is_acc_val(final, out, initial, final, total, outcome);
  if (!check_to)
    return *outcome;

  bool funds_enough = accounts[initial] >= total;

  int resultant = funds_enough
                      ? (accounts[initial] -= total,
                         accounts[final] += total, OK_send(out, i, initial, final, total))
                      : nofunds_send(out, initial, final, total);

  return resultant;
}

// helper to manage inquiry of balance
static int left_money_check(int out, int i, int initial, int final, int total, int *outcome)
{
  bool is_ok = is_acc_val(initial, out, initial, final, total, outcome);
  int rem = accounts[initial];

  int resultant = is_ok
                      ? OK_send(out, i, initial, final, rem)
                      : *outcome;

  return resultant;
}

// The `bank` function processes commands received from an ATM.  It
// processes the commands and makes the appropriate changes to the
// designated accounts if necessary.  For example, if it receives a
// DEPOSIT message it will update the `to` account with the deposit
// amount.  It then communicates back to the ATM with success or
// failure.

int bank(int atm_out_fd[], Command *cmd, int *atms_remaining)
{
  cmd_t c;
  int i, f, t, a;
  Command bankcmd;

  cmd_unpack(cmd, &c, &i, &f, &t, &a);
  cmd_dump("bank rec from atm", i, cmd);

  // int result = SUCCESS;

  // TODO: your code here
  int result = check_valid_atm(i);
  if (result != SUCCESS)
    return ERR_UNKNOWN_ATM;

  int out = atm_out_fd[i];

  switch (c)
  {
  case BALANCE:
    result = left_money_check(out, i, f, t, a, &result);
    break;

  case WITHDRAW:
    result = minus_manage(out, i, f, t, a, &result);
    break;

  case CONNECT:
    result = conn_manage(out, i, f, t);
    break;

  case TRANSFER:
    result = trans_manage(out, i, f, t, a, &result);
    break;

  case EXIT:
    result = exit_manage(out, i, f, t, a, atms_remaining);
    break;

  case DEPOSIT:
    result = add_manage(out, i, f, t, a, &result);
    break;

  default:
    error_msg(ERR_UNKNOWN_CMD, "invalid cmd received");
    result = ERR_UNKNOWN_CMD;
  }

  return result;
}

static int scanner = -1; // this scans ATMs circularly, for fairness

static struct pollfd *pollfds; // the fds and conditions to await

// sets up an fd_set holding the fds on which we may receive
// messages from ATMs
static void set_up_poll(int bank_in_fd[])
{
  pollfds = (struct pollfd *)(malloc(sizeof(struct pollfd) * atm_count));
  for (int i = 0; i < atm_count; ++i)
  {
    pollfds[i].fd = bank_in_fd[i];
    pollfds[i].events = POLLIN; // Note: can also return POLLHUP
    // no need to set revents - it's an output of poll
  }
}

// when an atm closes, we don't want to look for more
// input from it
static void note_atm_closed(int atm, int bank_in_fd[])
{
  pollfds[atm].fd = -1; // causes poll to ignore it
  close(bank_in_fd[atm]);
}

// Using scanner as a roving number of an ATM to check for input,
// tries to find a fd whose bit is set in in_fds and return the
// corresponding ATM number.
static int find_ready_atm()
{
  while (1)
  {
    int result = poll(pollfds, atm_count, -1);

    if (result < 0)
    {
      // error value returned by select
      printf("poll had an error ... stopping\n");
      return result;
    }

    if (result == 0)
    {
      // returned "early" -- ignore
      continue;
    }

    // at least one fd ready; find next one circularly
    for (int j = atm_count; --j >= 0;)
    {
      ++scanner;
      scanner %= atm_count;
      if (pollfds[scanner].revents)
      {
        // some event happened on this fd
        return scanner;
      }
    }
    // if we get here, we checked atm_count fds, so there is a problem
    printf("find_ready_atm: no ready fd when there should be one\n");
  }
}

// This simply repeatedly tries to read another command from the
// bank input fd (coming from any of the atms) and calls the bank
// function to process the message and develop and send a reply.
// It stops when there are no active atms.

int run_bank(int bank_in_fd[], int atm_out_fd[])
{
  Command cmd;

  int result = 0;
  int atms_remaining = atm_count;

  set_up_poll(bank_in_fd);

  while (atms_remaining != 0)
  {
    int found = find_ready_atm();
    if (found < 0)
      return found;

    // read input from (apparently) ready atm
    result = checked_read(bank_in_fd[found], &cmd, MESSAGE_SIZE);
    if (result == ERR_ATM_CLOSED)
    {
      note_atm_closed(found, bank_in_fd);
      continue;
    }

    if (result != SUCCESS)
      return result;

    result = bank(atm_out_fd, &cmd, &atms_remaining);

    if (result == ERR_UNKNOWN_ATM)
    {
      printf("received message from unknown ATM. Ignoring...\n");
      continue;
    }

    if (result != SUCCESS)
    {
      return result;
    }
    continue;
  }

  return SUCCESS;
}
