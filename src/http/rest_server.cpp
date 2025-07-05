#include <iostream>
#include <thread>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <future>

#include "rest_server.h"


namespace TakeAwayPlatform
{
    RestServer::RestServer(const std::string& configPath) : threadPool(std::thread::hardware_concurrency()) 
    {
        std::cout << "RestServer starting." << std::endl;
        std::cout.flush();

        Json::Value config = load_config(configPath);

        std::cout << "RestServer load config success." << std::endl;
        std::cout.flush();
        
        // 初始化数据库连接池
        init_db_pool(config["database"]);

        std::cout << "RestServer instance created." << std::endl;
        std::cout.flush();
    }

    RestServer::~RestServer() 
    {
        stop();
    }

    void RestServer::start(int port) 
    {
        if (isRunning) {
            std::cerr << "Server is already running." << std::endl;
            std::cout.flush();
            return;
        }
        
        isRunning = true;
        stopRequested = false;
        
        // 成员变量保存线程
        serverThread = std::thread([this, port] {
            this->run_server(port);
        });
        
        std::cout << "Server starting on port " << port << "..." << std::endl;
        std::cout.flush();
    }

    void RestServer::run_server(int port) 
    {
        try 
        {
            // 设置路由
            setup_routes();
            
            std::cout << "HTTP server listening on port " << port << std::endl;
            std::cout.flush();

            if (!server.listen("0.0.0.0", port)) {
                std::cerr << "Failed to start server on port " << port << std::endl;
            }
            
            std::cout << "HTTP server exited listen loop." << std::endl;
        } 
        catch (const std::exception& e) 
        {
            std::cerr << "Server error in worker thread: " << e.what() << std::endl;
            std::cout.flush();
        }
        
        // 服务器已停止，更新状态
        isRunning = false;
        
        // 通知等待的线程
        stopCv.notify_one();
        std::cout << "Server worker thread exiting." << std::endl;
        std::cout.flush();
    }

    void RestServer::stop() 
    {
        if (!isRunning) {
            std::cout << "Server already stop." << std::endl;
            std::cout.flush();
            return;
        }
        
        std::cout << "Requesting server stop..." << std::endl;
        std::cout.flush();
        stopRequested = true;
        
        // 通知服务器停止
        server.stop();
        
        // 等待服务器实际停止
        std::unique_lock<std::mutex> lock(stopMtx);
        if (stopCv.wait_for(lock, std::chrono::seconds(5), [this] {
            return !isRunning;
        }))
        {
            if (serverThread.joinable()) 
            {
                serverThread.join();
            }

            std::cout << "Server stopped successfully." << std::endl;
            std::cout.flush();
        } 
        else 
        {
            std::cerr << "Warning: Server did not stop within timeout." << std::endl;
            std::cout.flush();

            if (serverThread.joinable()) 
            {
                serverThread.detach(); // 最后手段，避免死锁
            }
        }

        // 清理数据库连接池
        std::lock_guard<std::mutex> dbLock(dbPoolMutex);
        while (!dbPool.empty()) {
            dbPool.pop();
        }
    }

    bool RestServer::is_running() const 
    { 
        return isRunning;
    }

    void RestServer::init_db_pool(const Json::Value& config) 
    {
        std::cout << "host: " << config["host"].asString() << std::endl;
        std::cout << "port: " << config["port"].asInt() << std::endl;
        std::cout << "user: " << config["user"].asString() << std::endl;
        std::cout << "password: " << config["password"].asString() << std::endl;
        std::cout << "name: " << config["name"].asString() << std::endl;
        std::cout.flush();
        
        int pool_size = config.get("pool_size", 10).asInt();

        dbConfig.push_back({
            config["host"].asString(),
            config["port"].asInt(),
            config["user"].asString(),
            config["password"].asString(),
            config["name"].asString()
        });

        for (int index = 0; index < pool_size; ++index) {
            dbPool.push(create_db_handler());
        }
    }

    std::unique_ptr<DatabaseHandler> RestServer::create_db_handler() 
    {
        return std::make_unique<DatabaseHandler>(dbConfig[0]);
    }

    std::unique_ptr<DatabaseHandler> RestServer::acquire_db_handler() 
    {
        std::lock_guard<std::mutex> lock(dbPoolMutex);
        if (dbPool.empty()) {
            return create_db_handler();
        }
        
        auto handler = std::move(dbPool.front());
        dbPool.pop();
        return handler;
    }

    void RestServer::release_db_handler(std::unique_ptr<DatabaseHandler> handler) 
    {
        std::lock_guard<std::mutex> lock(dbPoolMutex);
        dbPool.push(std::move(handler));
    }

    void RestServer::setup_routes() 
    {
        // 首页测试接口
        server.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("TakeAwayPlatform is running!", "text/plain");
        });
        
        //健康检查接口
        server.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
            if (this->is_running() && !this->stopRequested) {
                res.set_content("OK", "text/plain");
            } else {
                res.set_content("SHUTTING_DOWN", "text/plain");
                res.status = 503; // Service Unavailable
            }
        });

        // 示例路由：获取所有菜品（使用线程池处理）
        server.Get("/menu", [&](const httplib::Request&, httplib::Response& res) 
        {
            threadPool.enqueue([this, &res] {
                auto db_handler = acquire_db_handler();
                Json::Value menu = db_handler->query("SELECT * FROM DISH");
                release_db_handler(std::move(db_handler));
                
                res.set_content(menu.toStyledString(), "application/json");
            });
        });

        // 示例路由：创建订单（使用线程池处理）
        server.Post("/order", [&](const httplib::Request& req, httplib::Response& res) 
        {
            threadPool.enqueue([this, req, &res] {
                Json::Value order = parse_json(req.body);
                auto db_handler = acquire_db_handler();
                
                // 验证订单数据...
                // 插入数据库...
                
                release_db_handler(std::move(db_handler));
                res.set_content("{\"status\":\"created\"}", "application/json");
            });
        });
    
        // ====================== 商家接口 ======================
        // ====================== 商家接口 ======================
    
    

        
        // ✅✅ 商家添加菜品接口：插入 DISH 表 ✅✅
        server.Post("/merchant/add_item", [&](const httplib::Request& req, httplib::Response& res) 
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    Json::Value item = parse_json(req.body);

                    // ✅ 提取字段
                    std::string name = item["name"].asString();
                    double price = item["price"].asDouble();
                    std::string desc = item.get("description", "").asString();
                    std::string merchantId = item["merchantId"].asString();
                    std::string categoryId = item["categoryId"].asString();
                    std::string imageUrl = item.get("imageUrl", "").asString();
                    int stock = item.get("stock", 0).asInt();
                    int sales = item.get("sales", 0).asInt();
                    double rating = item.get("rating", 0.0).asDouble();
                    int isOnSale = item.get("isOnSale", 1).asInt();

                    std::cout << "/merchant/add_item name: " << name << std::endl;
                    std::cout << "/merchant/add_item price: " << price << std::endl;
                    std::cout << "/merchant/add_item desc: " << desc << std::endl;
                    std::cout << "/merchant/add_item merchantId: " << merchantId << std::endl;
                    std::cout << "/merchant/add_item categoryId: " << categoryId << std::endl;
                    std::cout << "/merchant/add_item imageUrl: " << imageUrl << std::endl;
                    std::cout << "/merchant/add_item stock: " << stock << std::endl;
                    std::cout << "/merchant/add_item sales: " << sales << std::endl;
                    std::cout << "/merchant/add_item rating: " << rating << std::endl;
                    std::cout << "/merchant/add_item isOnSale: " << isOnSale << std::endl;
                    std::cout.flush();

                    auto db_handler = acquire_db_handler();

                    // ✅ 构造 SQL 插入语句（使用 UUID 生成 dishId）
                    std::string sql = "INSERT INTO DISH "
                        "(dishId, merchantId, categoryId, name, description, price, imageUrl, stock, sales, rating, isOnSale) "
                        "VALUES (UUID(), '" + merchantId + "', '" + categoryId + "', '" + name + "', '" + desc + "', " +
                        std::to_string(price) + ", '" + imageUrl + "', " + std::to_string(stock) + ", " +
                        std::to_string(sales) + ", " + std::to_string(rating) + ", " + std::to_string(isOnSale) + ")";

                    db_handler->query(sql);
                    release_db_handler(std::move(db_handler));

                    res.set_content("{\"status\":\"success\"}", "application/json");
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
        });
// 添加商家的接口      
server.Post("/merchant/add", [&](const httplib::Request& req, httplib::Response& res)
{
    std::cout << "/merchant/add request body: " << req.body << std::endl;

    Json::Value merchant = parse_json(req.body);

    // ✅ 从 JSON 中读取所有字段
    const std::string name = merchant["name"].asString();
    const std::string address = merchant["address"].asString();
    const std::string phone = merchant["phoneNumber"].asString();
    const std::string desc = merchant.get("description", "").asString();
    const std::string logo = merchant.get("logoUrl", "").asString();
    const bool isOpen = merchant.get("isOpen", false).asBool();
    const std::string status = merchant.get("status", "pending").asString();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[添加商家] name: " << name << std::endl;

            auto db = acquire_db_handler();
            const std::string merchantId = generate_uuid();

            std::ostringstream sql;
            sql << "INSERT INTO MERCHANT "
                << "(merchantId, name, description, address, phoneNumber, logoUrl, isOpen, status) "
                << "VALUES ('" << merchantId << "', '"
                << name << "', '" 
                << desc << "', '" 
                << address << "', '"
                << phone << "', '" 
                << logo << "', "
                << (isOpen ? "1" : "0") << ", '" 
                << status << "')";

            db->query(sql.str());
            release_db_handler(std::move(db));

            // ✅ 返回插入内容
            Json::Value insertedMerchant;
            insertedMerchant["merchantId"] = merchantId;
            insertedMerchant["name"] = name;
            insertedMerchant["description"] = desc;
            insertedMerchant["address"] = address;
            insertedMerchant["phoneNumber"] = phone;
            insertedMerchant["logoUrl"] = logo;
            insertedMerchant["isOpen"] = isOpen;
            insertedMerchant["status"] = status;

            response["status"] = "success";
            response["message"] = "商家添加成功！";
            response["merchant"] = insertedMerchant;

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});
        
//添加菜品分类
 server.Post("/merchant/add_category", [&](const httplib::Request& req, httplib::Response& res)
{
    std::cout << "/merchant/add_category request body: " << req.body << std::endl;

    Json::Value category = parse_json(req.body);

    // ✅ 从 JSON 中读取所有字段
    const std::string categoryId = category["categoryId"].asString();
    const std::string merchantId = category["merchantId"].asString();
    const std::string categoryName = category["categoryName"].asString();
    const int sortOrder = category["sortOrder"].asInt();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[添加分类] categoryId: " << categoryId << std::endl;
            std::cout << "[添加分类] merchantId: " << merchantId << std::endl;
            std::cout << "[添加分类] categoryName: " << categoryName << std::endl;
            std::cout << "[添加分类] sortOrder: " << sortOrder << std::endl;

            auto db = acquire_db_handler();

            std::ostringstream sql;
            sql << "INSERT INTO DISH_CATEGORY "
                << "(categoryId, merchantId, categoryName, sortOrder) "
                << "VALUES ('" << categoryId << "', '"
                << merchantId << "', '"
                << categoryName << "', "
                << sortOrder << ")";

            db->query(sql.str());
            release_db_handler(std::move(db));

            // ✅ 返回插入内容
            Json::Value insertedCategory;
            insertedCategory["categoryId"] = categoryId;
            insertedCategory["merchantId"] = merchantId;
            insertedCategory["categoryName"] = categoryName;
            insertedCategory["sortOrder"] = sortOrder;

            response["status"] = "success";
            response["message"] = "分类添加成功！";
            response["category"] = insertedCategory;

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});
// 添加菜品
  server.Post("/merchant/add_dish", [&](const httplib::Request& req, httplib::Response& res)
{
    std::cout << "/merchant/add_dish request body: " << req.body << std::endl;

    Json::Value dish = parse_json(req.body);

    // ✅ 从 JSON 中读取所有字段（前端需全部提供）
    const std::string dishId = dish["dishId"].asString();
    const std::string merchantId = dish["merchantId"].asString();
    const std::string categoryId = dish["categoryId"].asString();
    const std::string name = dish["name"].asString();
    const std::string description = dish["description"].asString();
    const double price = dish["price"].asDouble();
    const std::string imageUrl = dish["imageUrl"].asString();
    const int stock = dish["stock"].asInt();
    const int sales = dish["sales"].asInt();
    const double rating = dish["rating"].asDouble();
    const bool isOnSale = dish["isOnSale"].asBool();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[添加菜品] dishId: " << dishId << std::endl;

            auto db = acquire_db_handler();

            std::ostringstream sql;
            sql << "INSERT INTO DISH (dishId, merchantId, categoryId, name, description, price, imageUrl, stock, sales, rating, isOnSale) "
                << "VALUES ('" << dishId << "', '" << merchantId << "', '" << categoryId << "', '"
                << name << "', '" << description << "', " << price << ", '" << imageUrl << "', "
                << stock << ", " << sales << ", " << rating << ", " << (isOnSale ? "1" : "0") << ")";

            db->query(sql.str());
            release_db_handler(std::move(db));

            // ✅ 返回插入内容
            Json::Value insertedDish;
            insertedDish["dishId"] = dishId;
            insertedDish["merchantId"] = merchantId;
            insertedDish["categoryId"] = categoryId;
            insertedDish["name"] = name;
            insertedDish["description"] = description;
            insertedDish["price"] = price;
            insertedDish["imageUrl"] = imageUrl;
            insertedDish["stock"] = stock;
            insertedDish["sales"] = sales;
            insertedDish["rating"] = rating;
            insertedDish["isOnSale"] = isOnSale;

            response["status"] = "success";
            response["message"] = "菜品添加成功！";
            response["dish"] = insertedDish;

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});
 //用户注册    
 server.Post("/user/register", [&](const httplib::Request& req, httplib::Response& res)
{
    std::cout << "/user/register request body: " << req.body << std::endl;

    Json::Value user = parse_json(req.body);

    // ✅ 从 JSON 中读取所有字段
    const std::string userId = user["userId"].asString();
    const std::string username = user["username"].asString();
    const std::string passwordHash = user["passwordHash"].asString();
    const std::string email = user.get("email", "").asString();
    const std::string phoneNumber = user.get("phoneNumber", "").asString();
    const std::string status = user.get("status", "active").asString();
    const std::string avatarUrl = user.get("avatarUrl", "").asString();
    const std::string gender = user.get("gender", "").asString();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[用户注册] userId: " << userId << std::endl;
            std::cout << "[用户注册] username: " << username << std::endl;

            auto db = acquire_db_handler();

            std::ostringstream sql;
            sql << "INSERT INTO USER (userId, username, passwordHash, email, phoneNumber, status, avatarUrl, gender) "
                << "VALUES ('" << userId << "', '"
                << username << "', '"
                << passwordHash << "', '"
                << email << "', '"
                << phoneNumber << "', '"
                << status << "', '"
                << avatarUrl << "', '"
                << gender << "')";

            db->query(sql.str());
            release_db_handler(std::move(db));

            // ✅ 返回插入内容（注意：不返回敏感信息如passwordHash）
            Json::Value insertedUser;
            insertedUser["userId"] = userId;
            insertedUser["username"] = username;
            insertedUser["email"] = email;
            insertedUser["phoneNumber"] = phoneNumber;
            insertedUser["status"] = status;
            insertedUser["avatarUrl"] = avatarUrl;
            insertedUser["gender"] = gender;

            response["status"] = "success";
            response["message"] = "用户注册成功！";
            response["user"] = insertedUser;

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});

        //用户登录接口       
 server.Post("/merchant/login_user", [&](const httplib::Request& req, httplib::Response& res)
{
    std::cout << "/merchant/login_user request body: " << req.body << std::endl;

    Json::Value loginReq = parse_json(req.body);

    const std::string userId = loginReq["userId"].asString();
    const std::string username = loginReq["username"].asString();
    const std::string passwordHash = loginReq["passwordHash"].asString();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[用户登录] userId: " << userId << std::endl;
            std::cout << "[用户登录] username: " << username << std::endl;

            auto db = acquire_db_handler();

            std::ostringstream sql;
            sql << "SELECT userId, username, email, phoneNumber, status, avatarUrl, gender "
                << "FROM USER WHERE userId = '" << userId
                << "' AND username = '" << username
                << "' AND passwordHash = '" << passwordHash << "'";

            std::cout << "[用户登录] 执行查询 SQL: " << sql.str() << std::endl;

            auto result = db->query(sql.str());
            release_db_handler(std::move(db));

            if (!result.empty()) {
                std::cout << "[用户登录] 查询成功：可以登录！" << std::endl;
                
                // 获取第一条记录(应该只有一条)
                auto row = result[0];
                
                // ✅ 返回用户信息(不含敏感信息)
                Json::Value userInfo;
                userInfo["userId"] = row["userId"];
                userInfo["username"] = row["username"];
                userInfo["email"] = row["email"];
                userInfo["phoneNumber"] = row["phoneNumber"];
                userInfo["status"] = row["status"];
                userInfo["avatarUrl"] = row["avatarUrl"];
                userInfo["gender"] = row["gender"];

                response["status"] = "success";
                response["message"] = "登录成功";
                response["user"] = userInfo;
                
                // 可以添加token或session信息
                // response["token"] = generate_auth_token(userId);
                
            } else {
                response["status"] = "fail";
                response["message"] = "用户名或密码错误";
            }

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        if (result.find("\"status\":\"fail\"") != std::string::npos) {
            res.status = 401;
        }
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});
    
        // 添加订单接口
server.Post("/order/create", [&](const httplib::Request& req, httplib::Response& res) {
    try {
        std::cout << "/order/create request body: " << req.body << std::endl;

        // 解析JSON
        Json::Value order;
        Json::CharReaderBuilder reader;
        std::string errors;
        std::istringstream jsonStream(req.body);
        
        if (!Json::parseFromStream(reader, jsonStream, &order, &errors)) {
            throw std::runtime_error("JSON解析错误: " + errors);
        }

        // 生成订单ID（如果未提供）
        const std::string orderId = order.get("orderId", generate_uuid()).asString();
        const std::string userId = order["userId"].asString();
        const std::string merchantId = order["merchantId"].asString();
        const std::string addressId = order["addressId"].asString();
        const std::string remark = order.get("remark", "").asString();
        double totalPrice = order["totalPrice"].asDouble();

        // 生成时间字段
        std::string orderTime = current_time_string();
        std::string paymentTime = orderTime;

        // 预计送达时间 = 当前时间 + 30 分钟
        std::string estimatedDeliveryTime = add_minutes(orderTime, 30);
        
        // 实际送达时间（如果没有提供，使用预计送达时间）
        std::string actualDeliveryTime = order.get("actualDeliveryTime", estimatedDeliveryTime).asString();

        // 日志输出
        std::cout << "[订单接口] 创建订单 - orderId: " << orderId << std::endl;
        std::cout << "[订单接口] userId: " << userId << ", merchantId: " << merchantId << std::endl;
        std::cout << "[订单接口] 总价: " << totalPrice << ", 订单时间: " << orderTime << std::endl;

        auto db = acquire_db_handler();

        // 插入订单主表
        std::ostringstream order_sql;
        order_sql << "INSERT INTO `ORDER` (orderId, userId, merchantId, totalPrice, status, orderTime, paymentTime, "
                << "estimatedDeliveryTime, actualDeliveryTime, addressId, remark) VALUES ('"
                << orderId << "', '" << userId << "', '" << merchantId << "', " << totalPrice
                << ", 'PENDING_PAYMENT', '" << orderTime << "', '" << paymentTime << "', '"
                << estimatedDeliveryTime << "', '" << actualDeliveryTime << "', '"
                << addressId << "', '" << remark << "')";

        db->query(order_sql.str());

        // 插入订单项
        const Json::Value& items = order["items"];
        int itemCount = 0;
        for (const auto& item : items) {
            const std::string orderItemId = generate_uuid();
            const std::string dishId = item["dishId"].asString();
            const std::string dishName = item["dishName"].asString();
            double price = item["price"].asDouble();
            int quantity = item["quantity"].asInt();

            std::ostringstream item_sql;
            item_sql << "INSERT INTO ORDER_ITEM (orderItemId, orderId, dishId, dishName, price, quantity) VALUES ('"
                    << orderItemId << "', '" << orderId << "', '" << dishId << "', '" << dishName << "', "
                    << price << ", " << quantity << ")";
            db->query(item_sql.str());
            itemCount++;
        }
        
        release_db_handler(std::move(db));

        // ========== 构建标准化的JSON响应 ==========
        Json::Value response;
        response["code"] = 200;
        response["message"] = "订单创建成功";
        
        // 添加主要订单信息
        Json::Value orderData;
        orderData["orderId"] = orderId;
        orderData["userId"] = userId;
        orderData["merchantId"] = merchantId;
        orderData["totalPrice"] = totalPrice;
        orderData["orderTime"] = orderTime;
        orderData["paymentTime"] = paymentTime;
        orderData["estimatedDeliveryTime"] = estimatedDeliveryTime;
        orderData["addressId"] = addressId;
        orderData["remark"] = remark;
        orderData["itemCount"] = itemCount;
        
        response["data"] = orderData;

   // 转换为JSON字符串并设置响应
        Json::StreamWriterBuilder writer;
        std::string jsonResponse = Json::writeString(writer, response);
        res.set_content(jsonResponse, "application/json");

    } catch (const std::exception& e) {
        // 错误处理 - 确保返回JSON格式错误信息
        Json::Value errorResponse;
        errorResponse["code"] = 500;
        errorResponse["message"] = "订单创建失败: " + std::string(e.what());
        
        Json::StreamWriterBuilder writer;
        std::string errorJson = Json::writeString(writer, errorResponse);
        res.status = 500;
        res.set_content(errorJson, "application/json");
        
        std::cerr << "[订单接口] 错误: " << e.what() << std::endl;
    }
});

         //用户地址插入接口
         //用户地址插入接口
 server.Post("/merchant/add_user_address", [&](const httplib::Request& req, httplib::Response& res) 
{
    std::cout << "/merchant/add_user_address request body: " << req.body << std::endl;

    Json::Value address = parse_json(req.body);

    const std::string addressId = generate_short_id();
    const std::string userId = address["userId"].asString();
    const std::string recipientName = address["recipientName"].asString();
    const std::string phoneNumber = address["phoneNumber"].asString();
    const std::string fullAddress = address["fullAddress"].asString();
    const int isDefault = address.get("isDefault", 0).asInt();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[用户地址接口] addressId: " << addressId << std::endl;

            auto db = acquire_db_handler();

            std::ostringstream sql;
            sql << "INSERT INTO USER_ADDRESS (addressId, userId, recipientName, phoneNumber, fullAddress, isDefault) "
                << "VALUES ('" << addressId << "', '"
                << userId << "', '"
                << recipientName << "', '"
                << phoneNumber << "', '"
                << fullAddress << "', "
                << isDefault << ")";

            db->query(sql.str());
            release_db_handler(std::move(db));

            // 返回插入内容
            Json::Value insertedAddress;
            insertedAddress["addressId"] = addressId;
            insertedAddress["userId"] = userId;
            insertedAddress["recipientName"] = recipientName;
            insertedAddress["phoneNumber"] = phoneNumber;
            insertedAddress["fullAddress"] = fullAddress;
            insertedAddress["isDefault"] = isDefault;

            response["status"] = "success";
            response["message"] = "地址添加成功！";
            response["address"] = insertedAddress;

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});

  //添加对于菜品评论

server.Post("/comment/add", [&](const httplib::Request& req, httplib::Response& res)
{
    std::cout << "/comment/add request body: " << req.body << std::endl;

    Json::Value comment = parse_json(req.body);

    const std::string commentId = generate_uuid();
    const std::string userId = comment["userId"].asString();
    const std::string dishId = comment.get("dishId", "").asString();
    const int rating = comment.get("rating", 5).asInt();
    const std::string content = comment.get("content", "").asString();

    auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([=]() {
        Json::Value response;

        try {
            std::cout << "[添加评论] commentId: " << commentId << std::endl;

            auto db = acquire_db_handler();

            std::ostringstream sql;
            sql << "INSERT INTO USER_COMMENT "
                << "(commentId, userId, dishId, rating, content, commentTime) "
                << "VALUES ('" << commentId << "', '"
                << userId << "', '"
                << dishId << "', "
                << rating << ", '"
                << content << "', NOW())";

            db->query(sql.str());
            release_db_handler(std::move(db));

            // 返回插入内容
            Json::Value insertedComment;
            insertedComment["commentId"] = commentId;
            insertedComment["userId"] = userId;
            insertedComment["dishId"] = dishId;
            insertedComment["rating"] = rating;
            insertedComment["content"] = content;
            insertedComment["commentTime"] = RestServer::current_time_string();

            response["status"] = "success";
            response["message"] = "评论添加成功！";
            response["comment"] = insertedComment;

        } catch (const std::exception& e) {
            response["status"] = "error";
            response["message"] = e.what();
        }

        return response.toStyledString();
    });

    std::future<std::string> result_future = task_ptr->get_future();
    threadPool.enqueue([task_ptr] { (*task_ptr)(); });

    try {
        std::string result = result_future.get();
        res.set_content(result, "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});

             // 添加管理员接口（重点在管理员信息插入）
              // 添加管理员接口（重点在管理员信息插入）
server.Post("/admin/add_admin", [&](const httplib::Request& req, httplib::Response& res) {
    try {
        std::cout << "/admin/add_admin request body: " << req.body << std::endl;

        // 解析JSON
        Json::Value admin;
        Json::CharReaderBuilder reader;
        std::string errors;
        std::istringstream jsonStream(req.body);
        
        if (!Json::parseFromStream(reader, jsonStream, &admin, &errors)) {
            throw std::runtime_error("JSON解析错误: " + errors);
        }

        // ✅ 自动生成 adminId 和当前时间
        const std::string adminId = generate_admin_id();
        const std::string currentTime = current_time_string();

        // ✅ 解析用户输入字段
        const std::string username = admin["username"].asString();
        const std::string passwordHash = admin["passwordHash"].asString();
        const std::string role = admin.get("role", "operator").asString(); // 默认为 operator

        // ✅ 控制台日志输出
        std::cout << "[管理员接口] 添加管理员 - adminId: " << adminId << std::endl;
        std::cout << "[管理员接口] username: " << username << std::endl;
        std::cout << "[管理员接口] role: " << role << std::endl;

        auto db = acquire_db_handler();

        // ✅ 构造SQL插入语句
        std::ostringstream sql;
        sql << "INSERT INTO ADMIN_USER (adminId, username, passwordHash, role, lastLogin) VALUES ('"
            << adminId << "', '"
            << username << "', '"
            << passwordHash << "', '"
            << role << "', '"
            << currentTime << "')";

        std::cout << "[管理员接口] 执行 SQL: " << sql.str() << std::endl;

        db->query(sql.str());
        release_db_handler(std::move(db));

        // ========== 构建标准化的JSON响应 ==========
        Json::Value response;
        response["code"] = 200;
        response["message"] = "插入成功";
        
        // 添加管理员信息
        Json::Value adminData;
        adminData["adminId"] = adminId;
        adminData["username"] = username;
        adminData["role"] = role;
        adminData["lastLogin"] = currentTime;
        
        response["data"] = adminData;

        // 转换为JSON字符串并设置响应
        Json::StreamWriterBuilder writer;
        std::string jsonResponse = Json::writeString(writer, response);
        res.set_content(jsonResponse, "application/json");

    } catch (const std::exception& e) {
        // 错误处理 - 确保返回JSON格式错误信息
        Json::Value errorResponse;
        errorResponse["code"] = 500;
        errorResponse["message"] = "添加管理员失败: " + std::string(e.what());
        
        Json::StreamWriterBuilder writer;
        std::string errorJson = Json::writeString(writer, errorResponse);
        res.status = 500;
        res.set_content(errorJson, "application/json");
        
        std::cerr << "[管理员接口] 错误: " << e.what() << std::endl;
    }
});

               // 管理员登录接口(关键在于查询)
 // 管理员登录接口
server.Post("/admin/login_admin", [&](const httplib::Request& req, httplib::Response& res) 
{
    try {
        std::cout << "/admin/login_admin request body: " << req.body << std::endl;

        Json::Value loginReq = parse_json(req.body);

        const std::string adminId = loginReq["adminId"].asString();
        const std::string username = loginReq["username"].asString();
        const std::string passwordHash = loginReq["passwordHash"].asString();

        std::cout << "[管理员登录接口] adminId: " << adminId << std::endl;
        std::cout << "[管理员登录接口] username: " << username << std::endl;
        std::cout << "[管理员登录接口] passwordHash: " << passwordHash << std::endl;

        auto db = acquire_db_handler();

        std::ostringstream sql;
        sql << "SELECT * FROM ADMIN_USER WHERE adminId = '" << adminId
            << "' AND username = '" << username
            << "' AND passwordHash = '" << passwordHash << "'";

        std::cout << "[管理员登录接口] 执行查询 SQL: " << sql.str() << std::endl;

        auto result = db->query(sql.str());
        release_db_handler(std::move(db));

        if (!result.empty()) {
            std::cout << "[管理员登录接口] 查询成功：可以登录！" << std::endl;
            
            // 直接构建并返回JSON字符串
            std::string jsonResponse = "{\"status\":\"success\", \"message\":\"" + username + "登录成功\"}";
            res.set_content(jsonResponse, "application/json");
            
        } else {
            std::cout << "[管理员登录接口] 查询失败：未查到对应账号" << std::endl;
            res.status = 401;
            res.set_content("{\"status\":\"fail\", \"message\":\"未查询到对应账号，请检查id/用户名/密码\"}", "application/json");
        }

    } catch (const std::exception& e) {
        std::cout << "[管理员登录接口] 异常错误: " << e.what() << std::endl;
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});

        // 插入商家评价接口
  // 插入商家评价接口（同步版本）
server.Post("/review/create", [&](const httplib::Request& req, httplib::Response& res) {
    try {
        Json::Value review = parse_json(req.body);

        const std::string reviewId = review.get("reviewId", generate_uuid()).asString();
        const std::string userId = review["userId"].asString();
        const std::string merchantId = review["merchantId"].asString();
        int rating = review["rating"].asInt();
        const std::string content = review["content"].asString();

        std::cout << "创建评价 - userId: " << userId << ", merchantId: " << merchantId << std::endl;

        // 使用自定义函数生成当前时间字符串
        std::string reviewTime = RestServer::current_time_string();

        auto db = acquire_db_handler();

        std::ostringstream review_sql;
        review_sql << "INSERT INTO MERCHANT_REVIEW (reviewId, userId, merchantId, rating, content, reviewTime) "
                << "VALUES ('" << reviewId << "', '" << userId << "', '" << merchantId << "', "
                << rating << ", '" << content << "', '" << reviewTime << "')";
        db->query(review_sql.str());

        release_db_handler(std::move(db));

        // ========== 构建标准化的JSON响应 ==========
        Json::Value response;
        response["status"] = "success";
        response["message"] = "评价添加成功";  // 添加成功消息
        
        // 添加评价信息
        Json::Value reviewData;
        reviewData["reviewId"] = reviewId;
        reviewData["userId"] = userId;
        reviewData["merchantId"] = merchantId;
        reviewData["rating"] = rating;
        reviewData["content"] = content;
        reviewData["reviewTime"] = reviewTime;
        
        response["data"] = reviewData;

        Json::StreamWriterBuilder writer;
        res.set_content(Json::writeString(writer, response), "application/json");

    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
    }
});

        // 查看某个商家的评论列表
        server.Get(R"(/merchant/reviews)", [&](const httplib::Request& req, httplib::Response& res) {
            // 拿到路径参数中的 merchantId
            std::cout << "/merchant/reviews request body: " << req.body << std::endl;
            Json::Value requestResult = parse_json(req.body);
            std::string merchantId = requestResult["merchant_id"].asString();
            std::cout << "/merchant/reviews merchantId: " << merchantId << std::endl;

            // 包装任务
            auto task_ptr = std::make_shared<std::packaged_task<std::string()> >([this, merchantId]() {
                std::cout << "[GET] /merchant/" << merchantId << "/reviews" << std::endl;
 
                Json::Value response;
                try {
                    auto db = acquire_db_handler();

                    std::ostringstream sql;
                    sql << "SELECT r.reviewId, r.userId, u.username, r.rating, r.content, r.reviewTime "
                        << "FROM MERCHANT_REVIEW r "
                        << "LEFT JOIN USER u ON r.userId = u.userId "
                        << "WHERE r.merchantId = '" << merchantId << "' "
                        << "ORDER BY r.reviewTime DESC";

                    Json::Value result = db->query(sql.str());
                    release_db_handler(std::move(db));

                    response["status"] = "success";
                    response["merchantId"] = merchantId;
                    response["reviews"] = result;

                } catch (const std::exception& e) {
                    response["status"] = "error";
                    response["message"] = e.what();
                }

                return response.toStyledString();
            });

            // 提交任务
            std::future<std::string> result_future = task_ptr->get_future();
            threadPool.enqueue([task_ptr] {
                (*task_ptr)();
            });

            // 返回响应
            try {
                std::string result = result_future.get();
                std::cout << "/merchant/:id/reviews result: " << result << std::endl;
                res.set_content(result, "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
            }
        });

        // 查看某个商家的菜品列表
        server.Get(R"(/merchant/dishes)", [&](const httplib::Request& req, httplib::Response& res) 
        {
            // 打印请求体
            std::cout << "/merchant/dishes request body: " << req.body << std::endl;

            Json::Value requestJson = parse_json(req.body);
            std::string merchantId = requestJson["merchantId"].asString();
            std::cout << "/merchant/dishes merchantId: " << merchantId << std::endl;

            // 包装异步任务
            auto task_ptr = std::make_shared<std::packaged_task<std::string()> >([this, merchantId]() {
                std::cout << "[GET] /merchant/" << merchantId << "/dishes" << std::endl;

                Json::Value response;

                try {
                    auto db = acquire_db_handler();

                    std::ostringstream sql;
                    sql << "SELECT dishId, merchantId, Name, description, price, imageUrl, categoryId "
                        << "FROM DISH "
                        << "WHERE merchantId = '" << merchantId << "' "
                        << "ORDER BY Name ASC";

                    Json::Value result = db->query(sql.str());
                    release_db_handler(std::move(db));

                    response["status"] = "success";
                    response["merchantId"] = merchantId;
                    response["dishes"] = result;

                } catch (const std::exception& e) {
                    response["status"] = "error";
                    response["message"] = e.what();
                }

                return response.toStyledString();
            });

            // 提交任务
            std::future<std::string> result_future = task_ptr->get_future();
            threadPool.enqueue([task_ptr] {
                (*task_ptr)();
            });

            // 设置返回响应
            try {
                std::string result = result_future.get();
                std::cout << "/merchant/dishes result: " << result << std::endl;
                res.set_content(result, "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
            }
        });


// 配送信息接口（同步版本）
server.Post("/merchant/add_delivery_info", [&](const httplib::Request& req, httplib::Response& res)
{
    try {
        std::cout << "/merchant/add_delivery_info request body: " << req.body << std::endl;

        Json::Value deliveryData = parse_json(req.body);

        // ✅ 自动生成配送ID
        const std::string deliveryId = generate_short_id();
        
        // ✅ 解析必填字段
        const std::string orderId = deliveryData["orderId"].asString();
        const std::string deliveryStatus = deliveryData.get("deliveryStatus", "PENDING_PICKUP").asString();
        
        // ✅ 解析可选字段
        const std::string estimatedDeliveryTime = deliveryData.get("estimatedDeliveryTime", "").asString();
        const std::string actualDeliveryTime = deliveryData.get("actualDeliveryTime", "").asString();
        const std::string deliveryPersonId = deliveryData.get("deliveryPersonId", "").asString();
        const std::string deliveryPersonName = deliveryData.get("deliveryPersonName", "").asString();
        const std::string deliveryPersonPhone = deliveryData.get("deliveryPersonPhone", "").asString();

        // ✅ 控制台日志输出
        std::cout << "[配送信息接口] deliveryId（自动生成）: " << deliveryId << std::endl;
        std::cout << "[配送信息接口] orderId: " << orderId << std::endl;
        std::cout << "[配送信息接口] deliveryStatus: " << deliveryStatus << std::endl;
        std::cout << "[配送信息接口] estimatedDeliveryTime: " << estimatedDeliveryTime << std::endl;
        std::cout << "[配送信息接口] actualDeliveryTime: " << actualDeliveryTime << std::endl;
        std::cout << "[配送信息接口] deliveryPersonId: " << deliveryPersonId << std::endl;
        std::cout << "[配送信息接口] deliveryPersonName: " << deliveryPersonName << std::endl;
        std::cout << "[配送信息接口] deliveryPersonPhone: " << deliveryPersonPhone << std::endl;

        auto db = acquire_db_handler();

        // ✅ 构造SQL插入语句
        std::ostringstream sql;
        sql << "INSERT INTO DELIVERY_INFO ("
            << "deliveryId, orderId, deliveryStatus, estimatedDeliveryTime, "
            << "actualDeliveryTime, deliveryPersonId, deliveryPersonName, deliveryPersonPhone"
            << ") VALUES ('"
            << deliveryId << "', '"
            << orderId << "', '"
            << deliveryStatus << "', "
            << (estimatedDeliveryTime.empty() ? "NULL" : "'" + estimatedDeliveryTime + "'") << ", "
            << (actualDeliveryTime.empty() ? "NULL" : "'" + actualDeliveryTime + "'") << ", "
            << (deliveryPersonId.empty() ? "NULL" : "'" + deliveryPersonId + "'") << ", "
            << (deliveryPersonName.empty() ? "NULL" : "'" + deliveryPersonName + "'") << ", "
            << (deliveryPersonPhone.empty() ? "NULL" : "'" + deliveryPersonPhone + "'") << ")";

        std::cout << "[配送信息接口] 执行 SQL: " << sql.str() << std::endl;

        db->query(sql.str());
        release_db_handler(std::move(db));

        // ========== 构建标准化的JSON响应 ==========
        Json::Value response;
        response["status"] = "success";
        response["message"] = "配送信息插入成功";  // 修改为要求的消息
        
        // 添加配送信息
        Json::Value deliveryInfo;
        deliveryInfo["deliveryId"] = deliveryId;
        deliveryInfo["orderId"] = orderId;
        deliveryInfo["deliveryStatus"] = deliveryStatus;
        if (!estimatedDeliveryTime.empty()) deliveryInfo["estimatedDeliveryTime"] = estimatedDeliveryTime;
        if (!actualDeliveryTime.empty()) deliveryInfo["actualDeliveryTime"] = actualDeliveryTime;
        if (!deliveryPersonId.empty()) deliveryInfo["deliveryPersonId"] = deliveryPersonId;
        if (!deliveryPersonName.empty()) deliveryInfo["deliveryPersonName"] = deliveryPersonName;
        if (!deliveryPersonPhone.empty()) deliveryInfo["deliveryPersonPhone"] = deliveryPersonPhone;
        
        response["data"] = deliveryInfo;
        // ========== 响应构建结束 ==========

        Json::StreamWriterBuilder writer;
        res.set_content(Json::writeString(writer, response), "application/json");

    } catch (const std::exception& e) {
        std::cout << "[配送信息接口] 错误：" << e.what() << std::endl;
        res.status = 500;
        
        // ✅ 错误响应JSON
        Json::Value errorResponse;
        errorResponse["status"] = "error";
        errorResponse["message"] = e.what();
        
        Json::StreamWriterBuilder writer;
        res.set_content(Json::writeString(writer, errorResponse), "application/json");
    }
});

        //支付记录接口
 server.Post("/merchant/add_payment_record", [&](const httplib::Request& req, httplib::Response& res) {
    try {
        // 确保请求体是有效的JSON
        if (req.body.empty()) {
            throw std::runtime_error("请求体为空");
        }

        // 解析JSON
        Json::Value paymentData;
        Json::CharReaderBuilder reader;
        std::string errors;
        std::istringstream jsonStream(req.body);
        
        if (!Json::parseFromStream(reader, jsonStream, &paymentData, &errors)) {
            throw std::runtime_error("JSON解析错误: " + errors);
        }

        // 生成必要数据
        const std::string paymentId = generate_short_id();
        const std::string currentTime = current_time_string();
        
        // 提取字段
        const std::string orderId = paymentData["orderId"].asString();
        const double amount = paymentData["amount"].asDouble();
        const std::string paymentMethod = paymentData["paymentMethod"].asString();
        const std::string transactionId = paymentData.get("transactionId", "").asString();
        const std::string status = paymentData.get("status", "SUCCESS").asString();

        // 日志输出
        std::cout << "[支付记录] 添加记录 - "
                  << "orderId: " << orderId << ", "
                  << "amount: " << amount << ", "
                  << "paymentMethod: " << paymentMethod << ", "
                  << "transactionId: " << transactionId << ", "
                  << "status: " << status << std::endl;

        // 数据库操作
        auto db = acquire_db_handler();
        std::ostringstream sql;
        sql << "INSERT INTO PAYMENT_RECORD (paymentId, orderId, amount, paymentTime, paymentMethod, transactionId, status) "
            << "VALUES ('" << paymentId << "', "
            << "'" << orderId << "', "
            << amount << ", "
            << "'" << currentTime << "', "
            << "'" << paymentMethod << "', "
            << "'" << transactionId << "', "
            << "'" << status << "')";
        
        db->query(sql.str());
        release_db_handler(std::move(db));

        // 构建JSON响应 - 确保这是最后一步
        Json::Value response;
        response["code"] = 200;
        response["message"] = "支付记录添加成功";
        response["data"]["paymentId"] = paymentId;
        response["data"]["orderId"] = orderId;
        response["data"]["amount"] = amount;
        response["data"]["paymentMethod"] = paymentMethod;
        response["data"]["transactionId"] = transactionId;
        response["data"]["status"] = status;
        response["data"]["paymentTime"] = currentTime;

        // 转换为JSON字符串并设置响应
        Json::StreamWriterBuilder writer;
        std::string jsonResponse = Json::writeString(writer, response);
        res.set_content(jsonResponse, "application/json");

    } catch (const std::exception& e) {
        // 错误处理 - 确保返回JSON格式错误信息
        Json::Value errorResponse;
        errorResponse["code"] = 500;
        errorResponse["message"] = "服务器错误: " + std::string(e.what());
        
        Json::StreamWriterBuilder writer;
        std::string errorJson = Json::writeString(writer, errorResponse);
        res.status = 500;
        res.set_content(errorJson, "application/json");
        
        std::cerr << "[支付记录] 错误: " << e.what() << std::endl;
    }
});

        // 按照名字搜索商家
        server.Get("/merchants", [&](const httplib::Request& req, httplib::Response& res) {
            std::string name_keyword = req.get_param_value("name");
    
            if (name_keyword.empty()) {
                Json::Value empty_result(Json::arrayValue);
                res.set_content(empty_result.toStyledString(), "application/json");
                return;
            }

            // 创建异步任务
            auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([this, name_keyword]() {
                auto db_handler = acquire_db_handler();
        
                // 手动转义特殊字符
                std::string escaped;
                escaped.reserve(name_keyword.length() * 2);
                for (char c : name_keyword) {
                    if (c == '\'' || c == '\\') {
                        escaped += '\\';
                    }
                escaped += c;
                }
        
                std::string sql = "SELECT * FROM MERCHANT WHERE name LIKE '%" + escaped + "%'";
                Json::Value merchants = db_handler->query(sql);
                release_db_handler(std::move(db_handler));
        
                std::cout << "Executing SQL: " << sql << std::endl;
                return merchants.toStyledString();
            });

            // 获取future并提交任务
            std::future<std::string> result_future = task_ptr->get_future();
            threadPool.enqueue([task_ptr] {
                (*task_ptr)();
            });

            // 获取结果并设置响应
            try {
                std::string result = result_future.get();
                res.set_content(result, "application/json");
            } catch (const std::exception& e) {
                Json::Value error;
                error["error"] = e.what();
                res.status = 500;
                res.set_content(error.toStyledString(), "application/json");
            }
        });

        // 查询某个用户的所有订单及其订单项
        server.Get(R"(/order/query)", [&](const httplib::Request& req, httplib::Response& res) {
            std::cout << "/order/query request body: " << req.body << std::endl;
            Json::Value requestJson = parse_json(req.body);
            std::string userId = requestJson["userId"].asString();
            std::cout << "[订单查询接口] userId: " << userId << std::endl;

            auto task_ptr = std::make_shared<std::packaged_task<std::string()>>([this, userId]() {
                Json::Value response;

                try {
                    auto db = acquire_db_handler();

                    // 查询订单主信息
                    std::ostringstream order_sql;
                    order_sql << "SELECT * FROM `ORDER` WHERE userId = '" << userId << "' ORDER BY orderTime DESC";
                    Json::Value orders = db->query(order_sql.str());

                    // 遍历每个订单，查询它的订单项
                    for (auto& order : orders) {
                        std::string orderId = order["orderId"].asString();

                        std::ostringstream item_sql;
                        item_sql << "SELECT dishId, dishName, price, quantity "
                                << "FROM ORDER_ITEM WHERE orderId = '" << orderId << "'";
                        Json::Value items = db->query(item_sql.str());

                        order["items"] = items; // 添加订单项到订单中
                    }

                    release_db_handler(std::move(db));

                    response["status"] = "success";
                    response["userId"] = userId;
                    response["orders"] = orders;

                } catch (const std::exception& e) {
                    response["status"] = "error";
                    response["message"] = e.what();
                }

                return response.toStyledString();
            });

            std::future<std::string> result_future = task_ptr->get_future();
            threadPool.enqueue([task_ptr] { (*task_ptr)(); });

            try {
                std::string result = result_future.get();
                res.set_content(result, "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
            }
        });

        //查看菜品评价
       server.Get(R"(/dish/reviews)", [&](const httplib::Request& req, httplib::Response& res) 
        {
            std::cout << "/dish/reviews request body: " << req.body << std::endl;

            Json::Value requestJson = parse_json(req.body);
            std::string dishId = requestJson["dishId"].asString();
            std::cout << "/dish/reviews dishId: " << dishId << std::endl;

            auto task_ptr = std::make_shared<std::packaged_task<std::string()> >([this, dishId]() {
                std::cout << "[GET] /dish/" << dishId << "/reviews" << std::endl;

                Json::Value response;
                try 
                {
                    auto db = acquire_db_handler();

                    std::ostringstream sql;
                    sql << "SELECT r.commentId, r.userId, u.username, r.rating, r.content, "

                        << "DATE_FORMAT(r.commentTime, '%Y-%m-%d %H:%i:%s') AS commentTime "

                        << "FROM USER_COMMENT r "
                        << "LEFT JOIN USER u ON r.userId = u.userId "
                    << "WHERE r.dishId = '" << dishId << "' "
                    << "ORDER BY r.commentTime DESC";

                    Json::Value result = db->query(sql.str());
                    release_db_handler(std::move(db));

                    response["status"] = "success";
                    response["dishId"] = dishId;
                    response["reviews"] = result;
                } 
                catch (const std::exception& e) 
                {
                    response["status"] = "error";
                    response["message"] = e.what();
                }

                return response.toStyledString();
            });

            std::future<std::string> result_future = task_ptr->get_future();
            threadPool.enqueue([task_ptr] 
            {
                (*task_ptr)();
            });

            try 
            {
                std::string result = result_future.get();
                std::cout << "/dish/reviews result: " << result << std::endl;
                res.set_content(result, "application/json");
            } 
            catch (const std::exception& e) 
            {
                res.status = 500;
                res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
            }
        });
 


}

    Json::Value RestServer::parse_json(const std::string& jsonStr) 
    {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream json_stream(jsonStr);
        
        if (!Json::parseFromStream(builder, json_stream, &root, &errors)) 
        {
            throw std::runtime_error("JSON parse error: " + errors);
        }

        return root;
    }

   std::string RestServer::generate_uuid()
    {
        std::stringstream ss;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        const char* hex = "0123456789abcdef";
        std::vector<int> uuid_format = {8, 4, 4, 4, 12}; 

        for (size_t i = 0; i < uuid_format.size(); ++i) {
            for (int j = 0; j < uuid_format[i]; ++j) {
                ss << hex[dis(gen)];
            }
            if (i != uuid_format.size() - 1) ss << "-";
        }

        return ss.str(); // 返回长度为 36 的标准 UUID
    }

    //生成adressid的函数

    std::string RestServer::generate_short_id(int length)
    {
        const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, chars.size() - 1);

        std::string result;
        for (int i = 0; i < length; ++i) {
            result += chars[dist(gen)];
            return result;
        }
    }

        //生成管理员id的函数
    std::string RestServer::generate_admin_id(int length)
     {
        std::stringstream ss;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15); // 16进制字符下标

        const char* hex = "0123456789abcdef";

        for (int i = 0; i < length; ++i) {
            ss << hex[dis(gen)];
        }

        return ss.str(); 
    }

        //自动生成时间戳
    std::string RestServer::current_time_string() 
    {
        time_t rawtime;
        time(&rawtime);
        struct tm* timeinfo = localtime(&rawtime);

        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

        return std::string(buffer);
    }   

    // 在当前时间字符串基础上加N分钟
    std::string RestServer::add_minutes(const std::string& timeStr, int minutes)
    {
        struct tm tm_time = {};
        strptime(timeStr.c_str(), "%Y-%m-%d %H:%M:%S", &tm_time);
        time_t t = mktime(&tm_time);
        t += minutes * 60;

        struct tm* new_time = localtime(&t);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", new_time);

        return std::string(buffer);
    }


}
