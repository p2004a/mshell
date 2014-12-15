#include "config.h"
#include "utils.h"

void swap_ptr(void ** a, void ** b) {
	void * tmp_ptr = *a;
	*a = *b;
	*b = tmp_ptr;
}
