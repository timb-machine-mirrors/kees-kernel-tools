/* Build with -Wall -O2 -fstrict-flex-arrays=3 -fsanitize=bounds -fsanitize=object-size -fstrict-flex-arrays=3 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "harness.h"

typedef unsigned char u8;
typedef signed char s8;

#define noinline __attribute__((__noinline__))

/* Used to stop optimizer from seeing constant expressions. */
volatile int unconst = 0;
volatile int debug = 0;

/*
 * Used make sure allocations are seen as "escaping" the local function
 * to avoid Dead Store Elimination.
 */
volatile void *escape;

#define REPORT_SIZE(p)      do {    \
	const size_t bdos = __builtin_dynamic_object_size(p, 1); \
	escape = p; \
	if (__builtin_constant_p(bdos)) { \
		if (bdos == SIZE_MAX) { \
			if (debug) TH_LOG(#p " has an unknown-to-bdos size"); \
		} else { \
			if (debug) TH_LOG(#p " has a fixed size: %zu (%zu elements of size %zu)", bdos, \
				bdos / sizeof(*(p)), sizeof(*(p))); \
		} \
	} else { \
		if (debug) TH_LOG(#p " has a dynamic size: %zu (%zu elements of size %zu)", bdos, \
			bdos / sizeof(*(p)), sizeof(*(p))); \
	} \
	if (debug) fflush(NULL); \
} while (0)

#if __has_attribute(__counted_by__)
# define __counted_by(member)	__attribute__((__counted_by__(member)))
#else
# define __counted_by(member)	/* __attribute__((__counted_by__(member))) */
#endif

#define DECLARE_FLEX_ARRAY(TYPE, NAME)		\
	struct {				\
		struct { } __empty_ ## NAME;	\
		TYPE NAME[];			\
	}

#define DECLARE_BOUNDED_FLEX_ARRAY(COUNT_TYPE, COUNT, TYPE, NAME)	\
	struct {							\
		COUNT_TYPE COUNT;					\
		TYPE NAME[] __counted_by(COUNT);			\
	}

#define DECLARE_FLEX_ARRAY_COUNTED_BY(TYPE, NAME, COUNTED_BY)		\
	struct {							\
		struct { } __empty_ ## NAME;				\
		TYPE NAME[] __counted_by(COUNTED_BY);			\
	}

#define MAX_INDEX	16
#define SIZE_BUMP	 2

enum enforcement {
	SHOULD_NOT_TRAP = 0,
	SHOULD_TRAP,
};

struct fixed {
	unsigned long flags;
	size_t count;
	int array[MAX_INDEX];
};

struct flex {
	unsigned long flags;
	long count;
	int array[];
};

struct annotated {
	unsigned long flags;
	long count;
	int array[] __counted_by(count);
};

struct multi {
	unsigned long flags;
	union {
		/* count member type intentionally mismatched to induce padding */
		DECLARE_BOUNDED_FLEX_ARRAY(int, count_bytes, unsigned char, bytes);
		DECLARE_BOUNDED_FLEX_ARRAY(s8,  count_ints,  unsigned char, ints);
		DECLARE_FLEX_ARRAY(unsigned char, unsafe);
	};
};

struct anon_struct {
	unsigned long flags;
	long count;
	int array[] __counted_by(count);
	//gcc: DECLARE_FLEX_ARRAY_COUNTED_BY(int, array, count);
};

struct composite {
	unsigned stuff;
	struct annotated inner;
};

/* Initial support in development. */
#ifdef COUNTED_BY_POINTERS
struct ptr_annotated {
	unsigned long flags;
	int *array __counted_by(count);
	unsigned char count;
};
#endif

/*
 * Test safe accesses at index 0 and index-1, then optionally check
 * what happens when accessing "index".
 */
#define TEST_ACCESS(p, array, index, enforcement)	do {	\
								\
	/* Index zero is in the array. */			\
	/*TH_LOG("array address: %p", (p)->array);*/		\
	if (debug) TH_LOG("safe: array[0] = 0xFF");			\
	(p)->array[0] = 0xFF;					\
	ASSERT_EQ((p)->array[0], 0xFF);				\
								\
	if (index > 1) {					\
		if (debug) TH_LOG("safe: array[%d] = 0xFF", index - 1);	\
		(p)->array[index - 1] = 0xFF;			\
		ASSERT_EQ((p)->array[index - 1], 0xFF);		\
	}							\
								\
	if (enforcement == SHOULD_TRAP) {			\
		/* "index" is expected to trap. */		\
		if (debug) TH_LOG("traps: array[%d] = 0xFF", index);	\
	} else {						\
		if (debug) TH_LOG("ignored: array[%d] = 0xFF", index);	\
	}							\
	(p)->array[index] = 0xFF;				\
	if (enforcement == SHOULD_TRAP) {			\
		/* Don't assert: test for lack of signal. */	\
		if (debug) TH_LOG("this should have been unreachable");	\
	}							\
} while (0)

/* Helper to hide the allocation size by using a leaf function. */
static struct flex * noinline alloc_flex(int index)
{
	struct flex *f;

	return malloc(sizeof(*f) + index * sizeof(*f->array));
}

/* Helper to hide the allocation size by using a leaf function. */
static struct annotated * noinline alloc_annotated(int index)
{
	struct annotated *p;

	p = malloc(sizeof(*p) + index * sizeof(*p->array));
	p->count = index;

	return p;
}

/* Helper to hide the allocation size by using a leaf function. */
static struct multi * noinline alloc_multi_ints(int index)
{
	struct multi *p;

	p = malloc(sizeof(*p) + index * sizeof(*p->ints));
	p->count_ints = index;

	return p;
}

/* Helper to hide the allocation size by using a leaf function. */
static struct multi * noinline alloc_multi_bytes(int index)
{
	struct multi *p;

	p = malloc(sizeof(*p) + index * sizeof(*p->bytes));
	p->count_bytes = index;

	return p;
}

/* Helper to hide the allocation size by using a leaf function. */
static struct anon_struct * noinline alloc_anon_struct(int index)
{
	struct anon_struct *p;

	p = malloc(sizeof(*p) + index * sizeof(*p->array));
	p->count = index;

	return p;
}

/* Helper to hide the allocation size by using a leaf function. */
static struct composite * noinline alloc_composite(int index)
{
	struct composite *p;

	p = malloc(sizeof(*p) + index * sizeof(*p->inner.array));
	p->inner.count = index;

	return p;
}

#ifdef COUNTED_BY_POINTERS
static struct ptr_annotated * noinline alloc_ptr_annotated(int index)
{
	struct ptr_annotated *p;
	void *a;

	/* Explicitly allocate out of order just to see if anything breaks. */
	a = malloc(index * sizeof(*p->array));
	p = malloc(sizeof(*p));
	p->array = a;
	p->count = index;

	return p;
}
#endif

/*
 * For a structure ending with a fixed-size array, sizeof(*p) should
 * match __builtin_object_size(p, 1), which should also match
 * __builtin_dynamic_object_size(p, 1). This should work for both the
 * array itself and the object as a whole.
 */
TEST(fixed_size_seen_by_bdos)
{
	struct fixed f = { };

	REPORT_SIZE(f.array);
	EXPECT_EQ(sizeof(f) - sizeof(f.array), offsetof(typeof(f), array));
	/* Check array size alone. */
	EXPECT_EQ(__builtin_object_size(f.array, 1), sizeof(f.array));
	EXPECT_EQ(__builtin_dynamic_object_size(f.array, 1), sizeof(f.array));
	/* Check check entire object size. */
	EXPECT_EQ(__builtin_object_size(&f, 1), sizeof(f));
	EXPECT_EQ(__builtin_dynamic_object_size(&f, 1), sizeof(f));
}

/*
 * For a fixed-size array, the sanitizer should trap when accessing
 * beyond the largest index.
 */
TEST_SIGNAL(fixed_size_enforced_by_sanitizer, SIGILL)
{
	struct fixed f = { };
	int index = MAX_INDEX + unconst;

	REPORT_SIZE(f.array);
	TEST_ACCESS(&f, array, index, SHOULD_TRAP);
}

/*
 * For a structure ending with a flexible array, sizeof(*p) should
 * match the offset of the flexible array. Since the size of the
 * array is unknown, both __builtin_object_size(p, 1) and
 * __builtin_dynamic_object_size(p, 1) should report SIZE_MAX. This
 * should be true for both the array itself and the object as a whole.
 */
TEST(unknown_size_unknown_to_bdos)
{
	struct flex *p;
	int index = MAX_INDEX + unconst;

	/* Hide actual allocation size from compiler. */
	p = alloc_flex(index);

	REPORT_SIZE(p->array);
	EXPECT_EQ(sizeof(*p), offsetof(typeof(*p), array));
	/* Check array size alone. */
	EXPECT_EQ(__builtin_object_size(p->array, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), SIZE_MAX);
	/* Check check entire object size. */
	EXPECT_EQ(__builtin_object_size(p, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p, 1), SIZE_MAX);
}

/*
 * For a structure ending with a flexible array, the sanitizer has
 * no information about max indexes and should not trap for any
 * accesses.
 */
TEST(unknown_size_ignored_by_sanitizer)
{
	struct flex *p;
	int index = MAX_INDEX + unconst;

	/* Hide actual allocation size from compiler. */
	p = alloc_flex(index);

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, index, SHOULD_NOT_TRAP);
}

/*
 * For a structure ending with a flexible array where the allocation
 * is visible (via the __alloc_size attribute), this size should be
 * visible to __builtin_dynamic_object_size(p, 1) for both the array
 * itself and the object as a whole. The result of sizeof() and
 * __builtin_object_size() should remain unchanged compared to the
 * "unknown" cases above.
 */
TEST(alloc_size_seen_by_bdos)
{
	int count = MAX_INDEX + unconst;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	struct flex *p = malloc(sizeof(*p) + count * sizeof(*p->array));

	REPORT_SIZE(p->array);
	EXPECT_EQ(sizeof(*p), offsetof(typeof(*p), array));
	/* Check array size alone. */
	EXPECT_EQ(__builtin_object_size(p->array, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), count * sizeof(*p->array));
	/* Check check entire object size. */
	EXPECT_EQ(__builtin_object_size(p, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p, 1), sizeof(*p) + count * sizeof(*p->array));
}

/*
 * For a structure ending with a flexible array where the allocation
 * is visible (via the __alloc_size attribute), the sanitizer should
 * trap when accessing beyond the highest index still contained by the
 * allocation.
 */
TEST_SIGNAL(alloc_size_enforced_by_sanitizer, SIGILL)
{
	int count = MAX_INDEX + unconst;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	struct flex *p = malloc(sizeof(*p) + count * sizeof(*p->array));

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, count, SHOULD_TRAP);
}

#if defined(__clang__)
#define CLANG_ONLY(a...)	a
#else
#define CLANG_ONLY(a...)	/**/
#endif

/*
 * For a structure ending with a flexible array where the allocation
 * is hidden, but the array size member (identified with the
 * __counted_by attribute) has been set, the calculated size should
 * be visible to __builtin_dynamic_object_size(p, 1) for both the array
 * itself and the object as a whole. The result of sizeof() and
 * __builtin_object_size() should remain unchanged compared to the
 * "unknown" cases above.
 */
#define CHECK(p, array, count)						\
	REPORT_SIZE(p->array);						\
									\
	EXPECT_GE(sizeof(*p), offsetof(typeof(*p), array));		\
	/* Check single array indexes. */				\
	EXPECT_EQ(__builtin_object_size(&p->array[index - 1], 1), SIZE_MAX); \
	EXPECT_EQ(__builtin_dynamic_object_size(&p->array[index - 1], 1), sizeof(*p->array)); \
	EXPECT_EQ(__builtin_dynamic_object_size(&p->array[index], 1), 0); \
	\
	/* GCC's sanitizer trips even in __bdos */ \
	/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=116984 */ \
	CLANG_ONLY(EXPECT_EQ(__builtin_dynamic_object_size(&p->array[negative], 1), 0)); \
	/* Check array size alone. */					\
	EXPECT_EQ(__builtin_object_size(p->array, 1), SIZE_MAX);	\
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), p->count * sizeof(*p->array)); \
	/* Check check entire object size. */				\
	EXPECT_EQ(__builtin_object_size(p, 1), SIZE_MAX);		\
									\
	/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=116984 */	\
	/* Whole object sizing also got disabled in Clang ... */	\
	/*CLANG_ONLY(EXPECT_EQ(__builtin_dynamic_object_size(p, 1), sizeof(*p) + p->count * sizeof(*p->array)));*/ \
	/* so instead just check for what GCC and Clang report: -1 */	\
	EXPECT_EQ(__builtin_dynamic_object_size(p, 1), SIZE_MAX);	\
									\
	/* Check for out of bounds count. */				\
	p->count = negative;						\
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), 0) {	\
		TH_LOG("Failed when " #p "->" #count "=%d", negative);	\
	}								\
	/* Check for reduced counts. */					\
	p->count = 1 + unconst;						\
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), sizeof(*p->array));	\
	p->count = 0 + unconst;						\
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), 0) {	\
		TH_LOG("Failed when count=0");				\
	}								\
	do { } while (0)

#define TEST_BDOS(name, type, array, count)	\
TEST(counted_by_ ## name ## _seen_by_bdos)	\
{						\
	type *p;				\
	int index = MAX_INDEX + unconst;	\
	int negative = -3 + unconst;		\
						\
	p = alloc_ ## name(index);		\
	CHECK(p, array, count);			\
}

TEST_BDOS(annotated, struct annotated, array, count)
TEST_BDOS(multi_ints, struct multi, ints, count_ints)
TEST_BDOS(multi_bytes, struct multi, bytes, count_bytes)
TEST_BDOS(anon_struct, struct anon_struct, array, count)
TEST_BDOS(composite, struct composite, inner.array, inner.count)
#ifdef COUNTED_BY_POINTERS
TEST_BDOS(ptr_annotated, struct ptr_annotated, array, count)
#endif

#undef CHECK

/*
 * For a structure ending with a flexible array where the allocation
 * is hidden, but the array size member (identified with the
 * __counted_by attribute) has been set, the sanitizer should trap
 * when accessing beyond the index stored in the counted_by member.
 */
TEST_SIGNAL(counted_by_enforced_by_sanitizer, SIGILL)
{
	struct annotated *p;
	int index = MAX_INDEX + unconst;

	p = alloc_annotated(index);

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, index, SHOULD_TRAP);
}

TEST_SIGNAL(counted_by_enforced_by_sanitizer_with_negative_index, SIGILL)
{
	struct annotated *p;
	int index = MAX_INDEX + unconst;
	int negative_one = -1 + unconst;

	p = alloc_annotated(index);

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, negative_one, SHOULD_TRAP);
}

TEST_SIGNAL(counted_by_enforced_by_sanitizer_with_negative_bounds, SIGILL)
{
	struct annotated *p;
	int index = MAX_INDEX + unconst;
	int negative_one = -1 + unconst;

	p = alloc_annotated(index);

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, index - 1, SHOULD_NOT_TRAP);
	p->count = negative_one;
	if (debug) TH_LOG("traps: array[%d] = 0xEE (when count == -1)", index - 1);
	p->array[index - 1] = 0xEE;

#ifdef __clang__
	XFAIL(return, "Clang doesn't pass this yet");
#endif

	TH_LOG("this should have been unreachable");
}

TEST_SIGNAL(counted_by_enforced_by_sanitizer_multi_ints, SIGILL)
{
	struct multi *m;
	int index = MAX_INDEX + unconst;

	m = alloc_multi_ints(index);

	REPORT_SIZE(m->ints);
	TEST_ACCESS(m, ints, index, SHOULD_TRAP);
}

TEST_SIGNAL(counted_by_enforced_by_sanitizer_multi_bytes, SIGILL)
{
	struct multi *m;
	int index = MAX_INDEX + unconst;

	m = alloc_multi_bytes(index);

	REPORT_SIZE(m->bytes);
	TEST_ACCESS(m, bytes, index, SHOULD_TRAP);
}

TEST_SIGNAL(counted_by_enforced_by_sanitizer_anon_struct, SIGILL)
{
	struct anon_struct *s;
	int index = MAX_INDEX + unconst;

	s = alloc_anon_struct(index);

	REPORT_SIZE(s->array);
	TEST_ACCESS(s, array, index, SHOULD_TRAP);
}

TEST_SIGNAL(counted_by_enforced_by_sanitizer_composite, SIGILL)
{
	struct composite *c;
	int index = MAX_INDEX + unconst;

	c = alloc_composite(index);

	REPORT_SIZE(c->inner.array);
	TEST_ACCESS(c, inner.array, index, SHOULD_TRAP);
}

#ifdef COUNTED_BY_POINTERS
TEST_SIGNAL(counted_by_enforced_by_sanitizer_pointer, SIGILL)
{
	struct ptr_annotated *p;
	int index = MAX_INDEX + unconst;

	p = alloc_ptr_annotated(index);

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, index, SHOULD_TRAP);
}
#endif

/*
 * When both __alloc_size and __counted_by are available to calculate
 * sizes, the smaller of the two should take precedence. Check that when
 * the visible allocation is larger than the size calculated from the
 * counted_by member's value, __builtin_dynamic_object_size(p, 1) sees
 * the smaller of the two (the allocation), with everything else unchanged.
 */
TEST(alloc_size_with_smaller_counted_by_seen_by_bdos)
{
	int count = MAX_INDEX + unconst;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	struct annotated *p = malloc(sizeof(*p) + (count + SIZE_BUMP) * sizeof(*p->array));
	p->count = count;

	REPORT_SIZE(p->array);
	EXPECT_EQ(sizeof(*p), offsetof(typeof(*p), array));
	/* Check array size alone. */
	EXPECT_EQ(__builtin_object_size(p->array, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), p->count * sizeof(*p->array));
	/* Check check entire object size: this is not limited by __counted_by. */
	EXPECT_EQ(__builtin_object_size(p, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p, 1), sizeof(*p) + (p->count + SIZE_BUMP) * sizeof(*p->array));

#ifdef __clang__
	/* This fails on Clang currently due to sub-object __bdos() bug. */
	if (_metadata->passed)
		TH_LOG("Unexpected pass!");
	else
		XFAIL(return, "Clang doesn't pass this yet");
#endif
}

/*
 * When both __alloc_size and __counted_by are available to calculate
 * sizes, the smaller of the two should take precedence. Check that when
 * the visible allocation is larger than the size calculated from the
 * counted_by member's value, the sanitizer should trap when accessing
 * beyond the index stored in the counted_by member.
 */
TEST_SIGNAL(alloc_size_with_smaller_counted_by_enforced_by_sanitizer, SIGILL)
{
	int count = MAX_INDEX + unconst;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	struct annotated *p = malloc(sizeof(*p) + (count + SIZE_BUMP) * sizeof(*p->array));
	p->count = count;

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, count, SHOULD_TRAP);
}

/*
 * When both __alloc_size and __counted_by are available to calculate
 * sizes, the smaller of the two should take precedence. Check that when
 * the visible allocation that is smaller than the size calculated from
 * the counted_by member's value, __builtin_dynamic_object_size(p, 1)
 * sees the smaller of the two (the counted_by member size calculation),
 * with everything else unchanged.
 */
TEST(alloc_size_with_bigger_counted_by_seen_by_bdos)
{
	int count = MAX_INDEX + unconst;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	struct annotated *p = malloc(sizeof(*p) + count * sizeof(*p->array));
	p->count = count + SIZE_BUMP;

	REPORT_SIZE(p->array);
	EXPECT_EQ(sizeof(*p), offsetof(typeof(*p), array));
	/* Check array size alone. */
	EXPECT_EQ(__builtin_object_size(p->array, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), (p->count - SIZE_BUMP) * sizeof(*p->array));
	/* Check check entire object size. */
	EXPECT_EQ(__builtin_object_size(p, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p, 1), sizeof(*p) + (p->count - SIZE_BUMP) * sizeof(*p->array));

	/*
	 * Both Clang and GCC prioritize counted_by above alloc_size.
	 * If this is actually a bug, we'll need to very clearly define
	 * how alloc_size and counted_by interact.
	 */
	if (_metadata->passed)
		TH_LOG("Unexpected pass!");
	else
		XFAIL(/* */, "Not expected yet for GCC nor Clang");
}

/*
 * When both __alloc_size and __counted_by are available to calculate
 * sizes, the smaller of the two should take precedence. Check that when
 * the visible allocation is smaller than the size calculated from the
 * counted_by member's value, the sanitizer should trap when accessing
 * beyond the highest index still contained by the allocation.
 */
TEST_SIGNAL(alloc_size_with_bigger_counted_by_enforced_by_sanitizer, SIGILL)
{
	int count = MAX_INDEX + unconst;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	struct annotated *p = malloc(sizeof(*p) + count * sizeof(*p->array));
	p->count = count + SIZE_BUMP;

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, count, SHOULD_TRAP);

#if !defined(__clang__)
	XFAIL(return, "Not expected yet for GCC");
#endif

	TH_LOG("this should have been unreachable");
}

/*
 * When both __alloc_size and __counted_by are available to calculate
 * sizes, both GCC and Clang agree about taking alloc_size on an outer
 * variable. Test that this is distinguished.
 */
TEST_SIGNAL(alloc_size_outside_counted_by, SIGILL)
{
	int count = MAX_INDEX + unconst;
	struct annotated *p;
	size_t bytes = sizeof(*p) + (count + SIZE_BUMP) * sizeof(*p->array);
	void *whole;
	u8 *b;

	/* malloc() is marked with __attribute__((alloc_size(1))) */
	p = whole = malloc(bytes);
	p->count = count;
	b = whole;

	REPORT_SIZE(p->array);
	TEST_ACCESS(p, array, count - 1, SHOULD_NOT_TRAP);

	/* Check sizes */
	EXPECT_EQ(sizeof(*p), offsetof(typeof(*p), array));
	/* Check array size alone. */
	EXPECT_EQ(__builtin_object_size(p->array, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p->array, 1), p->count * sizeof(*p->array));
	/* Check check entire object size. */
	EXPECT_EQ(__builtin_object_size(p, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(p, 1), sizeof(*p) + p->count * sizeof(*p->array));
	/* Check whole allocation object size. */
	EXPECT_EQ(__builtin_object_size(b, 1), SIZE_MAX);
	EXPECT_EQ(__builtin_dynamic_object_size(b, 1), bytes);

	/* Verify sanitizer */
	memset(b, 0xAA, bytes);
	EXPECT_EQ(b[bytes - 1], 0xAA);

	/* This should trap. */
	EXPECT_NE(b[bytes], 0xAA);

	TH_LOG("this should have been unreachable");
}

#if 0
/*
 * It would be nice to have a way to verify expected compile time failures.
 * For example, we want this warning:
 *
 * array-bounds.c: In function 'invalid_assignment_order':
 * array-bounds.c:366:17: warning: '*p.count' is used uninitialized [-Wuninitialized]
 *   366 |         p->array[0] = 0;
 *       |         ~~~~~~~~^~~
 */
TEST(invalid_assignment_order)
{
	int count = MAX_INDEX + unconst;

	struct annotated *p = malloc(sizeof(*p) + count * sizeof(*p->array));

	/* It should be a build error to access "array" before "count" is set. */
	p->array[0] = 0;
	p->count = 1;
}
#endif

TEST_HARNESS_MAIN
