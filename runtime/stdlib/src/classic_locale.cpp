#include <ctype.h>
#include <locale.h>
#include <string.h>
#include <errno.h>
struct __scratch_locale { int tag; };
namespace { __scratch_locale classic{0}; locale_t current=&classic; char empty[]="",dot[]=".",name[]="C";
 lconv conv={dot,empty,empty,empty,empty,empty,empty,empty,empty,empty,
  127,127,127,127,127,127,127,127,127,127,127,127,127,127};
 bool valid(const char *s){return s && (!*s || !strcmp(s,"C") || !strcmp(s,"POSIX"));}
}
extern "C" {
int isdigit(int c){return c>='0'&&c<='9';} int islower(int c){return c>='a'&&c<='z';}
int isupper(int c){return c>='A'&&c<='Z';} int isalpha(int c){return islower(c)||isupper(c);}
int isalnum(int c){return isalpha(c)||isdigit(c);} int isblank(int c){return c==' '||c=='\t';}
int isspace(int c){return c==' '||(c>='\t'&&c<='\r');} int iscntrl(int c){return (c>=0&&c<32)||c==127;}
int isprint(int c){return c>=32&&c<127;} int isgraph(int c){return c>32&&c<127;}
int ispunct(int c){return isgraph(c)&&!isalnum(c);} int isxdigit(int c){return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
int tolower(int c){return isupper(c)?c+32:c;} int toupper(int c){return islower(c)?c-32:c;}
int tolower_l(int c,locale_t){return tolower(c);} int toupper_l(int c,locale_t){return toupper(c);}
char *setlocale(int category,const char *s){if(category<0||category>LC_ALL|| (s&&!valid(s))){errno=EINVAL;return nullptr;}return name;}
lconv *localeconv(){return &conv;}
locale_t newlocale(int mask,const char *s,locale_t){if((mask&~LC_ALL_MASK)||!valid(s)){errno=ENOENT;return nullptr;}return &classic;}
void freelocale(locale_t){}
locale_t uselocale(locale_t p){locale_t old=current;if(p)current=p;return old;}
int strcoll_l(const char *a,const char *b,locale_t){return strcmp(a,b);}
size_t strxfrm_l(char *d,const char *s,size_t n,locale_t){return strxfrm(d,s,n);}
}
