#include "AC2AccountService.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winhttp.h>
#include "Windows/HideWindowsPlatformTypes.h"
namespace
{
struct FInternet
{
    HINTERNET Handle=nullptr;
    ~FInternet(){if(Handle)WinHttpCloseHandle(Handle);}
};
}
#endif
FAC2AccountReply AC2AccountLogin(const FString& URL,const FString& JSON)
{
    FAC2AccountReply Reply;
#if PLATFORM_WINDOWS
    URL_COMPONENTS Parts={};Parts.dwStructSize=sizeof(Parts);Parts.dwHostNameLength=Parts.dwUrlPathLength=DWORD(-1);
    if(!WinHttpCrackUrl(*URL,URL.Len(),0,&Parts))return Reply;
    const FString Host(Parts.dwHostNameLength,Parts.lpszHostName),Path(Parts.dwUrlPathLength,Parts.lpszUrlPath);
    FInternet Session{WinHttpOpen(L"AC2Unreal/0.11",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!Session.Handle)return Reply;
    WinHttpSetTimeouts(Session.Handle,10000,10000,10000,10000);
    FInternet Connection{WinHttpConnect(Session.Handle,*Host,Parts.nPort,0)};if(!Connection.Handle)return Reply;
    FInternet Request{WinHttpOpenRequest(Connection.Handle,L"POST",*Path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,Parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};
    if(!Request.Handle)return Reply;
    DWORD Policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(Request.Handle,WINHTTP_OPTION_REDIRECT_POLICY,&Policy,sizeof(Policy)))return Reply;
    FTCHARToUTF8 Payload(*JSON);
    if(!WinHttpSendRequest(Request.Handle,L"Content-Type: application/json\r\n",DWORD(-1),const_cast<char*>(Payload.Get()),Payload.Length(),Payload.Length(),0)||!WinHttpReceiveResponse(Request.Handle,nullptr))return Reply;
    DWORD Size=sizeof(Reply.Status);
    if(!WinHttpQueryHeaders(Request.Handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&Reply.Status,&Size,WINHTTP_NO_HEADER_INDEX))return {};
    TArray<uint8> Bytes;
    for(;;)
    {
        DWORD Available=0;if(!WinHttpQueryDataAvailable(Request.Handle,&Available))return {};
        if(!Available)break;if(Available>8192||Bytes.Num()+Available>8192)return {};
        int32 Offset=Bytes.Num();Bytes.AddUninitialized(Available);DWORD Read=0;
        if(!WinHttpReadData(Request.Handle,Bytes.GetData()+Offset,Available,&Read)||Read==0)return {};
        Bytes.SetNum(Offset+Read);
    }
    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),Bytes.Num());Reply.Body=FString(Text.Length(),Text.Get());
#endif
    return Reply;
}
