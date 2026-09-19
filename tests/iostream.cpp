#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <scratch_io.h>
namespace {
char written[4096];unsigned written_size;unsigned flushes;
const char *input="42 -17 18446744073709551615 1.25 true\nnext line\n\n";unsigned input_at;
struct Global { Global(){std::cout<<"G";} ~Global(){std::cout<<"Z";} } global;
}
extern "C" int __scrpp_io_write(int,const unsigned char*p,size_t n){if(written_size+n>=sizeof(written)){errno=EIO;return -1;}memcpy(written+written_size,p,n);written_size+=(unsigned)n;written[written_size]=0;return (int)n;}
extern "C" int __scrpp_io_read(unsigned char*p,size_t n){if(!input[input_at])return 0;size_t i=0;while(i<n&&input[input_at])p[i++]=(unsigned char)input[input_at++];return (int)i;}
extern "C" int __scrpp_io_flush(int){++flushes;unsigned last=written_size?written[written_size-1]:0;asm volatile("data_setvariableto VARIABLE=\"iostream_last\" VALUE=%0"::"r"(last):"memory");return 0;}
int main(){
 if(strcmp(written,"G"))return 1;
 std::locale classic("C");
 if(classic.name()!="C" || &std::use_facet<std::ctype<char>>(classic)!=&std::use_facet<std::ctype<char>>(std::locale::classic()))return 26;
 std::ostringstream s;
 s<<std::hex<<std::showbase<<255<<' '<<std::dec<<std::showpos<<42;
 if(s.str()!="0xff +42")return 2;
 s.str("");s.clear();s<<std::noshowbase<<std::noshowpos<<std::setfill('0')<<std::setw(5)<<17;
 if(s.str()!="00017")return 3;
 s.str("");s<<std::fixed<<std::setprecision(2)<<1.25<<' '<<-0.0;
 if(s.str()!="1.25 -0.00")return 4;
 s.str("");s<<std::scientific<<std::setprecision(3)<<123.0;
 if(s.str()!="1.230e+02")return 5;
 s.str("");s<<std::defaultfloat<<std::showpoint<<std::setprecision(5)<<1.25;
 if(s.str()!="1.2500")return 6;
 s.str("");s<<std::noshowpoint<<std::hexfloat<<1.5;
 if(s.str()!="0x1.8p+0")return 7;
 s.str("");s<<std::defaultfloat<<std::numeric_limits<double>::infinity()<<' '<<std::numeric_limits<double>::quiet_NaN();
 if(s.str()!="inf nan")return 8;
 s.str("");s<<std::setprecision(17)<<std::numeric_limits<double>::max()<<' '<<std::numeric_limits<double>::denorm_min();
 if(s.str()!="1.7976931348623157e+308 4.9406564584124654e-324")return 27;
 s.str("");s<<std::fixed<<std::setprecision(2)<<1.125<<' '<<1.375;
 if(s.str()!="1.12 1.38")return 28;
 int a,b;unsigned long long u;double f;bool truth;
 unsigned before=flushes;
 std::cin>>a>>b>>u>>f>>std::boolalpha>>truth;
 if(!std::cin||a!=42||b!=-17||u!=18446744073709551615ULL||f!=1.25||!truth||flushes<=before)return 9;
 std::string line;std::getline(std::cin,line);if(!line.empty())return 10;
 std::getline(std::cin,line);if(line!="next line")return 11;
 std::getline(std::cin,line);if(!line.empty()||!std::cin)return 12;
 if(std::cin.peek()!=std::char_traits<char>::eof()||!std::cin.eof())return 13;
 std::istringstream in("12x 999999999999999999999999999 -4");int n=0;in>>n;if(n!=12||!in)return 14;
 if(in.peek()!='x'||in.get()!='x')return 15;in.unget();if(in.get()!='x')return 16;
 in>>n;if(!in.fail()||n!=std::numeric_limits<int>::max())return 17;in.clear();in>>n;if(n!=-4)return 18;
 std::stringstream both;both<<"abc";both.seekp(1);both<<'Z';both.seekg(0);both>>line;if(line!="aZc")return 19;
 errno=0;char *end;double d=strtod(" -0x1.8p+2tail",&end);if(d!=-6.0||strcmp(end,"tail"))return 20;
 errno=0;strtoll("9223372036854775808",&end,10);if(errno!=ERANGE)return 21;
 errno=0;if(strtod("1e309",&end)!=std::numeric_limits<double>::infinity()||errno!=ERANGE||*end)return 29;
 if(strtod("0x1p-1074",&end)!=std::numeric_limits<double>::denorm_min()||*end)return 30;
 char text[64];if(snprintf(text,sizeof(text),"%#08x|%-5s|%+.2f",42,"ok",1.25)!=20||strcmp(text,"0x00002a|ok   |+1.25"))return 22;
 char shortbuf[4];if(snprintf(shortbuf,sizeof(shortbuf),"abcdef")!=6||strcmp(shortbuf,"abc"))return 23;
 if(!std::ios::sync_with_stdio(false)||std::ios::sync_with_stdio(true))return 24;
 std::cout<<"Done\n";std::clog<<"Log";std::cerr<<"Error";
 if(!std::cout||!std::cerr||!std::clog)return 25;
 return 0;
}
