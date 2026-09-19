// Classic-locale printf formatting used by libc++ num_put. Float conversion is
// delegated to the adapted exact musl core, not Scratch host numbers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <charconv>
#include <limits>
namespace {
struct Sink {char*p;size_t size,count=0;bool failed=false;
 void put(char c){if(count+1<size)p[count]=c;if(count==(size_t)INT_MAX)failed=true;else ++count;}
 void bytes(const char*s,size_t n){for(size_t i=0;i<n;++i)put(s[i]);}
 void pad(char c,size_t n){for(size_t i=0;i<n;++i)put(c);}
 int end(){if(size)p[count<size?count:size-1]=0;if(failed){errno=EOVERFLOW;return -1;}return (int)count;}
};
#include "float_format.inc"
int decimal(const char*&p){unsigned r=0;while(*p>='0'&&*p<='9'){unsigned d=(unsigned)(*p++-'0');if(r>((unsigned)INT_MAX-d)/10)return -1;r=r*10+d;}return (int)r;}
}
extern "C" int vsnprintf(char*dst,size_t size,const char*fmt,va_list arguments){
 if(!fmt||(!dst&&size)){errno=EINVAL;return -1;}
 Sink out{dst,size};va_list ap;va_copy(ap,arguments);
 for(const char*p=fmt;*p&&!out.failed;){
  if(*p!='%'){out.put(*p++);continue;}++p;
  bool left=false,plus=false,space=false,zero=false,alternate=false;
  for(bool flags=true;flags;){switch(*p){case '-':left=true;++p;break;case '+':plus=true;++p;break;case ' ':space=true;++p;break;case '0':zero=true;++p;break;case '#':alternate=true;++p;break;default:flags=false;}}
  int width=0,precision=-1;
  if(*p=='*'){width=va_arg(ap,int);++p;if(width<0){if(width==INT_MIN){out.failed=true;break;}left=true;width=-width;}}
  else {width=decimal(p);if(width<0){out.failed=true;break;}}
  if(*p=='.'){++p;if(*p=='*'){precision=va_arg(ap,int);++p;}else {precision=decimal(p);if(precision<0){out.failed=true;break;}}}
  enum Length{normal,hh,h,l,ll,j,z,t,L};Length length=normal;
  switch(*p){case 'h':++p;length=h;if(*p=='h'){++p;length=hh;}break;case 'l':++p;length=l;if(*p=='l'){++p;length=ll;}break;case 'j':length=j;++p;break;case 'z':length=z;++p;break;case 't':length=t;++p;break;case 'L':length=L;++p;break;}
  const char spec=*p;if((spec=='s'||spec=='c')&&length!=normal){va_end(ap);errno=EINVAL;return -1;}if(!spec){errno=EINVAL;out.failed=true;break;}++p;
  char stack[80],prefix[3];const char*text=stack;size_t n=0,prefix_n=0,leading=0;char*owned=nullptr;
  bool numeric=false,integer=false;
  if(spec=='d'||spec=='i'||spec=='u'||spec=='o'||spec=='x'||spec=='X'||spec=='p'){
   numeric=integer=true;unsigned long long v;bool negative=false;const bool signed_value=spec=='d'||spec=='i';
   if(spec=='p'){v=(uintptr_t)va_arg(ap,void*);prefix[prefix_n++]='0';prefix[prefix_n++]='x';}
   else if(signed_value){long long value;
    switch(length){case l:value=va_arg(ap,long);break;case ll:case j:value=va_arg(ap,long long);break;case z:case t:value=va_arg(ap,ptrdiff_t);break;default:value=va_arg(ap,int);if(length==h)value=(short)value;else if(length==hh)value=(signed char)value;}
    negative=value<0;v=negative?0-(unsigned long long)value:(unsigned long long)value;
   }else {switch(length){case l:v=va_arg(ap,unsigned long);break;case ll:case j:v=va_arg(ap,unsigned long long);break;case z:case t:v=va_arg(ap,size_t);break;default:v=va_arg(ap,unsigned int);if(length==h)v=(unsigned short)v;else if(length==hh)v=(unsigned char)v;}}
   if(signed_value){if(negative)prefix[prefix_n++]='-';else if(plus)prefix[prefix_n++]='+';else if(space)prefix[prefix_n++]=' ';}
   unsigned base=(spec=='o'?8:(spec=='x'||spec=='X'||spec=='p'?16:10));
   if(alternate&&v&&base==16&&spec!='p'){prefix[prefix_n++]='0';prefix[prefix_n++]=spec;}
   auto r=std::to_chars(stack,stack+sizeof(stack),v,base);n=(size_t)(r.ptr-stack);
   if(precision==0&&v==0&&spec!='p')n=0;
   if(spec=='X')for(size_t i=0;i<n;++i)if(stack[i]>='a'&&stack[i]<='f')stack[i]-=32;
   if(precision>0&&(size_t)precision>n)leading=(size_t)precision-n;
   if(alternate&&base==8&&(!n||stack[0]!='0')&&!leading)leading=1;
  }else if(spec=='s'){
   text=va_arg(ap,const char*);if(!text)text="(null)";
   while(text[n]&&(precision<0||n<(size_t)precision))++n;
  }else if(spec=='c'){stack[0]=(char)va_arg(ap,int);n=1;}
  else if(spec=='%'){stack[0]='%';n=1;}
  else if(spec=='f'||spec=='F'||spec=='e'||spec=='E'||spec=='g'||spec=='G'||spec=='a'||spec=='A'){
   long double value=length==L?va_arg(ap,long double):(long double)va_arg(ap,double);
   int flags=(left?LEFT_ADJ:0)|(plus?MARK_POS:0)|(space?PAD_POS:0)|(zero?ZERO_PAD:0)|(alternate?ALT_FORM:0);
   if(fmt_fp(&out,value,width,precision,flags,spec)<0){out.failed=true;break;}
   continue;
  }else {va_end(ap);errno=EINVAL;return -1;}
  const size_t total=prefix_n+leading+n,pad=(size_t)width>total?(size_t)width-total:0;
  const bool zero_width=zero&&!left&&numeric&&(!integer||precision<0);
  if(!left&&!zero_width)out.pad(' ',pad);out.bytes(prefix,prefix_n);
  if(zero_width)out.pad('0',pad);out.pad('0',leading);out.bytes(text,n);if(left)out.pad(' ',pad);
  free(owned);
 }
 va_end(ap);return out.end();
}
extern "C" {
int snprintf(char*d,size_t n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(d,n,f,a);va_end(a);return r;}
int vsprintf(char*d,const char*f,va_list a){return vsnprintf(d,SIZE_MAX,f,a);}
int sprintf(char*d,const char*f,...){va_list a;va_start(a,f);int r=vsprintf(d,f,a);va_end(a);return r;}
int vasprintf(char**d,const char*f,va_list a){if(!d){errno=EINVAL;return -1;}*d=nullptr;int n=vsnprintf(nullptr,0,f,a);if(n<0)return -1;char*p=(char*)malloc((size_t)n+1);if(!p){errno=ENOMEM;return -1;}int r=vsnprintf(p,(size_t)n+1,f,a);if(r<0){free(p);return -1;}*d=p;return r;}
int asprintf(char**d,const char*f,...){va_list a;va_start(a,f);int r=vasprintf(d,f,a);va_end(a);return r;}
int vfprintf(FILE*out,const char*f,va_list a){char*p;int n=vasprintf(&p,f,a);if(n<0)return -1;bool ok=fwrite(p,1,(size_t)n,out)==(size_t)n;free(p);return ok?n:-1;}
int fprintf(FILE*out,const char*f,...){va_list a;va_start(a,f);int r=vfprintf(out,f,a);va_end(a);return r;}
int vprintf(const char*f,va_list a){return vfprintf(stdout,f,a);}
int printf(const char*f,...){va_list a;va_start(a,f);int r=vprintf(f,a);va_end(a);return r;}
}
