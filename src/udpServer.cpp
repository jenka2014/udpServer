#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <iostream>
#include <sys/time.h>
#include <thread>
#include <queue>
#include <mutex>
#include <string.h>
#include "date.h"
#include "md5.h"

using namespace std;
using namespace date;

#define DEFAULT_PORT 3500
#define SOCK_BUF_SIZE 5000
#define MAX_DATA_LEN 1600
#define MD5_LEN 16
#define RING_BUF_SIZE 16

mutex m_cout;

void PrintTimeStamp(uint64_t timeStamp);
string Md5ToStr(uint8_t* md5);
string GetDataStr(int16_t* dataBuf, uint16_t count);

struct DataHeader 
{
    uint32_t ID;
    uint64_t TimeStamp;
    uint16_t DataCount;
    uint8_t Md5CheckSum[MD5_LEN];  
};

struct DataPacket
{
    DataHeader Header;
    int16_t Data[MAX_DATA_LEN];
};

void PrintDataPacket(const DataPacket* dataPacket)
{
    cout << "#" << dataPacket->Header.ID << " ";
    PrintTimeStamp(dataPacket->Header.TimeStamp);
    string data_str = GetDataStr((int16_t*)dataPacket->Data, dataPacket->Header.DataCount);
    string md5str = Md5ToStr((uint8_t*)dataPacket->Header.Md5CheckSum);                      //md5 in header
    MD5 md5 = MD5(data_str);
    string md5_verify_str=md5.hexdigest();                                                   //md5 calculated by the server  
    cout << " md5=" << md5str; 
    if(md5str==md5_verify_str)
        cout << " CheckMd5=OK";
    else
        cout << " CheckMd5=FAIL";
}

class ThreadSafeRingBuffer {
private:
    uint32_t m_bufSize;
    std::queue<DataPacket> packetsQueue; 
    mutex m; 
public:
    ThreadSafeRingBuffer(uint32_t bufSize){
        if(bufSize==0)
            bufSize=1;
        m_bufSize=bufSize;           
    }

    DataPacket pop_front(){
        DataPacket front_packet;  //empty packet
        m.lock();
        if( !packetsQueue.empty() ) {
            front_packet = packetsQueue.front();
            packetsQueue.pop();
        }
        m.unlock();
        return front_packet;
    };

    void push_back(DataPacket packet){
        m.lock();
        if(packetsQueue.size()==m_bufSize)  // if queue is overload, remove first element
            packetsQueue.pop();
        packetsQueue.push(packet);
        m.unlock();
    };

    uint32_t size(){
        return packetsQueue.size();  
    }
};

uint64_t GetTimeStamp()
{
    chrono::_V2::system_clock::time_point tp = chrono::system_clock::now();
    uint64_t timeStamp;
    memcpy(&timeStamp, &tp, 8);
    return timeStamp;
}

void PrintTimeStamp(uint64_t timeStamp)
{  
    chrono::_V2::system_clock::time_point tp;
    memcpy(&tp, &timeStamp, 8);
    cout << tp;
}

string Md5ToStr(uint8_t* md5)
{
    char buf[33];
    for (int32_t i=0; i<16; i++)
        sprintf(buf+i*2, "%02x", md5[i]);
    buf[32]=0;
 
    return std::string(buf);
}

string GetDataStr(int16_t* dataBuf, uint16_t count)
{
    string resltStr ="";
    for(int32_t i=0; i<count; i++){
        if(i==count-1)
            resltStr+=std::to_string(dataBuf[i]); 
        else
            resltStr+=std::to_string(dataBuf[i])+",";       
    }
    return resltStr;
}

void ReadDataFromSocket(ThreadSafeRingBuffer* ringBuffer)
{
    int32_t sock = 0;
    struct sockaddr_in addr;
    int8_t sockBuf[SOCK_BUF_SIZE];
    int32_t bytes_read = 0;
  
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0)
    {
        cout << "Error. Create socket\n";
        return;
    }
    memset((int8_t*)&addr, 0, sizeof(addr));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        cout << "Error. Bind socket\n";
        return;
    }
    DataHeader* p_packetHeader = nullptr;
    DataPacket* p_dataPacket = nullptr;
    string md5str;
    string md5_verify_str;
    string data_str;
    uint16_t dataCount=0;
    cout << "UDP server started at port "<< DEFAULT_PORT << ". Waiting for data\n";
    while(1)
    {  
        fflush(stdout);
        bytes_read = recvfrom(sock, sockBuf, SOCK_BUF_SIZE, 0, NULL, NULL);

        if(bytes_read ==-1)
        {
            cout << "Error!\n";
            return;
        } 
        if(bytes_read<(int32_t)sizeof(DataHeader))
            continue;

        p_packetHeader=(DataHeader*)sockBuf;
        dataCount = p_packetHeader->DataCount; 

        data_str="";
        if(dataCount>0){
            p_dataPacket=(DataPacket*)sockBuf;
            if(dataCount>MAX_DATA_LEN){
                dataCount = MAX_DATA_LEN;
                data_str=" (DataCount>"+std::to_string(MAX_DATA_LEN) + " Read only " + std::to_string(MAX_DATA_LEN)+" elements)";      
            }
            data_str+=GetDataStr(p_dataPacket->Data, dataCount);
        }else
            data_str=" No data";
    
        //verify md5_checksum
        MD5 md5 = MD5(data_str);
        md5_verify_str=md5.hexdigest();

        m_cout.lock();
        cout << "Received:  #" << p_packetHeader->ID <<" ";
        PrintTimeStamp(p_packetHeader->TimeStamp);
        md5str=Md5ToStr(p_packetHeader->Md5CheckSum);
        cout << " md5=" << md5str;
        if(md5str==md5_verify_str)
            cout << " CheckMd5=OK";
        else
            cout << " CheckMd5=FAIL";
        m_cout.unlock();   

        ringBuffer->push_back(*p_dataPacket);

        //cout << " data=" << data_str;
        cout << endl;       
    }
        
}

void ProcessingData(ThreadSafeRingBuffer* ringBuffer){
    while(1){
        if(ringBuffer->size()>0){
            DataPacket packet=ringBuffer->pop_front();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            m_cout.lock();
            cout << "Processed: ";
            PrintDataPacket(&packet); 
            cout << endl;
            m_cout.unlock();
        }
    }
}

int main()
{
    ThreadSafeRingBuffer ringBuffer(RING_BUF_SIZE);
    std::thread t1(ReadDataFromSocket,&ringBuffer);
    std::thread t2(ProcessingData, &ringBuffer);
    t1.join();
    t2.join();

    cout << "UdpServer finished\n";
    return 0;
}
