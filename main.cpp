#include "Logger.h"
#include "server.h"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

// 全局服务器指针，用于信号处理
static Server *g_server = nullptr;

// 信号处理函数
void signal_handler(int signal) {
    if (g_server && (signal == SIGINT || signal == SIGTERM)) {
        Logger::info("收到停止信号，正在关闭服务器...");
        g_server->shutdown();
    }
}

int main() {
    try {
        // 配置服务器
        ServerConfig config;

        // 内存池配置
        config.max_memory = 1024 * 1024 * 1024; // 1GB
        config.enable_memory_pool_tls = true;
        // MySQL连接池配置（从环境变量读取）
        config.mysql_host = "localhost";
        config.mysql_port = 3306;
        config.mysql_user = "root";
        config.mysql_password = "guiyuan98.";
        config.mysql_db_name = "webserver";
        config.mysql_min_connections = 10;
        config.mysql_max_connections = 50;
        // 线程池配置
        config.thread_pool_min_threads = 10;
        config.thread_pool_max_threads = 50;

        // 服务器配置
        config.server_name = "WebServer";
        config.enable_statistics = true;

        // HTTP服务器配置
        config.http_port = 8080;
        config.http_host = "0.0.0.0"; // 监听所有网络接口
        config.listen_backlog = 1024;

        // 设置日志级别
        Logger::set_min_level(LogLevel::INFO);

        // 检查必要的配置
        if (config.mysql_password.empty()) {
            Logger::warn("MySQL密码未设置，请通过环境变量 MYSQL_PASSWORD 设置");
        }

        // 创建服务器实例
        Server server(config);
        g_server = &server;

        // 注册信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 启动服务器
        server.start();

        Logger::info("========================================");
        Logger::info("服务器已启动");
        Logger::info("访问地址: http://localhost:" + std::to_string(config.http_port));
        Logger::info("按 Ctrl+C 停止服务器");
        Logger::info("========================================");

        // 保持服务器运行
        while (server.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        Logger::info("服务器已关闭");
        return 0;

    } catch (const std::exception &e) {
        Logger::error("服务器启动失败: " + std::string(e.what()));
        return 1;
    }
}
