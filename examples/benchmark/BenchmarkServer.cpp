#include <iostream>
#include <mutex>
#include <atomic>
#include <map>

#include <brynet/net/SocketLibFunction.h>
#include <brynet/net/EventLoop.h>
#include <brynet/net/WrapTCPService.h>
#include <brynet/net/ListenThread.h>
#include <brynet/net/Socket.h>
#include <brynet/utils/packet.h>

#include "meta.pb.h"
#include "GayRpcCore.h"
#include "OpPacket.h"
#include "UtilsDataHandler.h"
#include "GayRpcInterceptor.h"
#include "UtilsInterceptor.h"

#include "./pb/benchmark_service.gayrpc.h"

using namespace brynet;
using namespace brynet::net;
using namespace utils_interceptor;
using namespace dodo::benchmark;
using namespace gayrpc::core;

std::atomic<int64_t> count(0);

class MyService : public benchmark_service::EchoServerService
{
public:
    bool echo(const EchoRequest& request, 
        const benchmark_service::EchoReply::PTR& replyObj) override
    {
        EchoResponse response;
        response.set_message(request.message());

        replyObj->reply(response);

        return true;
    }
};

static void counter(const RpcMeta& meta, const google::protobuf::Message& message, const UnaryHandler& next)
{
    count++;
    next(meta, message);
}

static void onConnection(const TCPSession::PTR& session)
{
    auto rpcHandlerManager = std::make_shared<gayrpc::core::RpcTypeHandleManager>();
    session->setDataCallback([rpcHandlerManager](const TCPSession::PTR& session,
        const char* buffer,
        size_t len) {
        return dataHandle(rpcHandlerManager, buffer, len);
    });

    // 入站拦截器
    auto inboundInterceptor = gayrpc::utils::makeInterceptor(withProtectedCall(), counter);

    // 出站拦截器
    auto outBoundInterceptor = gayrpc::utils::makeInterceptor(withSessionSender(std::weak_ptr<TCPSession>(session)));

    // 创建服务对象
    auto rpcServer = std::make_shared<MyService>();
    registerEchoServerService(rpcHandlerManager, rpcServer, inboundInterceptor, outBoundInterceptor);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: <listen port>\n");
        exit(-1);
    }

    auto server = std::make_shared<WrapTcpService>();
    auto listenThread = ListenThread::Create();

    listenThread->startListen(
        false, 
        "0.0.0.0", 
        atoi(argv[1]), 
        [=](TcpSocket::PTR socket){
            socket->SocketNodelay();
            server->addSession(std::move(socket), 
                onConnection, 
                false, 
                nullptr, 
                1024*1024);
        });

    server->startWorkThread(std::thread::hardware_concurrency());

    EventLoop mainLoop;
    std::atomic<int64_t> tmp(0);

    while (true)
    {
        mainLoop.loop(1000);
        std::cout << "count is:" << (count-tmp) << std::endl;
        tmp.store(count);
    }
}
