
#pragma once

#include <rc_description.h>

#define JCON_VERSION_MAJOR  2
#define JCON_VERSION_MINOR  0
#define JCON_VERSION_POINT  0
#define JCON_VERSION_POINT2 0


#ifdef __cplusplus
#  define EXTERN_C  extern "C"
#else
#  define EXTERN_C
#endif

#ifndef _CRT_STRINGIZE
#define __CRT_STRINGIZE(_Value) #_Value
#define _CRT_STRINGIZE(_Value) __CRT_STRINGIZE(_Value)
#endif  /* _CRT_STRINGIZE */

#ifndef _CRT_WIDE
#define __CRT_WIDE(_String) L ## _String
#define _CRT_WIDE(_String) __CRT_WIDE(_String)
#endif  /* _CRT_WIDE */

#ifndef _CRT_APPEND
#define __CRT_APPEND(_Value1, _Value2) _Value1 ## _Value2
#define _CRT_APPEND(_Value1, _Value2) __CRT_APPEND(_Value1, _Value2)
#endif  /* _CRT_APPEND */

#ifndef _T
#ifdef _UNICODE
#define __TXT(x)    L ## x
#else  /* _UNICODE */
#define __TXT(x)    x
#endif  /* _UNICODE */
#define _T(x)       __TXT(x)
#endif  /* _T */

#define JCON_VERSION                        \
    _CRT_APPEND(JCON_VERSION_MAJOR,         \
    _CRT_APPEND(JCON_VERSION_MINOR,         \
                JCON_VERSION_POINT))

#ifdef _WIN32
#  ifdef jcon_EXPORT
#    define JCONAPI_EXTERN      extern __declspec(dllexport)
#    define JCONAPI             __declspec(dllexport)
#  else
#    define JCONAPI_EXTERN      extern __declspec(dllimport)
#    define JCONAPI             __declspec(dllimport)
#  endif
#endif
#ifndef JCONAPI
#  define JCONAPI_EXTERN        extern
#  define JCONAPI
#endif


#define FILE_VERSION_MAJOR  JCON_VERSION_MAJOR
#define FILE_VERSION_MINOR  JCON_VERSION_MINOR
#define FILE_VERSION_POINT  JCON_VERSION_POINT
#define FILE_VERSION_POINT2 JCON_VERSION_POINT2


#define INTERAL_NAME        _T(_CRT_STRINGIZE(PROJECT_NAME))
#define FILE_DESCRIPTION    _T("JSON RPC 2.0 library using C++ 11 and Qt 5.")
#define PRODUCT_NAME        FILE_DESCRIPTION
#define LEGAL_COPYRIGHT     _T("Copyright (C) 2016-2017 ") COMPANY_NAME

#define FILE_VERSION        _T(_CRT_STRINGIZE(FILE_VERSION_MAJOR)) _T(".")    \
                            _T(_CRT_STRINGIZE(FILE_VERSION_MINOR)) _T(".")    \
                            _T(_CRT_STRINGIZE(FILE_VERSION_POINT)) _T(".")    \
                            _T(_CRT_STRINGIZE(FILE_VERSION_POINT2))
