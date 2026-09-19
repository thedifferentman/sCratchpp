#ifndef SCRATCH_LOCALE_H
#define SCRATCH_LOCALE_H
#include <stddef.h>
#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL 6
#define LC_CTYPE_MASK 1
#define LC_NUMERIC_MASK 2
#define LC_TIME_MASK 4
#define LC_COLLATE_MASK 8
#define LC_MONETARY_MASK 16
#define LC_MESSAGES_MASK 32
#define LC_ALL_MASK 63
#define LC_GLOBAL_LOCALE ((locale_t)-1)
typedef struct __scratch_locale *locale_t;
struct lconv {
 char *decimal_point,*thousands_sep,*grouping,*int_curr_symbol,*currency_symbol;
 char *mon_decimal_point,*mon_thousands_sep,*mon_grouping,*positive_sign,*negative_sign;
 char int_frac_digits,frac_digits,p_cs_precedes,p_sep_by_space,n_cs_precedes,n_sep_by_space,p_sign_posn,n_sign_posn;
 char int_p_cs_precedes,int_p_sep_by_space,int_n_cs_precedes,int_n_sep_by_space,int_p_sign_posn,int_n_sign_posn;
};
#ifdef __cplusplus
extern "C" {
#endif
char *setlocale(int,const char*);
struct lconv *localeconv(void);
locale_t newlocale(int,const char*,locale_t);
void freelocale(locale_t);
locale_t uselocale(locale_t);
float strtof_l(const char*,char**,locale_t);
double strtod_l(const char*,char**,locale_t);
long double strtold_l(const char*,char**,locale_t);
int strcoll_l(const char*,const char*,locale_t);
size_t strxfrm_l(char*,const char*,size_t,locale_t);
#ifdef __cplusplus
}
#endif
#endif
