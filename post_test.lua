-- POST 请求测试脚本
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.body = "username=testuser&password=testpass123"

