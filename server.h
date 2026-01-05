#pragma once
#include "Logger.h"
#include "Memory_Pool.h"
#include "Mysql_Pool.h"
#include "Reactor.h"
#include "Thread_Pool.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// 服务器配置结构
struct ServerConfig {
    // 内存池配置
    size_t max_memory = 1024 * 1024 * 1024; // 最大内存（1GB）
    bool enable_memory_pool_tls = true;     // 启用内存池TLS

    // MySQL连接池配置
    std::string mysql_host = "localhost"; // MySQL主机地址
    int mysql_port = 3306;                // MySQL端口号
    std::string mysql_user;               // MySQL用户名
    std::string mysql_password;           // MySQL密码
    std::string mysql_db_name;            // MySQL数据库名
    size_t mysql_min_connections = 5;     // MySQL最小连接数
    size_t mysql_max_connections = 20;    // MySQL最大连接数
    // 线程池配置
    size_t thread_pool_min_threads = 5;  // 线程池最小线程数
    size_t thread_pool_max_threads = 20; // 线程池最大线程数
    // 服务器配置
    std::string server_name = "WebServer"; // 服务器名称
    bool enable_statistics = true;         // 启用统计
    // HTTP服务器配置
    int http_port = 8080;              // HTTP服务器端口
    std::string http_host = "0.0.0.0"; // 监听地址
    int listen_backlog = 128;          // 监听队列长度
    std::string doc_root = "wwwroot";  // 静态资源根目录
};

// 服务器统计信息
struct ServerStats {
    std::atomic<long long> total_requests{0};         // 总请求数
    std::atomic<long long> completed_requests{0};     // 完成请求数
    std::atomic<long long> failed_requests{0};        // 失败请求数
    std::atomic<bool> is_running{false};              // 服务器是否运行中
    std::chrono::steady_clock::time_point start_time; // 启动时间
};

// HTTP请求结构
struct HttpRequest {
    std::string method;                         // 请求方法
    std::string path;                           // 请求路径
    std::string route;                          // 请求路由
    std::string query_string;                   // 请求查询字符串
    std::map<std::string, std::string> headers; // 请求头
    std::string body;                           // 请求体
    std::map<std::string, std::string> params;  // 请求参数
};

// HTTP响应结构
struct HttpResponse {
    int status_code = 200;                                        // 响应状态码
    std::string status_text = "OK";                               // 响应状态文本
    std::string content_type = "application/json; charset=utf-8"; // 响应内容类型
    std::string body;                                             // 响应体
};

class Server {
  private:
    ServerConfig config_; // 服务器配置
    ServerStats stats_;   // 服务器统计信息

    // 核心组件
    std::unique_ptr<Memory_Pool> memory_pool_; // 内存池
    std::unique_ptr<Mysql_Pool> mysql_pool_;   // MySQL连接池
    std::unique_ptr<Thread_Pool> thread_pool_; // 线程池

    // HTTP服务器相关
    std::atomic<bool> shutdown_{false}; // 服务器是否关闭
    int server_socket_{-1};             // 服务器套接字
    std::thread http_server_thread_;    // HTTP服务器线程
    std::unique_ptr<Reactor> reactor_;  // 反应器

    // 连接超时配置
    std::chrono::seconds connection_timeout_{300}; // 连接超时时间（5分钟）
    std::thread timeout_checker_thread_;           // 连接超时检查线程

    // 初始化组件
    void initialize_components() { // 初始化组件
        try {
            // 初始化Reactor
            reactor_ = std::make_unique<Reactor>();

            // 初始化内存池
            memory_pool_ = std::make_unique<Memory_Pool>(
                config_.max_memory, config_.enable_memory_pool_tls, 8);

            // 初始化MySQL连接池
            if (!config_.mysql_user.empty() && !config_.mysql_db_name.empty()) {
                mysql_pool_ = std::make_unique<Mysql_Pool>(
                    config_.mysql_host, config_.mysql_port, config_.mysql_user,
                    config_.mysql_password, config_.mysql_db_name,
                    config_.mysql_min_connections, config_.mysql_max_connections);
                init_database();
            }

            // 初始化线程池
            thread_pool_ = std::make_unique<Thread_Pool>(
                config_.thread_pool_min_threads, config_.thread_pool_max_threads);

        } catch (const std::exception &e) {
            Logger::error("初始化组件失败: " + std::string(e.what()));
            throw;
        }
    }

    // 初始化数据库表
    void init_database() {
        if (!mysql_pool_) {
            return;
        }
        try {
            Mysql_Connection_RAII conn_raii = get_mysql_connection();
            MYSQL *conn = conn_raii.get_conn();
            const char *create_table_sql = R"(
                CREATE TABLE IF NOT EXISTS users (
                    id INT AUTO_INCREMENT PRIMARY KEY,
                    username VARCHAR(50) UNIQUE NOT NULL,
                    password_hash VARCHAR(64) NOT NULL,
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                    INDEX idx_username (username)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
            )";

            if (mysql_query(conn, create_table_sql) != 0) {
                Logger::error("创建用户表失败: " + std::string(mysql_error(conn)));
            } else {
                Logger::info("数据库表初始化成功");
            }
        } catch (const std::exception &e) {
            Logger::error("数据库初始化错误: " + std::string(e.what()));
        }
    }

    // SHA256哈希函数
    std::string sha256(const std::string &str) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256_ctx;
        SHA256_Init(&sha256_ctx);
        SHA256_Update(&sha256_ctx, str.c_str(), str.length());
        SHA256_Final(hash, &sha256_ctx);

        std::ostringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    // URL解码
    std::string url_decode(const std::string &str) {
        std::string result;
        result.reserve(str.length());

        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '%' && i + 2 < str.length()) {
                int value = 0;
                std::istringstream is(str.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    result += static_cast<char>(value);
                    i += 2;
                } else {
                    result += str[i];
                }
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }

    // 读取文件到字符串（用于静态资源）
    static bool read_file_all(const std::string &file_path, std::string &out) {
        std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        out = ss.str();
        return true;
    }

    // 简单的 MIME 推断
    static std::string guess_mime_type(const std::string &path) {
        auto pos = path.find_last_of('.');
        std::string ext = (pos == std::string::npos) ? "" : path.substr(pos + 1);
        for (auto &c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == "html" || ext == "htm")
            return "text/html; charset=utf-8";
        if (ext == "css")
            return "text/css; charset=utf-8";
        if (ext == "js")
            return "application/javascript; charset=utf-8";
        if (ext == "json")
            return "application/json; charset=utf-8";
        if (ext == "png")
            return "image/png";
        if (ext == "jpg" || ext == "jpeg")
            return "image/jpeg";
        if (ext == "gif")
            return "image/gif";
        if (ext == "svg")
            return "image/svg+xml";
        if (ext == "ico")
            return "image/x-icon";
        return "application/octet-stream";
    }

    // 静态文件响应（防止 .. 穿越）
    bool try_serve_static(const std::string &route, HttpResponse &response) const {
        if (route.empty()) {
            return false;
        }
        if (route.find("..") != std::string::npos) {
            return false;
        }

        // 处理路径：如果以 /wwwroot/ 开头，去掉这个前缀
        std::string rel = route;
        if (rel.find("/wwwroot/") == 0) {
            rel = rel.substr(8); // 去掉 "/wwwroot"
        }
        if (rel.empty() || rel == "/") {
            rel = "/index.html";
        }

        std::string file_path = config_.doc_root + rel;

        std::string body;
        if (!read_file_all(file_path, body)) {
            return false;
        }
        response.status_code = 200;
        response.status_text = "OK";
        response.content_type = guess_mime_type(file_path);
        response.body = std::move(body);
        return true;
    }

    // 解析URL参数
    std::map<std::string, std::string> parse_url_params(const std::string &query) {
        std::map<std::string, std::string> params;
        if (query.empty()) {
            return params;
        }

        std::string::size_type pos = 0;
        std::string::size_type start = 0;

        while ((pos = query.find('&', start)) != std::string::npos || start < query.length()) {
            std::string::size_type end = (pos == std::string::npos) ? query.length() : pos;
            std::string pair = query.substr(start, end - start);

            std::string::size_type eq_pos = pair.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = url_decode(pair.substr(0, eq_pos));
                std::string value = url_decode(pair.substr(eq_pos + 1));
                params[key] = value;
            }

            start = end + 1;
        }

        return params;
    }

    // 解析HTTP请求（使用内存池）
    bool parse_http_request(const char *buffer, size_t buffer_len, HttpRequest &request) {
        if (!buffer || buffer_len == 0) {
            return false;
        }

        // 解析请求行
        const char *line_end = strstr(buffer, "\r\n");
        if (!line_end) {
            return false;
        }

        std::string request_line(buffer, line_end - buffer);
        std::istringstream iss(request_line);

        if (!(iss >> request.method >> request.path)) {
            return false;
        }

        // 移除路径末尾的斜杠（除了根路径）
        while (request.path.length() > 1 && request.path.back() == '/') {
            request.path.pop_back();
        }

        // 解析路径和查询字符串
        size_t query_pos = request.path.find('?');
        if (query_pos != std::string::npos) {
            request.route = request.path.substr(0, query_pos);
            request.query_string = request.path.substr(query_pos + 1);
        } else {
            request.route = request.path;
        }

        // 解析Content-Length
        size_t content_length = 0;
        const char *cl_header = strstr(buffer, "Content-Length:");
        if (cl_header) {
            content_length = std::strtoul(cl_header + 15, nullptr, 10);
        }

        // 解析请求体
        const char *body_start = strstr(buffer, "\r\n\r\n");
        if (body_start && content_length > 0) {
            body_start += 4;
            size_t available = buffer_len - (body_start - buffer);
            size_t to_read = std::min(content_length, available);
            request.body.assign(body_start, to_read);
        }

        // 解析参数
        if (!request.query_string.empty()) {
            request.params = parse_url_params(request.query_string);
        }
        if (!request.body.empty() && request.method == "POST") {
            auto body_params = parse_url_params(request.body);
            request.params.insert(body_params.begin(), body_params.end());
        }

        return true;
    }

    // 用户注册
    std::string register_user(const std::string &username, const std::string &password) {
        if (username.empty() || password.empty()) {
            return R"({"success": false, "message": "用户名和密码不能为空"})";
        }

        if (!mysql_pool_) {
            return R"({"success": false, "message": "数据库未初始化"})";
        }

        try {
            Mysql_Connection_RAII conn_raii = get_mysql_connection();
            MYSQL *conn = conn_raii.get_conn();

            // SQL转义
            std::vector<char> escaped_username(username.length() * 2 + 1);
            mysql_real_escape_string(conn, escaped_username.data(), username.c_str(), username.length());

            // 检查用户名是否已存在
            std::string check_sql = "SELECT id FROM users WHERE username = '" +
                                    std::string(escaped_username.data()) + "' LIMIT 1";

            if (mysql_query(conn, check_sql.c_str()) == 0) {
                MYSQL_RES *result = mysql_store_result(conn);
                if (result && mysql_num_rows(result) > 0) {
                    mysql_free_result(result);
                    return R"({"success": false, "message": "用户名已存在"})";
                }
                if (result) {
                    mysql_free_result(result);
                }
            }

            // 插入新用户
            std::string password_hash = sha256(password);
            std::string insert_sql = "INSERT INTO users (username, password_hash) VALUES ('" +
                                     std::string(escaped_username.data()) + "', '" + password_hash + "')";

            if (mysql_query(conn, insert_sql.c_str()) == 0) {
                return R"({"success": true, "message": "注册成功"})";
            } else {
                return R"({"success": false, "message": "注册失败: )" +
                       std::string(mysql_error(conn)) + "\"}";
            }
        } catch (const std::exception &e) {
            return R"({"success": false, "message": "注册失败: )" +
                   std::string(e.what()) + "\"}";
        }
    }

    // 用户登录验证
    std::string login_user(const std::string &username, const std::string &password) {
        if (username.empty() || password.empty()) {
            return R"({"success": false, "message": "用户名和密码不能为空"})";
        }

        if (!mysql_pool_) {
            return R"({"success": false, "message": "数据库未初始化"})";
        }

        try {
            Mysql_Connection_RAII conn_raii = get_mysql_connection();
            MYSQL *conn = conn_raii.get_conn();

            std::string password_hash = sha256(password);
            std::vector<char> escaped_username(username.length() * 2 + 1);
            mysql_real_escape_string(conn, escaped_username.data(), username.c_str(), username.length());

            std::string query_sql = "SELECT id FROM users WHERE username = '" +
                                    std::string(escaped_username.data()) +
                                    "' AND password_hash = '" + password_hash + "' LIMIT 1";

            if (mysql_query(conn, query_sql.c_str()) == 0) {
                MYSQL_RES *result = mysql_store_result(conn);
                if (result && mysql_num_rows(result) > 0) {
                    mysql_free_result(result);
                    return R"({"success": true, "message": "登录成功"})";
                }
                if (result) {
                    mysql_free_result(result);
                }
                return R"({"success": false, "message": "用户名或密码错误"})";
            } else {
                return R"({"success": false, "message": "登录失败: )" +
                       std::string(mysql_error(conn)) + "\"}";
            }
        } catch (const std::exception &e) {
            return R"({"success": false, "message": "登录失败: )" +
                   std::string(e.what()) + "\"}";
        }
    }

    // 修改密码
    std::string change_password(const std::string &username, const std::string &old_password,
                                const std::string &new_password) {
        if (username.empty() || old_password.empty() || new_password.empty()) {
            return R"({"success": false, "message": "参数不能为空"})";
        }

        if (!mysql_pool_) {
            return R"({"success": false, "message": "数据库未初始化"})";
        }

        try {
            Mysql_Connection_RAII conn_raii = get_mysql_connection();
            MYSQL *conn = conn_raii.get_conn();

            // 验证旧密码
            std::string old_password_hash = sha256(old_password);
            std::vector<char> escaped_username(username.length() * 2 + 1);
            mysql_real_escape_string(conn, escaped_username.data(), username.c_str(), username.length());

            std::string check_sql = "SELECT id FROM users WHERE username = '" +
                                    std::string(escaped_username.data()) +
                                    "' AND password_hash = '" + old_password_hash + "' LIMIT 1";

            if (mysql_query(conn, check_sql.c_str()) == 0) {
                MYSQL_RES *result = mysql_store_result(conn);
                if (!result || mysql_num_rows(result) == 0) {
                    if (result) {
                        mysql_free_result(result);
                    }
                    return R"({"success": false, "message": "用户名或旧密码错误"})";
                }
                mysql_free_result(result);
            } else {
                return R"({"success": false, "message": "验证失败: )" +
                       std::string(mysql_error(conn)) + "\"}";
            }

            // 更新密码
            std::string new_password_hash = sha256(new_password);
            std::string update_sql = "UPDATE users SET password_hash = '" + new_password_hash +
                                     "' WHERE username = '" + std::string(escaped_username.data()) + "'";

            if (mysql_query(conn, update_sql.c_str()) == 0) {
                if (mysql_affected_rows(conn) > 0) {
                    return R"({"success": true, "message": "密码修改成功"})";
                } else {
                    return R"({"success": false, "message": "密码修改失败"})";
                }
            } else {
                return R"({"success": false, "message": "密码修改失败: )" +
                       std::string(mysql_error(conn)) + "\"}";
            }
        } catch (const std::exception &e) {
            return R"({"success": false, "message": "密码修改失败: )" +
                   std::string(e.what()) + "\"}";
        }
    }

    // 处理路由
    HttpResponse handle_route(const HttpRequest &request) {
        HttpResponse response;

        // POST请求处理
        if (request.method == "POST") {
            if (request.route == "/register") {
                std::string username = request.params.count("username") ? request.params.at("username") : "";
                std::string password = request.params.count("password") ? request.params.at("password") : "";
                response.body = register_user(username, password);
            } else if (request.route == "/login") {
                std::string username = request.params.count("username") ? request.params.at("username") : "";
                std::string password = request.params.count("password") ? request.params.at("password") : "";
                response.body = login_user(username, password);
            } else if (request.route == "/change_password") {
                std::string username = request.params.count("username") ? request.params.at("username") : "";
                std::string old_password = request.params.count("old_password") ? request.params.at("old_password") : "";
                std::string new_password = request.params.count("new_password") ? request.params.at("new_password") : "";
                response.body = change_password(username, old_password, new_password);
            } else {
                // POST请求但路由不匹配
                response.status_code = 404;
                response.status_text = "Not Found";
                std::ostringstream debug_info;
                debug_info << R"({"success": false, "message": "404 Not Found", )";
                debug_info << R"("method": ")" << request.method << R"(", )";
                debug_info << R"("path": ")" << request.path << R"(", )";
                debug_info << R"("route": ")" << request.route << R"("})";
                response.body = debug_info.str();
            }
        }
        // GET请求处理
        else if (request.method == "GET") {
            // 浏览器通常会自动请求 favicon.ico；不提供文件时返回 204，避免“看起来像网站404”
            if (request.route == "/favicon.ico") {
                response.status_code = 204;
                response.status_text = "No Content";
                response.content_type = "image/x-icon";
                response.body.clear();
            }
            // 优先尝试提供静态文件（若不存在再回退到内置HTML）
            else if (!request.route.empty() && try_serve_static(request.route, response)) {
                // served
            } else if (request.route == "/" || request.route == "/index.html" || request.route.empty()) {
                response.content_type = "text/html; charset=utf-8";
                response.body = get_index_html();
            } else {
                // GET请求但路由不匹配
                response.status_code = 404;
                response.status_text = "Not Found";
                std::ostringstream debug_info;
                debug_info << R"({"success": false, "message": "404 Not Found", )";
                debug_info << R"("method": ")" << request.method << R"(", )";
                debug_info << R"("path": ")" << request.path << R"(", )";
                debug_info << R"("route": ")" << request.route << R"("})";
                response.body = debug_info.str();
            }
        }
        // 其他HTTP方法
        else {
            response.status_code = 405;
            response.status_text = "Method Not Allowed";
            response.body = R"({"success": false, "message": "Method Not Allowed"})";
        }

        return response;
    }

    // 获取首页HTML
    std::string get_index_html() const {
        return R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>用户管理系统</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }
        button { background: #007bff; color: white; padding: 10px 20px; border: none; cursor: pointer; border-radius: 4px; }
        button:hover { background: #0056b3; }
        .result { margin-top: 20px; padding: 10px; border-radius: 5px; }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        h1 { color: #333; }
        h2 { color: #555; margin-top: 30px; }
    </style>
</head>
<body>
    <h1>用户管理系统</h1>
    
    <h2>用户注册</h2>
    <form id="registerForm">
        <div class="form-group">
            <label>用户名:</label>
            <input type="text" name="username" required>
        </div>
        <div class="form-group">
            <label>密码:</label>
            <input type="password" name="password" required>
        </div>
        <button type="submit">注册</button>
    </form>
    <div id="registerResult"></div>
    
    <h2>用户登录</h2>
    <form id="loginForm">
        <div class="form-group">
            <label>用户名:</label>
            <input type="text" name="username" required>
        </div>
        <div class="form-group">
            <label>密码:</label>
            <input type="password" name="password" required>
        </div>
        <button type="submit">登录</button>
    </form>
    <div id="loginResult"></div>
    
    <h2>修改密码</h2>
    <form id="changePasswordForm">
        <div class="form-group">
            <label>用户名:</label>
            <input type="text" name="username" required>
        </div>
        <div class="form-group">
            <label>旧密码:</label>
            <input type="password" name="old_password" required>
        </div>
        <div class="form-group">
            <label>新密码:</label>
            <input type="password" name="new_password" required>
        </div>
        <button type="submit">修改密码</button>
    </form>
    <div id="changePasswordResult"></div>
    
    <script>
        function handleForm(formId, endpoint, resultId) {
            document.getElementById(formId).addEventListener('submit', async (e) => {
                e.preventDefault();
                const formData = new FormData(e.target);
                const params = new URLSearchParams();
                for (const [key, value] of formData.entries()) {
                    params.append(key, value);
                }
                
                try {
                    const response = await fetch(endpoint, {
                        method: 'POST',
                        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                        body: params.toString()
                    });
                    const data = await response.json();
                    const resultDiv = document.getElementById(resultId);
                    resultDiv.className = 'result ' + (data.success ? 'success' : 'error');
                    resultDiv.textContent = data.message;
                } catch (error) {
                    const resultDiv = document.getElementById(resultId);
                    resultDiv.className = 'result error';
                    resultDiv.textContent = '请求失败: ' + error.message;
                }
            });
        }
        
        handleForm('registerForm', '/register', 'registerResult');
        handleForm('loginForm', '/login', 'loginResult');
        handleForm('changePasswordForm', '/change_password', 'changePasswordResult');
    </script>
</body>
</html>)";
    }

    // 发送HTTP响应（使用内存池）
    bool send_response(int client_socket, const HttpResponse &response) {
        if (!memory_pool_) {
            return false;
        }

        // 分配响应头缓冲区
        const size_t header_size = 512;
        void *header_buffer = memory_pool_->allocate(header_size);
        if (!header_buffer) {
            return false;
        }

        char *header_ptr = static_cast<char *>(header_buffer);
        int header_len = snprintf(header_ptr, header_size,
                                  "HTTP/1.1 %d %s\r\n"
                                  "Content-Type: %s\r\n"
                                  "Content-Length: %zu\r\n"
                                  "Access-Control-Allow-Origin: *\r\n"
                                  "Connection: close\r\n"
                                  "\r\n",
                                  response.status_code, response.status_text.c_str(),
                                  response.content_type.c_str(), response.body.length());

        // 发送响应头
        ssize_t sent = send(client_socket, header_ptr, header_len, 0);
        memory_pool_->deallocate(header_buffer);

        if (sent < 0) {
            return false;
        }

        // 发送响应体
        if (!response.body.empty()) {
            sent = send(client_socket, response.body.c_str(), response.body.length(), 0);
            if (sent < 0) {
                return false;
            }
        }

        return true;
    }

    // 非阻塞读取数据
    ssize_t read_nonblocking(int fd, std::string &buffer, size_t max_size = 8192) {
        char temp_buffer[4096];
        ssize_t total_read = 0;

        while (total_read < static_cast<ssize_t>(max_size)) {
            ssize_t bytes_read = recv(fd, temp_buffer, sizeof(temp_buffer), 0);

            if (bytes_read > 0) {
                buffer.append(temp_buffer, bytes_read);
                total_read += bytes_read;
            } else if (bytes_read == 0) {
                // 连接关闭
                return total_read > 0 ? total_read : -1;
            } else {
                // 错误处理
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 没有更多数据可读
                    return total_read;
                } else if (errno == EINTR) {
                    // 被信号中断，继续读取
                    continue;
                } else {
                    // 其他错误
                    return -1;
                }
            }
        }

        return total_read;
    }

    // 非阻塞写入数据
    ssize_t write_nonblocking(int fd, const std::string &data) {
        size_t total_sent = 0;
        const char *ptr = data.data();
        size_t remaining = data.size();

        while (total_sent < data.size()) {
            ssize_t bytes_sent = send(fd, ptr + total_sent, remaining, 0);

            if (bytes_sent > 0) {
                total_sent += bytes_sent;
                remaining -= bytes_sent;
            } else if (bytes_sent == 0) {
                // 连接关闭
                return -1;
            } else {
                // 错误处理
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 缓冲区满，需要等待可写事件
                    return total_sent;
                } else if (errno == EINTR) {
                    // 被信号中断，继续写入
                    continue;
                } else {
                    // 其他错误
                    return -1;
                }
            }
        }

        return total_sent;
    }

    // 处理客户端连接事件
    void handle_client_event(int client_fd, uint32_t events) {
        auto conn = reactor_->get_connection(client_fd);
        if (!conn) {
            return;
        }
        // 处理读事件
        if (events & EPOLLIN) {
            std::string temp_buffer;
            ssize_t bytes_read = read_nonblocking(client_fd, temp_buffer);

            if (bytes_read > 0) {
                conn->read_buffer.append(temp_buffer);
                // 尝试解析HTTP请求
                HttpRequest request;
                if (parse_http_request(conn->read_buffer.data(), conn->read_buffer.size(), request)) {
                    // 请求解析成功，处理请求
                    stats_.total_requests++;
                    if (config_.enable_statistics) {
                        Logger::info("请求: " + request.method + " " + request.path +
                                     " -> route: " + request.route);
                    }
                    // 使用线程池处理业务逻辑
                    thread_pool_->submit([this, client_fd, request]() {
                        HttpResponse response = handle_route(request);
                        // 构建响应字符串
                        std::ostringstream response_stream;
                        response_stream << "HTTP/1.1 " << response.status_code << " "
                                        << response.status_text << "\r\n";
                        response_stream << "Content-Type: " << response.content_type << "\r\n";
                        response_stream << "Content-Length: " << response.body.length() << "\r\n";
                        response_stream << "Access-Control-Allow-Origin: *\r\n";
                        response_stream << "Connection: close\r\n";
                        response_stream << "\r\n";
                        response_stream << response.body;

                        // 将响应添加到连接的写缓冲区
                        auto conn = reactor_->get_connection(client_fd);
                        if (conn) {
                            conn->write_buffer = response_stream.str();
                            conn->state = ConnectionState::WRITING;

                            // 注册写事件（LT模式）
                            reactor_->update_connection(client_fd, EPOLLOUT);

                            // 尝试立即写入
                            handle_write_event(client_fd);
                        }
                    });

                    // 清空读缓冲区
                    conn->read_buffer.clear();
                } else if (conn->read_buffer.size() > 65536) {
                    // 请求过大，关闭连接
                    close_connection(client_fd);
                    stats_.failed_requests++;
                }
            } else if (bytes_read == 0 || bytes_read < 0) {
                // 连接关闭或错误
                close_connection(client_fd);
            }
        }

        // 处理写事件
        if (events & EPOLLOUT) {
            handle_write_event(client_fd);
        }
    }

    // 处理写事件
    void handle_write_event(int client_fd) {
        auto conn = reactor_->get_connection(client_fd);
        if (!conn || conn->write_buffer.empty()) {
            return;
        }

        ssize_t bytes_sent = write_nonblocking(client_fd, conn->write_buffer);

        if (bytes_sent > 0) {
            conn->write_buffer.erase(0, bytes_sent);

            if (conn->write_buffer.empty()) {
                // 所有数据已发送，关闭连接
                stats_.completed_requests++;
                close_connection(client_fd);
            }
        } else if (bytes_sent < 0) {
            // 发送错误，关闭连接
            stats_.failed_requests++;
            close_connection(client_fd);
        }
        // bytes_sent == 0 表示需要等待，保持写事件注册
    }

    // 关闭连接
    void close_connection(int client_fd) {
        reactor_->remove_connection(client_fd);
    }

    // 处理新连接
    void handle_new_connection() {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        while (true) {
            int client_fd = accept(server_socket_, (struct sockaddr *)&client_addr, &addr_len);

            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 没有更多连接
                    break;
                } else if (errno == EINTR) {
                    // 被信号中断，继续
                    continue;
                } else {
                    // 其他错误
                    break;
                }
            }

            // 设置TCP选项
            int opt = 1;
            setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            // 注册到Reactor
            if (reactor_->register_connection(client_fd,
                                              [this](int fd, uint32_t events) {
                                                  handle_client_event(fd, events);
                                              })) {
                // 连接注册成功
            } else {
                close(client_fd);
            }
        }
    }

    // 检查连接超时
    void check_connection_timeout() {
        while (!shutdown_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));

            auto now = std::chrono::steady_clock::now();
            std::vector<int> timeout_connections;

            for (auto &conn : reactor_->get_all_connections()) {
                if (now - conn->last_active_time > connection_timeout_) {
                    timeout_connections.push_back(conn->fd);
                }
            }
        }
    }

    // HTTP服务器主循环（基于Reactor事件驱动）
    void http_server_loop() {
        struct sockaddr_in address;
        int opt = 1;

        // 创建socket
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ < 0) {
            Logger::error("Socket创建失败: " + std::string(strerror(errno)));
            return;
        }

        // 设置socket选项
        if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            Logger::error("设置SO_REUSEADDR失败: " + std::string(strerror(errno)));
            close(server_socket_);
            server_socket_ = -1;
            return;
        }

        // 设置非阻塞
        int flags = fcntl(server_socket_, F_GETFL, 0);
        if (flags < 0 || fcntl(server_socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
            Logger::error("设置非阻塞失败: " + std::string(strerror(errno)));
            close(server_socket_);
            server_socket_ = -1;
            return;
        }

        // 配置地址
        address.sin_family = AF_INET;
        if (config_.http_host == "0.0.0.0" || config_.http_host.empty()) {
            address.sin_addr.s_addr = INADDR_ANY;
        } else {
            address.sin_addr.s_addr = inet_addr(config_.http_host.c_str());
            if (address.sin_addr.s_addr == INADDR_NONE) {
                Logger::error("无效的IP地址: " + config_.http_host);
                close(server_socket_);
                server_socket_ = -1;
                return;
            }
        }
        address.sin_port = htons(config_.http_port);

        // 绑定地址
        if (bind(server_socket_, (struct sockaddr *)&address, sizeof(address)) < 0) {
            Logger::error("绑定地址失败: " + std::string(strerror(errno)));
            close(server_socket_);
            server_socket_ = -1;
            return;
        }

        // 监听
        if (listen(server_socket_, config_.listen_backlog) < 0) {
            Logger::error("监听失败: " + std::string(strerror(errno)));
            close(server_socket_);
            server_socket_ = -1;
            return;
        }

        // 注册服务器socket到Reactor
        if (!reactor_->register_connection(server_socket_,
                                           [this](int fd, uint32_t events) {
                                               if (events & EPOLLIN) {
                                                   handle_new_connection();
                                               }
                                           })) {
            Logger::error("注册服务器socket失败");
            close(server_socket_);
            server_socket_ = -1;
            return;
        }

        Logger::info("HTTP服务器启动（Reactor事件驱动），监听 " +
                     config_.http_host + ":" + std::to_string(config_.http_port));

        // 启动Reactor事件循环
        reactor_->start();
        reactor_->event_loop(100); // 100ms超时

        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }
    }

  public:
    // 构造函数
    Server() : shutdown_(false), server_socket_(-1) {
        stats_.start_time = std::chrono::steady_clock::now();
    }

    // 带配置的构造函数
    Server(const ServerConfig &config) : config_(config), shutdown_(false), server_socket_(-1) {
        stats_.start_time = std::chrono::steady_clock::now();
        initialize_components();
    }

    // 析构函数
    ~Server() {
        shutdown();
    }

    // 禁止拷贝和移动
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    Server(Server &&) = delete;
    Server &operator=(Server &&) = delete;

    // 配置MySQL连接
    void configure_mysql(const std::string &host, int port, const std::string &user,
                         const std::string &password, const std::string &db_name,
                         size_t min_connections = 5, size_t max_connections = 20) {
        config_.mysql_host = host;
        config_.mysql_port = port;
        config_.mysql_user = user;
        config_.mysql_password = password;
        config_.mysql_db_name = db_name;
        config_.mysql_min_connections = min_connections;
        config_.mysql_max_connections = max_connections;

        mysql_pool_.reset();
        mysql_pool_ = std::make_unique<Mysql_Pool>(
            host, port, user, password, db_name, min_connections, max_connections);
        init_database();
    }

    // 启动服务器
    void start() {
        if (stats_.is_running.load()) {
            return;
        }

        if (!memory_pool_) {
            initialize_components();
        }

        stats_.is_running = true;
        stats_.start_time = std::chrono::steady_clock::now();

        http_server_thread_ = std::thread(&Server::http_server_loop, this);

        // 启动超时检查线程
        timeout_checker_thread_ = std::thread(&Server::check_connection_timeout, this);
    }

    // 关闭服务器
    void shutdown() {
        if (!stats_.is_running.load()) {
            return;
        }

        shutdown_ = true;
        stats_.is_running = false;

        // 停止Reactor
        if (reactor_) {
            reactor_->stop();
        }

        // 关闭socket以中断accept
        if (server_socket_ >= 0) {
            close(server_socket_);
        }

        // 等待HTTP服务器线程结束
        if (http_server_thread_.joinable()) {
            http_server_thread_.join();
        }

        // 等待超时检查线程结束
        if (timeout_checker_thread_.joinable()) {
            timeout_checker_thread_.join();
        }

        // 等待所有任务完成
        if (thread_pool_) {
            thread_pool_->wait_for_idle();
        }

        // 清理组件
        reactor_.reset();
        thread_pool_.reset();
        mysql_pool_.reset();
        memory_pool_.reset();
    }

    // 获取MySQL连接（RAII封装）
    Mysql_Connection_RAII get_mysql_connection() {
        if (!mysql_pool_) {
            throw std::runtime_error("MySQL pool not initialized");
        }
        return Mysql_Connection_RAII(*mysql_pool_);
    }

    // 分配内存
    void *allocate_memory(size_t size) {
        if (!memory_pool_) {
            throw std::runtime_error("Memory pool not initialized");
        }
        return memory_pool_->allocate(size);
    }

    // 释放内存
    void deallocate_memory(void *ptr) {
        if (!memory_pool_) {
            throw std::runtime_error("Memory pool not initialized");
        }
        memory_pool_->deallocate(ptr);
    }

    // 使用RAII分配内存
    Memory_Pool_RAII allocate_memory_raii(size_t size) {
        if (!memory_pool_) {
            throw std::runtime_error("Memory pool not initialized");
        }
        return Memory_Pool_RAII(*memory_pool_, size);
    }

    // 获取服务器统计信息
    std::string get_stats() const {
        std::ostringstream result;
        result << "=== " << config_.server_name << " Statistics ===\n\n";

        // 服务器基本信息
        result << "Server Status:\n";
        result << "  Running: " << (stats_.is_running.load() ? "Yes" : "No") << "\n";
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - stats_.start_time);
        result << "  Uptime: " << uptime.count() << " seconds\n\n";

        // 请求统计
        result << "Request Statistics:\n";
        result << "  Total Requests: " << stats_.total_requests.load() << "\n";
        result << "  Completed: " << stats_.completed_requests.load() << "\n";
        result << "  Failed: " << stats_.failed_requests.load() << "\n\n";

        // 组件统计
        if (reactor_) {
            result << "Reactor:\n";
            result << "  Active Connections: " << reactor_->get_connection_count() << "\n\n";
        }
        if (memory_pool_) {
            result << "Memory Pool:\n"
                   << memory_pool_->get_stats() << "\n";
        }
        if (thread_pool_) {
            result << "Thread Pool:\n"
                   << thread_pool_->get_stats() << "\n";
        }
        if (mysql_pool_) {
            result << "MySQL Pool:\n"
                   << mysql_pool_->get_stats() << "\n";
        }

        return result.str();
    }

    // 获取配置
    ServerConfig get_config() const { return config_; }

    // 检查是否运行中
    bool is_running() const { return stats_.is_running.load(); }

    // 获取组件指针
    Memory_Pool *get_memory_pool() const { return memory_pool_.get(); }
    Mysql_Pool *get_mysql_pool() const { return mysql_pool_.get(); }
    Thread_Pool *get_thread_pool() const { return thread_pool_.get(); }
};
