#ifndef MINIDB_EXECUTOR_H
#define MINIDB_EXECUTOR_H

#include "parser.h"
#include "storage.h"
#include <stdio.h>

/* Borrows all arguments. Dispatches only INSERT/SELECT/SELECT_ALL. */
DbStatus executor_execute(Storage *storage, const Command *command, FILE *output);

#endif
