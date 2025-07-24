# Bank Simulator

This project simulates a banking system with multiple ATMs communicating with a central bank process via inter-process communication using UNIX pipes. It supports transactions like deposits, withdrawals, transfers, and balance checks through a structured command protocol.

---

## Overview

- Each ATM reads a trace file of commands and communicates with the bank process.
- The bank handles requests from all ATMs and ensures account consistency.
- Communication follows a fixed 17-byte `Command` structure.
- The simulator uses forked processes and bidirectional pipes for message passing.

---

## Project Structure

| File           | Description |
|----------------|-------------|
| `atm.c`        | Reads trace commands and sends them to the bank, handles bank responses. |
| `bank.c`       | Central bank logic: processes incoming ATM requests and maintains account balances. |
| `main.c`       | Sets up pipes, forks processes for each ATM and the bank, and starts the simulation. |
| `command.h`    | Defines the `Command` structure and related macros (e.g., `MSG_DEPOSIT`, `MSG_BALANCE`). |
| `errors.c/h`   | Defines error types and corresponding messages (e.g., insufficient funds). |
| `trace.c/h`    | Parses trace files to feed transactions into ATMs. |
| `twriter`, `treader` | Utility programs for generating and debugging trace files. |

---

## Features

- **Multi-ATM Simulation**: Multiple ATMs execute concurrently and independently.
- **Robust Transaction Handling**: Supports `CONNECT`, `DEPOSIT`, `WITHDRAW`, `TRANSFER`, `BALANCE`, `EXIT`.
- **Two-Way Communication**: Pipes allow request-response between each ATM and the bank.
- **Error Handling**: Proper responses for invalid accounts, overdrafts, or protocol errors.
- **Trace File Execution**: Load `.trace` binary files to simulate real-world ATM activity.

