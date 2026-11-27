#pragma once
#include <overwolf_hook/abi/events.h>
#include <windows.h>
#include <cstring>
namespace overwolf_hook::controller {
/** Recording format is independent of the live C ABI: 24-byte record header
 * followed by 64,784 bytes (capture clock/channel/length + original raw bytes). */
inline bool write_capture_record(HANDLE file,const owh_event&event)noexcept{
 struct Record{uint64_t sequence,lost_before;uint32_t size,reserved;};
 struct Frame{uint64_t captured_qpc;uint32_t source_channel,raw_size;unsigned char raw[255+63*1024];};
 static_assert(sizeof(Record)==24&&sizeof(Frame)==64784);
 const Record record{event.sequence,event.lost_before,sizeof(Frame),0};
 Frame frame{};frame.captured_qpc=event.captured_qpc;frame.source_channel=event.source_channel;frame.raw_size=event.raw_size;
 std::memcpy(frame.raw,event.raw,sizeof(frame.raw));DWORD written=0;
 return WriteFile(file,&record,sizeof(record),&written,nullptr)&&written==sizeof(record)&&
 WriteFile(file,&frame,sizeof(frame),&written,nullptr)&&written==sizeof(frame);
}
}
