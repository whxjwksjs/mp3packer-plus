#ifndef PTR_H
#define PTR_H

//#define _XOPEN_SOURCE 700

// Needed for intrinsics
//#//include <malloc.h>
#include <stdio.h>

#include <caml/mlvalues.h>
#include <caml/signals.h>
#include <caml/alloc.h>
#include <caml/fail.h>
#include <caml/memory.h>
#include <caml/custom.h>

#if defined(__WIN32__) || defined(__WIN32)
#ifndef _WIN32
#define _WIN32
#endif
#endif

#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#endif

// Microsoft typing
#ifdef _WIN32
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
#else
#include <stdint.h>
#define FALSE 0
#define TRUE 1
#endif

enum ptr_type {
	PTR_MALLOC,
	PTR_MMAP,
	PTR_VIRTUALALLOC,
	PTR_NULL // For ignoring
};

struct ptr_struct {
	char *begin;
	char *alloc_begin;
	intnat length;
	intnat align;
	enum ptr_type type;
};

#define Struct_val(x) ((struct ptr_struct *)Data_custom_val(x))
#define Begin_val(x) Struct_val(x)->begin
#define Alloc_begin_val(x) Struct_val(x)->alloc_begin
#define Length_val(x) Struct_val(x)->length
#define Align_val(x) Struct_val(x)->align
#define Type_val(x) Struct_val(x)->type




#endif /* PTR_H */
