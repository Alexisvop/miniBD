#ifndef MINIDB_STATUS_H
#define MINIDB_STATUS_H

typedef enum {
    DB_OK,
    DB_IO_ERROR,
    DB_PERMISSION_DENIED,
    DB_NO_MEMORY,
    DB_LIMIT_REACHED,
    DB_INVALID_FORMAT,
    DB_UNSUPPORTED_VERSION,
    DB_INVALID_RECORD,
    DB_DUPLICATE_ID,
    DB_NOT_FOUND
} DbStatus;

const char *db_status_message(DbStatus status);

#endif
