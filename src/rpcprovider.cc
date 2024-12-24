#include "rpcprovider.h"
#include <google/protobuf/descriptor.h>
#include "rpcheader.pb.h"
#include "logger.h"

/*
service_name => service描述
                        => service* 记录服务对象
                        method_name => method方法对象
json  protobuf
*/
// 框架提供给外部服务的，可以发布rpc方法的函数接口
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;

    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();

    // 获取服务名字
    std::string service_name = pserviceDesc->name();

    // 获取服务对象的方法的数量
    int methodCnt = pserviceDesc->method_count();

    // std::cout << "serviec_name: " << service_name << std::endl;
    LOG_INFO("service_name: %s", service_name.c_str());

    for (int i = 0; i < methodCnt; ++i)
    {
        // 获取了服务对象指定下标的服务方法的描述（抽象描述）  UserService  Login
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_method_Map.insert({method_name, pmethodDesc});

        // std::cout << "method_name: " << method_name << std::endl; 
        LOG_INFO("method_name: %s", method_name.c_str());
    }

    service_info.m_service = service;
    m_serviceMap.insert({service_name, service_info});
}

// 启动rpc服务节点，开始提供rpc远程网络调用服务远程
void RpcProvider::Run()
{
    // 读取配置文件rpcserver的信息
    std::string ip = MprpcApplication::GetInstance().getConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().getConfig().Load("rpcserverport").c_str());
    muduo::net::InetAddress address(ip, port);

    // 创建Tcpserver对象
    muduo::net::TcpServer server(&m_eventLoop, address, "RpcProvider");

    // 绑定连接回调和消息读写回调方法       分离了网络代码和业务代码
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1,
                                        std::placeholders::_2, std::placeholders::_3));

    // 设置muduo库的线程数量
    server.setThreadNum(4);

    std::cout << "RpcProvider start service at ip: " << ip << " port: " << port << std::endl;
    // LOG_INFO("RpcProvider start service at ip: %s port: %s", ip.c_str(), std::to_string(port).c_str());

    // 启动网路服务
    server.start();
    m_eventLoop.loop();
}

// 新的socket连接回调
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        // 和rpc client的连接断开
        conn->shutdown();
    }
}

/*
在框架内部，RpcProvider和RpcConsumer协商好通信用的protobuf数据类型
service_name method_name args       定义protobuf的message类型，进行数的序列化和反序列化
                                    service_name  method_name args_size
UsersServiceLoginzhang san123456

header_size(4个字节) + header_str + args_str
10      "10"
10000   "10000"
std::string insert和copy方法
*/
// 已建立连接用户的读写事件回调 如果远程有一个rpc服务的调用请求，那么OnMessage方法就会响应
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp time)
{
    // 网络上接收的远程rpc调用请求字符流   Login args
    std::string recv_buf = buffer->retrieveAllAsString();

    // 从字符流中读取前4个字节的内容
    uint32_t header_size = 0;
    recv_buf.copy((char *)&header_size, 4, 0);

    // 根据header_size读取数据头的原始字符流, 反序列化数据，得到rpc请求的详细信息
    std::string rpc_header_str = recv_buf.substr(4, header_size);
    mprpc::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    uint32_t args_size;
    if (rpcHeader.ParseFromString(rpc_header_str))
    {
        // 数据头反序列化成功
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }
    else
    {
        // 数据头反序列化失败
        // std::cout << "rpc_header_str: " << rpc_header_str << " parse error!" << std::endl;
        LOG_ERR("rpc_header_str: %s parse error!", rpc_header_str.c_str());
        return;
    }

    // 获取rpc方法参数的字符流数据
    std::string args_str = recv_buf.substr(4 + header_size, args_size);

    // 打印调试信息
    std::cout << "==========================" << std::endl;
    std::cout << "head_size: " << header_size << std::endl;
    std::cout << "rpc_head_str: " << rpc_header_str << std::endl;
    std::cout << "service_name: " << service_name << std::endl;
    std::cout << "method_name: " << method_name << std::endl;
    std::cout << "args_str: " << args_str << std::endl;
    std::cout << "==========================" << std::endl;

    // 获取service对象喝method对象
    auto it = m_serviceMap.find(service_name);
    if (it == m_serviceMap.end())
    {
        // std::cout << service_name << " is not exits!" << std::endl;
        LOG_ERR("%s is not exits!", service_name.c_str());
        return;
    }

    auto mit = it->second.m_method_Map.find(method_name);
    if (mit == it->second.m_method_Map.end())
    {
        // std::cout << method_name << " is not exits!" << std::endl;
        LOG_ERR("%s is not exits!", method_name.c_str());
        return;
    }

    google::protobuf::Service *service = it->second.m_service;      // 获取service对象  new UserService
    const google::protobuf::MethodDescriptor *method = mit->second; // 获取method对象

    // 生成rpc方法调用的请求request和响应response参数
    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str))
    {
        // std::cout << "request parse error, content: " << args_str << std::endl;
        LOG_ERR("request parse error, content: %s", args_str.c_str());
        return;
    }
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    // 给下面的method方法的调用，绑定一个Closure的回调函数
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider,
                                                                    const muduo::net::TcpConnectionPtr &,
                                                                    google::protobuf::Message *>(this,
                                                                                                 &RpcProvider::sendRpcResponse,
                                                                                                 conn, response);
    /*template <typename Class, typename Arg1, typename Arg2>
    inline Closure* NewCallback(Class* object, void (Class::*method)(Arg1, Arg2),
                            Arg1 arg1, Arg2 arg2)
    muduo::net::TcpConnectionPtr
    */

    // 在框架上根据远端rpc请求，调用当前rpc节点上发布的方法
    // new UserService().Login(controller, request, response, done)
    service->CallMethod(method, nullptr, request, response, done);
}

// Closure的回调操作, 用于序列化rpc的响应和网络发送
void RpcProvider::sendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
    std::string response_str;
    if (response->SerializeToString(&response_str))     // response进行序列化
    {
        // 序列化成功后，通过网络把rpc方法执行的结果发送给rpc的调用方
        conn->send(response_str);
        conn->shutdown();       // 模拟http的短链接服务，由rpcprovider主动断开连接
    }
    else
    {
        // std::cout << "serialize response_str error!" << std::endl;
        LOG_ERR("serialize response_str error!");
    }
    conn->shutdown();           // 模拟http的短链接服务，由rpcprovider主动断开连接

}