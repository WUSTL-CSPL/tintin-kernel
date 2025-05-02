#ifndef TINTIN_UNCERTAINTY_H
#define TINTIN_UNCERTAINTY_H

#include <linux/tintin_event.h>

void tintin_update_uncertainty(
	struct tintin_event * sc);

void tintin_update_variance_by_Welfords_method(
	struct tintin_event * sc,
	u64 count, u64 time);

#endif //TINTIN_UNCERTAINTY_H