#include <stdlib.h>
#include <locale.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <charconv>
#include <cstring>
#include <limits>
namespace {
unsigned digit(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='z')return c-'a'+10;if(c>='A'&&c<='Z')return c-'A'+10;return 99;}
unsigned long long integer(const char*s,char**end,int base,bool sign,unsigned long long maximum,unsigned long long negative_max){
 const char*orig=s;while(isspace((unsigned char)*s))++s;bool neg=*s=='-';if(*s=='+'||*s=='-')++s;
 if(base && (base<2||base>36)){errno=EINVAL;if(end)*end=(char*)orig;return 0;}
 if((base==0||base==16)&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')&&digit((unsigned char)s[2])<16){base=16;s+=2;}
 if(!base)base=*s=='0'?8:10;
 const char*start=s;unsigned long long result=0,limit=sign&&neg?negative_max:maximum;bool overflow=false;
 for(;digit((unsigned char)*s)<(unsigned)base;++s){unsigned d=digit((unsigned char)*s);if(result>(limit-d)/(unsigned)base)overflow=true;else if(!overflow)result=result*(unsigned)base+d;}
 if(end)*end=(char*)(s==start?orig:s);
 if(overflow){errno=ERANGE;return sign&&neg?0-negative_max:maximum;}
 return neg?0-result:result;
}
template<class T>T floating(const char*s,char**end){const char*orig=s;while(isspace((unsigned char)*s))++s;bool neg=*s=='-';if(*s=='+'||*s=='-')++s;
 auto fmt=std::chars_format::general;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')&&(isxdigit((unsigned char)s[2])||(s[2]=='.'&&isxdigit((unsigned char)s[3])))){s+=2;fmt=std::chars_format::hex;}
 auto r=std::__from_chars_floating_point<T>(s,s+strlen(s),fmt);
 if(r.__ec==std::errc::invalid_argument){if(end)*end=(char*)orig;return T(0);}
 if(end)*end=(char*)(s+r.__n);if(r.__ec==std::errc::result_out_of_range)errno=ERANGE;
 return neg?-r.__value:r.__value;
}
}
extern "C" {
long long strtoll(const char*s,char**e,int b){return (long long)integer(s,e,b,true,LLONG_MAX,(unsigned long long)LLONG_MAX+1);}
long strtol(const char*s,char**e,int b){return (long)integer(s,e,b,true,LONG_MAX,(unsigned long long)LONG_MAX+1);}
unsigned long long strtoull(const char*s,char**e,int b){return integer(s,e,b,false,ULLONG_MAX,0);}
unsigned long strtoul(const char*s,char**e,int b){return (unsigned long)integer(s,e,b,false,ULONG_MAX,0);}
int atoi(const char*s){return (int)strtol(s,nullptr,10);}long atol(const char*s){return strtol(s,nullptr,10);}long long atoll(const char*s){return strtoll(s,nullptr,10);}
float strtof(const char*s,char**e){return floating<float>(s,e);}double strtod(const char*s,char**e){return floating<double>(s,e);}
long double strtold(const char*s,char**e){static_assert(sizeof(long double)==sizeof(double));return floating<double>(s,e);}
double atof(const char*s){return strtod(s,nullptr);}
float strtof_l(const char*s,char**e,locale_t){return strtof(s,e);}double strtod_l(const char*s,char**e,locale_t){return strtod(s,e);}long double strtold_l(const char*s,char**e,locale_t){return strtold(s,e);}
}
