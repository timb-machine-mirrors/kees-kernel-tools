#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>

#define noinline __attribute__((noinline))
#define struct_size(s, flex, count)	\
	(sizeof(*s) + sizeof(*(s)->flex) * (count))

#define __force
#define is_signed_type(type) (((type)(-1)) < (__force type)1)
#define is_unsigned_type(type) (!is_signed_type(type))
#define __type_half_max(type) ((type)1 << (8*sizeof(type) - 1 - is_signed_type(type)))
#define __type_max(T) ((T)((__type_half_max(T) - 1) + __type_half_max(T)))
#define type_max(t)     __type_max(typeof(t))
#define __type_min(T) ((T)((T)-type_max(T)-(T)1))
#define type_min(t)     __type_min(typeof(t))

#define WARN_ON_ONCE(fmt...)	fprintf(stderr, fmt)

#define alloc(P, FLEX, COUNT)	({ \
	size_t __count = (COUNT); \
	size_t __max_count; \
	typeof(P) __p = NULL; \
	if (__builtin_get_counted_by(__p->FLEX) && \
	    __count > (__max_count = type_max(typeof(*__builtin_get_counted_by(__p->FLEX))))) { \
		WARN_ON_ONCE("%zu cannot be stored in " #P "->" #FLEX "'s counted_by type which has max value %zu\n", __count, __max_count); \
	} else { \
		size_t __size = struct_size(__p, FLEX, __count); \
		__p = calloc(1, __size); \
		if (__p && __builtin_get_counted_by(__p->FLEX)) \
			*__builtin_get_counted_by(__p->FLEX) = __count; \
	} \
	(P) = __p; \
})

#define __counted_by(MEMBER)	__attribute__((__counted_by__(MEMBER)))

struct uncounted {
	int flags;
	unsigned char count;
	short array[];
};

struct counted {
	int flags;
	unsigned char count;
	short array[] __counted_by(count);
};

int main(int argc, char *argv[])
{
	struct uncounted *no;
	struct counted *yes;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s COUNT\n", argv[0]);
		return 1;
	}

	int count = atoi(argv[1]);

	alloc(no, array, count);
	alloc(yes, array, count);

	if (no)
		printf("uncounted: %d\n", no->count);
	else
		printf("uncounted not allocated\n");
	if (yes)
		printf("counted: %d\n", yes->count);
	else
		printf("counted not allocated\n");

	return 0;
}
