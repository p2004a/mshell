#ifndef _UTILS_H_
#define _UTILS_H_

#define EINTR_RETRY(res, call) do { res = (call); } while (res == -1 && errno == EINTR)

void swap_ptr(void ** a, void ** b);

#endif /* !_UTILS_H_ */
