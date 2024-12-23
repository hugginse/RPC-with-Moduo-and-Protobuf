#include "mprpcapplication.h"
#include <iostream>
#include <unistd.h>
#include <string>

MprpcConfig MprpcApplication::m_config;

void ShowArgsHelp()
{
    std::cout << "format: command -i <configfile>" << std::endl;
}

void MprpcApplication::Init(int argc, char **argv) 
{
    if (argc < 2)
    {
        ShowArgsHelp();
        exit(EXIT_FAILURE);
    }

    int c = 0; 
    std::string confile_file;
    while ((c = getopt(argc, argv, "i:")) != -1)
    {
        switch(c)
        {
            case 'i':
                confile_file = optarg;
                break;
            case '?':
                ShowArgsHelp();
                exit(EXIT_FAILURE);
            case ':':
                ShowArgsHelp();
                exit(EXIT_FAILURE);
            default:
                break;
        }
    }

    // 开始加载配置文件 rpcserver_ip= repserver_port=  zookeeper= zookeeper_port=
    m_config.LoadConfigFile(confile_file.c_str());

    /*
    std::cout << "rpcserverip: " << m_config.Load("rpcserverip") << std::endl;
    std::cout << "rpcserverport: " << m_config.Load("rpcserverport") << std::endl;
    std::cout << "zookeeperip: " << m_config.Load("zookeeperip") << std::endl;
    std::cout << "zookeeperport: " << m_config.Load("zookeeperport") << std::endl;
    */
}

MprpcApplication& MprpcApplication::GetInstance()
{
    static MprpcApplication app;
    return app;
}

MprpcConfig& MprpcApplication::getConfig()
{
    return m_config;
}