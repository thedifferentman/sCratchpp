// Strong terminal hooks override the base SDK's no-device implementations.
#include "console.hpp"
#include <scratch_io.h>
#include <errno.h>
#include <string.h>
namespace { std::string& pending() { static std::string text; return text; } std::size_t offset=0; }
extern "C" int __scrpp_io_read(unsigned char *bytes,std::size_t count) {
    if(!count)return 0;
    if(offset==pending().size()) {
        pending().clear();offset=0;
        const auto result=scratch::console::read_line_result(pending());
        using S=scratch::console::InputStatus;
        if(result==S::eof)return 0;
        if(result!=S::line){errno=result==S::busy?EBUSY:ECANCELED;return -1;}
        pending().push_back('\n');
    }
    const auto remaining=pending().size()-offset;
    if(count>remaining)count=remaining;
    memcpy(bytes,pending().data()+offset,count);offset+=count;
    return (int)count;
}
extern "C" int __scrpp_io_write(int,const unsigned char *bytes,std::size_t count) {
    scratch::console::write(reinterpret_cast<const char*>(bytes),count);return (int)count;
}
extern "C" int __scrpp_io_flush(int) {scratch::console::flush();return 0;}
