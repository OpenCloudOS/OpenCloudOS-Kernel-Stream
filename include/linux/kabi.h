/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kabi.h - mimimal kABI abstraction helpers, mostly debranded from Centos 9 Kernel
 *
 * Kernel KABI checking is based on checking the CRC values of every exported
 * symbol used by a module.
 *
 * All available symbols and CRC values are stored in Modules.symvers files.
 * For each .ko, CRCs of unresolved symbols are available in the __versions section.
 * CRC values are embedded in __kcrctab secion of vmlinux. .ko that provides extra
 * symbols also have a __ksymtab section.
 *
 * These CRCs are calculated based on the `gcc -E -D__GENKSYMS__` output of the
 * source code backing an object file, using genksyms tool. So genksyms see the
 * version of code with __GENKSYMS__ defined, and kernel binary used the version
 * of code with __GENKSYMS__ undefined.
 *
 * These kabi macros hide the changes from the kabi checker `genksyms` by
 * leveraging __GENKSYMS__ macro, so it keep the CRCs unchanged.
 *
 * CAUTION: Use of these macros does not make your kABI magically consistent,
 * it only helps bypass the kABI check, plese check your final result carefully
 * with tools like pahole.
 */

#ifndef _LINUX_KABI_H
#define _LINUX_KABI_H

#include <linux/compiler.h>
#include <linux/kconfig.h>
#include <linux/stringify.h>

#define __KABI_CHECK_SIZE_ALIGN(_orig, _new) \
union { \
	static_assert(sizeof(struct{_new; }) <= sizeof(struct{_orig; }), \
		"kABI failure: " \
		__stringify(_new) " is larger than " __stringify(_orig)); \
	static_assert(__alignof__(struct{_new; }) <= __alignof__(struct{_orig; }), \
		"kABI failure: " \
		__stringify(_new) " is not aligned the same as " __stringify(_orig)); \
}

#define __KABI_CHECK_SIZE(_item, _size) \
	static_assert(sizeof(struct{_item; }) <= _size, \
		"kABI failure: " \
		__stringify(_item) " is larger than reserved size (" __stringify(_size) " bytes)")

#define _KABI_RESERVE(n)		union {unsigned long kabi_reserved##n; }

/* Just a little wrapper to make checkpatch.pl happy */
#define _KABI_ARGS(...)			__VA_ARGS__

#ifdef __GENKSYMS__

#define KABI_ADD(_new)
#define KABI_EXTEND(_new)
#define KABI_FILL_HOLE(_new)
#define KABI_RENAME(_orig, _new)			_orig
#define KABI_DEPRECATE(_type, _orig)			_KABI_ARGS(_type _orig)
#define KABI_DEPRECATE_FN(_type, _orig, _args...)	_KABI_ARGS(_type(*_orig)(_args))
#define KABI_REPLACE(_orig, _new)			_orig
#define KABI_EXCLUDE(_elem)

#else

#define KABI_ADD(_new)					_new
#define KABI_EXTEND(_new)				_KABI_ARGS(_new;)
#define KABI_FILL_HOLE(_new)				_new
#define KABI_RENAME(_orig, _new)			_new
#define KABI_DEPRECATE(_type, _orig) \
	_KABI_ARGS(_type kabi_deprecate##_orig)
#define KABI_DEPRECATE_FN(_type, _orig, _args...) \
	_KABI_ARGS(_type(*kabi_deprecate##_orig)(_args))
#define KABI_REPLACE(_orig, _new)			\
	union {						\
		_new;					\
		struct {				\
			_orig;				\
		} __UNIQUE_ID(kabi_hidden_);		\
		__KABI_CHECK_SIZE_ALIGN(struct {_orig; }, _new);	\
	}
#define KABI_EXCLUDE(_elem)		_elem

#endif /* __GENKSYMS__ */

#define _KABI_REPLACE1(_new)		_new
#define _KABI_REPLACE2(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE1(__VA_ARGS__))
#define _KABI_REPLACE3(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE2(__VA_ARGS__))
#define _KABI_REPLACE4(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE3(__VA_ARGS__))
#define _KABI_REPLACE5(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE4(__VA_ARGS__))
#define _KABI_REPLACE6(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE5(__VA_ARGS__))
#define _KABI_REPLACE7(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE6(__VA_ARGS__))
#define _KABI_REPLACE8(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE7(__VA_ARGS__))
#define _KABI_REPLACE9(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE8(__VA_ARGS__))
#define _KABI_REPLACE10(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE9(__VA_ARGS__))
#define _KABI_REPLACE11(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE10(__VA_ARGS__))
#define _KABI_REPLACE12(_new, ...)	_KABI_ARGS(_new; _KABI_REPLACE11(__VA_ARGS__))
#define KABI_REPLACE_SPLIT(_orig, ...)	KABI_REPLACE(_orig, \
		struct { __PASTE(_KABI_REPLACE, COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__); })

#define KABI_RESERVE(n)		_KABI_RESERVE(n)
#define _KABI_USE1(n, _new)	_KABI_ARGS(_KABI_RESERVE(n), _new)
#define _KABI_USE2(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE1(__VA_ARGS__))
#define _KABI_USE3(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE2(__VA_ARGS__))
#define _KABI_USE4(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE3(__VA_ARGS__))
#define _KABI_USE5(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE4(__VA_ARGS__))
#define _KABI_USE6(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE5(__VA_ARGS__))
#define _KABI_USE7(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE6(__VA_ARGS__))
#define _KABI_USE8(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE7(__VA_ARGS__))
#define _KABI_USE9(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE8(__VA_ARGS__))
#define _KABI_USE10(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE9(__VA_ARGS__))
#define _KABI_USE11(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE10(__VA_ARGS__))
#define _KABI_USE12(n, ...)	_KABI_ARGS(_KABI_RESERVE(n); _KABI_USE11(__VA_ARGS__))
#define _KABI_USE(...)		KABI_REPLACE(__VA_ARGS__)
#define KABI_USE(n, ...) \
	_KABI_USE(__PASTE(_KABI_USE, COUNT_ARGS(__VA_ARGS__))(n, __VA_ARGS__))
#define KABI_USE_SPLIT(n, ...) \
	KABI_REPLACE_SPLIT(_KABI_RESERVE(n), __VA_ARGS__)

#define KABI_EXTEND_WITH_SIZE(_new, _size)				\
	KABI_EXTEND(union {						\
		_new;							\
		unsigned long __UNIQUE_ID(__kabi_hidden)[_size];	\
		__KABI_CHECK_SIZE(_new, 8 * (_size));			\
	})

#endif /* _LINUX_KABI_H */
