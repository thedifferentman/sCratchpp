#include <stdio.h>
#include <scratch_io.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
struct __scratch_FILE { int fd; bool eof,error,closed; int pushed; unsigned char buffer[256]; size_t pos,end; bool dirty=false; int mode=_IOFBF; };
namespace { __scratch_FILE in{0,false,false,false,-1,{},0,0},out{1,false,false,false,-1,{},0,0},err{2,false,false,false,-1,{},0,0,false,_IONBF};
 bool valid(FILE *f){if(!f||f->closed){errno=EBADF;return false;}return true;}
}
extern "C" {
FILE *stdin=&in,*stdout=&out,*stderr=&err;
// Reads/writes require a real terminal provider at final link. Missing/old
// Console packages must be diagnosed instead of silently setting stream flags.
__attribute__((weak)) int __scrpp_io_flush(int){return 0;}
int fflush(FILE *f){if(!f){int a=fflush(stdout),b=fflush(stderr);return a||b?EOF:0;}if(!valid(f))return EOF;
 if(f->fd==0||!f->dirty)return 0;int r=__scrpp_io_flush(f->fd);if(r<0)f->error=true;else f->dirty=false;return r<0?EOF:0;}
int fgetc(FILE *f){if(!valid(f)||f->fd!=0){if(f)f->error=true;errno=EBADF;return EOF;}
 if(f->pushed>=0){int r=f->pushed;f->pushed=-1;return r;}if(f->eof)return EOF;
 if(f->pos==f->end){int r=__scrpp_io_read(f->buffer,sizeof(f->buffer));if(r<=0){if(r==0)f->eof=true;else f->error=true;return EOF;}f->pos=0;f->end=(size_t)r;}
 return f->buffer[f->pos++];}
int getc(FILE*f){return fgetc(f);} int getchar(){return fgetc(stdin);}
int ungetc(int c,FILE*f){if(!valid(f)||f->fd!=0||c==EOF||f->pushed>=0)return EOF;f->eof=false;f->pushed=(unsigned char)c;return f->pushed;}
size_t fread(void *p,size_t size,size_t n,FILE*f){if(!size||!n)return 0;if(n>SIZE_MAX/size){errno=EOVERFLOW;return 0;}
 auto*b=(unsigned char*)p;size_t i=0;for(;i<size*n;++i){int c=fgetc(f);if(c==EOF)break;b[i]=(unsigned char)c;}return i/size;}
size_t fwrite(const void*p,size_t size,size_t n,FILE*f){if(!size||!n)return 0;if(!valid(f)||f->fd==0){if(f)f->error=true;errno=EBADF;return 0;}
 if(n>SIZE_MAX/size){f->error=true;errno=EOVERFLOW;return 0;}const auto*b=(const unsigned char*)p;size_t done=0;
 while(done<size*n){size_t count=size*n-done;if(count>4096)count=4096;int r=__scrpp_io_write(f->fd,b+done,count);if(r<=0){f->error=true;break;}done+=(size_t)r;f->dirty=true;}if(f->mode==_IONBF||(f->mode==_IOLBF&&memchr(p,'\n',done)))fflush(f);return done/size;}
int fputc(int c,FILE*f){unsigned char b=(unsigned char)c;return fwrite(&b,1,1,f)==1?b:EOF;}
int putc(int c,FILE*f){return fputc(c,f);} int putchar(int c){return fputc(c,stdout);}
int fputs(const char*s,FILE*f){size_t n=strlen(s);return fwrite(s,1,n,f)==n?0:EOF;}
int puts(const char*s){return fputs(s,stdout)==EOF?EOF:fputc('\n',stdout);}
char*fgets(char*p,int n,FILE*f){if(n<=0)return nullptr;int i=0;for(;i<n-1;++i){int c=fgetc(f);if(c==EOF)break;p[i]=(char)c;if(c=='\n'){++i;break;}}p[i]=0;return i||n==1?p:nullptr;}
void clearerr(FILE*f){if(f){f->eof=f->error=false;}} int feof(FILE*f){return f&&f->eof;} int ferror(FILE*f){return f&&f->error;}
int fclose(FILE*f){if(!valid(f))return EOF;int r=fflush(f);f->closed=true;return r;}
int setvbuf(FILE*f,char*,int mode,size_t){if(!valid(f)||mode<0||mode>2){errno=EINVAL;return -1;}if(fflush(f)==EOF)return -1;f->mode=mode;return 0;}
void setbuf(FILE*f,char*p){(void)setvbuf(f,p,p?_IOFBF:_IONBF,BUFSIZ);}
int fseek(FILE*,long,int){errno=ESPIPE;return -1;} long ftell(FILE*){errno=ESPIPE;return -1;}
int fgetpos(FILE*,fpos_t*){errno=ESPIPE;return -1;}int fsetpos(FILE*,const fpos_t*){errno=ESPIPE;return -1;}
void rewind(FILE*f){(void)fseek(f,0,SEEK_SET);clearerr(f);}
}
