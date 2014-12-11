#ifndef _UTILS_H_
#define _UTILS_H_

#include "siparse.h"

void printcommand(command *, int);
void printpipeline(pipeline, int);
void printparsedline(line *);

command * pickfirstcommand(line *);

#endif /* !_UTILS_H_ */

#define EINTR_RETRY(res, call) do { res = (call); } while (res == -1 && errno == EINTR)
