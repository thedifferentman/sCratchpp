#ifndef SCRATCH_CTYPE_H
#define SCRATCH_CTYPE_H
#include <locale.h>
#ifdef __cplusplus
extern "C" {
#endif
int isalnum(int); int isalpha(int); int isblank(int); int iscntrl(int); int isdigit(int);
int isgraph(int); int islower(int); int isprint(int); int ispunct(int); int isspace(int);
int isupper(int); int isxdigit(int); int tolower(int); int toupper(int);
int tolower_l(int,locale_t); int toupper_l(int,locale_t);
#ifdef __cplusplus
}
#endif
#endif
