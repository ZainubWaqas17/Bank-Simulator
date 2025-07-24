#include "atm.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "command.h"
#include "errors.h"
#include "trace.h"
#include <stdbool.h>

// The following functions should be used to read and write
// groups of data over the pipes.  Use them in the implementation
// of the `atm` function below!

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
      error_msg(ERR_PIPE_WRITE_ERR, "could not write message to bank");
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
    if (result >= 0)
    {
      // this approach handles both complete and partial reads
      d += result;
      n -= result;
    }
    else
    {
      error_msg(ERR_PIPE_READ_ERR, "could not read message from bank");
      return ERR_PIPE_READ_ERR;
    }
  }
  return SUCCESS;
}

// helper to check is cmd belongs to the curr atm
static bool atm_is_correct(int id_cmd, int id_curr)
{
  bool is_holder = id_cmd == id_curr;
  return is_holder ? true : false;
}

// helper to send cmd to bank
static int bank_send(int out, Command *c)
{
  int outcome = checked_write(out, c, MESSAGE_SIZE);
  bool success = (outcome == SUCCESS);

  return success ? outcome : (error_print(), outcome);
}

// helper to received cmd from bank
static int res_get(int in, Command *res)
{
  int outcome = checked_read(in, res, MESSAGE_SIZE);
  bool success = (outcome == SUCCESS);

  return success ? outcome : (error_print(), outcome);
}

// helper to interpret reply from bank and return status accord
static int res_manage(Command *res)
{
  cmd_t case_type;
  int i, f, t, a;
  cmd_unpack(res, &case_type, &i, &f, &t, &a);

  switch (case_type)
  {
  case ACCUNKN:
    return ERR_UNKNOWN_ACCOUNT;
  case OK:
    return SUCCESS;
  case ATMUNKN:
    return ERR_UNKNOWN_ATM;

  case NOFUNDS:
    return ERR_NOFUNDS;

  default:
    error_msg(ERR_UNKNOWN_CMD, "invalid res cmd");
    return ERR_UNKNOWN_CMD;
  }
}

// helper to handle transaction req
static int handle_trans(int out, int in, Command *c, int i)
{
  cmd_dump("atm - bank", i, c);

  int outcome = bank_send(out, c);
  bool success_w = (outcome == SUCCESS);

  Command res;
  int resultant = success_w
                      ? res_get(in, &res)
                      : (error_print(), outcome);

  bool success_r = (resultant == SUCCESS);

  return (success_r)
             ? (cmd_dump("bank - atm", i, &res), res_manage(&res))
             : (error_print(), resultant);
}

// The `atm` function processes commands received from a trace
// file.  It communicates to the bank transactions with a matching
// ID.  It then receives a response from the bank process and handles
// the response appropriately.

int atm(int bank_out_fd, int atm_in_fd, int atm_id, Command *cmd)
{
  byte c;
  int i, f, t, a;
  // Command atmcmd;

  cmd_unpack(cmd, &c, &i, &f, &t, &a);

  // int status = SUCCESS;

  // TODO: your code here
  static bool con = false;
  if (!atm_is_correct(i, atm_id))
  {
    return ERR_UNKNOWN_ATM;
  }
  if (c == CONNECT)
  {
    if (con)
    {
      return SUCCESS;
    }
    con = true;
  }

  switch (c)
  {
  case CONNECT:
  case EXIT:
  case DEPOSIT:
  case WITHDRAW:
  case TRANSFER:
  case BALANCE:

    return handle_trans(bank_out_fd, atm_in_fd, cmd, atm_id);

  default:
    error_msg(ERR_UNKNOWN_CMD, "invalid atm cmd");
    return ERR_UNKNOWN_CMD;
  }

  // return status;
}

int atm_run(const char *trace, int bank_out_fd, int atm_in_fd, int atm_id)
{
  int status = trace_open(trace);
  if (status == -1)
  {
    error_msg(ERR_BAD_TRACE_FILE, "could not open trace file");
    return ERR_BAD_TRACE_FILE;
  }

  Command cmd;
  while (trace_read_cmd(&cmd))
  {
    status = atm(bank_out_fd, atm_in_fd, atm_id, &cmd);

    switch (status)
    {
    // We continue if the ATM was unknown. This is ok because the trace
    // file contains commands for all the ATMs.
    case ERR_UNKNOWN_ATM:
      break;

    // We display an error message to the ATM user if the account
    // is not valid.
    case ERR_UNKNOWN_ACCOUNT:
      printf("ATM error: unknown account! ATM Out of service\n");
      break;

    // We display an error message to the ATM user if the account
    // does not have sufficient funds.
    case ERR_NOFUNDS:
      printf("not enough funds, retry transaction\n");
      break;

    // If we receive some other status that is not successful
    // we return with the status.
    default:
      if (status != SUCCESS)
      {
        printf("status is %d\n", atm_id, status);
        return status;
      }
    }
  }

  trace_close();

  return SUCCESS;
}
