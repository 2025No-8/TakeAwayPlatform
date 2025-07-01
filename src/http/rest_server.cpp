#include <iostream>
#include <thread>

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
      
        //添加商家接口
        server.Post("/merchant/add", [&](const httplib::Request& req, httplib::Response& res)
         {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/merchant/add request body: " << req.body << std::endl;
                    std::cout.flush();
                    Json::Value merchant = parse_json(req.body);

                    // 必填字段
                    const std::string name = merchant["name"].asString();
                    const std::string address = merchant["address"].asString();
                    const std::string phone = merchant["phoneNumber"].asString();

                    // 可选字段（带默认值）
                    const std::string desc = merchant.get("description", "").asString();
                    const std::string logo = merchant.get("logoUrl", "").asString();
                    const bool isOpen = merchant.get("isOpen", false).asBool();
                    const std::string status = merchant.get("status", "pending").asString();

                    std::cout << "/merchant/add name: " << name << std::endl;
                    std::cout << "/merchant/add address: " << address << std::endl;
                    std::cout << "/merchant/add phone: " << phone << std::endl;
                    std::cout << "/merchant/add desc: " << desc << std::endl;
                    std::cout << "/merchant/add logo: " << logo << std::endl;
                    std::cout << "/merchant/add isOpen: " << isOpen << std::endl;
                    std::cout << "/merchant/add status: " << status << std::endl;

                    auto db = acquire_db_handler();

                    // 构建 SQL 插入语句
                    std::string merchantId = generate_uuid();
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

                    res.set_content("{\"status\":\"success\"}", "application/json");

                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
         });
          // 添加菜品分类接口
        server.Post("/merchant/add_category", [&](const httplib::Request& req, httplib::Response& res)
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/merchant/add_category request body: " << req.body << std::endl;

                    Json::Value category = parse_json(req.body);

                // ✅ 所有字段都从 JSON 中读取
                    const std::string categoryId = category["categoryId"].asString();
                    const std::string merchantId = category["merchantId"].asString();
                    const std::string categoryName = category["categoryName"].asString();
                    const int sortOrder = category["sortOrder"].asInt(); // 这里不再设默认值，必须由你传入

                // ✅ 控制台打印，便于调试
                    std::cout << "[分类接口] categoryId: " << categoryId << std::endl;
                    std::cout << "[分类接口] merchantId: " << merchantId << std::endl;
                    std::cout << "[分类接口] categoryName: " << categoryName << std::endl;
                    std::cout << "[分类接口] sortOrder: " << sortOrder << std::endl;

                    auto db = acquire_db_handler();

                // ✅ 构造 SQL 插入语句
                    std::ostringstream sql;
                    sql << "INSERT INTO DISH_CATEGORY "
                        << "(categoryId, merchantId, categoryName, sortOrder) "
                        << "VALUES ('" << categoryId << "', '"
                        << merchantId << "', '"
                        << categoryName << "', "
                        << sortOrder << ")";

                    db->query(sql.str());
                    release_db_handler(std::move(db));

                    res.set_content("{\"status\":\"success\"}", "application/json");
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                    }
            });
        });

        //插入菜品接口(基于dish表)：：    //添加菜品接口
        server.Post("/merchant/add_dish", [&](const httplib::Request& req, httplib::Response& res)
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/merchant/add_dish request body: " << req.body << std::endl;

                    Json::Value dish = parse_json(req.body);

                    // ✅ 从 JSON 中读取所有字段（全部必填，前端需完整传入）
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

                    // ✅ 控制台打印，便于调试与追踪
                    std::cout << "[添加菜品] dishId: " << dishId << std::endl;
                    std::cout << "[添加菜品] merchantId: " << merchantId << std::endl;
                    std::cout << "[添加菜品] categoryId: " << categoryId << std::endl;
                    std::cout << "[添加菜品] name: " << name << std::endl;
                    std::cout << "[添加菜品] price: " << price << std::endl;
                    std::cout << "[添加菜品] stock: " << stock << ", sales: " << sales << ", rating: " << rating << std::endl;
                    std::cout << "[添加菜品] isOnSale: " << isOnSale << std::endl;

                    auto db = acquire_db_handler();

                    // ✅ 构造 SQL 插入语句
                    std::ostringstream sql;
                    sql << "INSERT INTO DISH "
                        << "(dishId, merchantId, categoryId, name, description, price, imageUrl, stock, sales, rating, isOnSale) "
                        << "VALUES ('" << dishId << "', '"
                        << merchantId << "', '"
                        << categoryId << "', '"
                        << name << "', '"
                        << description << "', "
                        << price << ", '"
                        << imageUrl << "', "
                        << stock << ", "
                        << sales << ", "
                        << rating << ", "
                        << (isOnSale ? "1" : "0") << ")";

                    db->query(sql.str());
                    release_db_handler(std::move(db));

                    res.set_content("{\"status\":\"success\"}", "application/json");

                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
        });
        //用户注册接口
        server.Post("/user/register", [&](const httplib::Request& req, httplib::Response& res)
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/user/register request body: " << req.body << std::endl;

                    Json::Value user = parse_json(req.body);

                    // ✅ 获取字段
                    const std::string userId = user["userId"].asString();
                    const std::string username = user["username"].asString();
                    const std::string passwordHash = user["passwordHash"].asString();
                    const std::string email = user.get("email", "").asString();
                    const std::string phoneNumber = user.get("phoneNumber", "").asString();
                    const std::string status = user.get("status", "active").asString();
                    const std::string avatarUrl = user.get("avatarUrl", "").asString();
                    const std::string gender = user.get("gender", "").asString();

                    // ✅ 打印调试信息
                    std::cout << "Registering user: " << username << ", ID: " << userId << std::endl;

                    auto db = acquire_db_handler();

                    // ✅ 构造 SQL 插入语句
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

                    // ✅ 返回成功响应
                    res.set_content("{\"status\":\"success\"}", "application/json");

                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
        });
        // 提交订单接口
        server.Post("/order/create", [&](const httplib::Request& req, httplib::Response& res)
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/order/create request body: " << req.body << std::endl;

                    Json::Value order = parse_json(req.body);

                    // ✅ 提取字段
                    const std::string orderId = order.get("orderId", generate_uuid()).asString(); // 允许前端提供或后端生成
                    const std::string userId = order["userId"].asString();
                    const std::string merchantId = order["merchantId"].asString();
                    const std::string addressId = order["addressId"].asString();
                    const std::string remark = order.get("remark", "").asString();
                    double totalPrice = order["totalPrice"].asDouble();

                    std::cout << "Creating order for userId: " << userId << ", merchantId: " << merchantId << std::endl;
                    std::cout << "Total Price: " << totalPrice << ", Address: " << addressId << std::endl;

                    auto db = acquire_db_handler();

                    // ✅ 插入订单主表
                    std::ostringstream order_sql;
                    order_sql << "INSERT INTO `ORDER` (orderId, userId, merchantId, totalPrice, status, orderTime, addressId, remark) "
                            << "VALUES ('" << orderId << "', '" << userId << "', '" << merchantId << "', "
                            << totalPrice << ", 'PENDING_PAYMENT', NOW(), '" << addressId << "', '" << remark << "')";
                    db->query(order_sql.str());

                    // ✅ 插入订单项
                    const Json::Value& items = order["items"];
                    for (const auto& item : items) {
                        const std::string orderItemId = generate_uuid();
                        const std::string dishId = item["dishId"].asString();
                        const std::string dishName = item["dishName"].asString();
                        double price = item["price"].asDouble();
                        int quantity = item["quantity"].asInt();

                        std::cout << "Adding item: " << dishName << " (" << quantity << " x " << price << ")" << std::endl;

                        std::ostringstream item_sql;
                        item_sql << "INSERT INTO ORDER_ITEM (orderItemId, orderId, dishId, dishName, price, quantity) "
                                << "VALUES ('" << orderItemId << "', '" << orderId << "', '" << dishId << "', '"
                                << dishName << "', " << price << ", " << quantity << ")";
                        db->query(item_sql.str());
                    }

                    release_db_handler(std::move(db));

                    // ✅ 返回响应
                    Json::Value response;
                    response["status"] = "success";
                    response["orderId"] = orderId;

                    Json::StreamWriterBuilder writer;
                    res.set_content(Json::writeString(writer, response), "application/json");

                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
        });

         //用户地址插入接口
        server.Post("/merchant/add_user_address", [&](const httplib::Request& req, httplib::Response& res) 
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/merchant/add_user_address request body: " << req.body << std::endl;

                    Json::Value address = parse_json(req.body);

                    //  自动生成 addressId
                    const std::string addressId = generate_short_id(); 

                    const std::string userId = address["userId"].asString();
                    const std::string recipientName = address["recipientName"].asString();
                    const std::string phoneNumber = address["phoneNumber"].asString();
                    const std::string fullAddress = address["fullAddress"].asString();
                    const int isDefault = address.get("isDefault", 0).asInt(); // 默认值为0

                        // 日志打印，字段一一明确
                    std::cout << "[用户地址接口] 自动生成的 addressId: " << addressId << std::endl;
                    std::cout << "[用户地址接口] userId（用户ID）: " << userId << std::endl;
                    std::cout << "[用户地址接口] recipientName（收货人姓名）: " << recipientName << std::endl;
                    std::cout << "[用户地址接口] phoneNumber（联系方式）: " << phoneNumber << std::endl;
                    std::cout << "[用户地址接口] fullAddress（详细地址）: " << fullAddress << std::endl;
                    std::cout << "[用户地址接口] isDefault（是否默认）: " << isDefault << std::endl;

                    auto db = acquire_db_handler();

                    std::ostringstream sql;
                    sql << "INSERT INTO USER_ADDRESS (addressId, userId, recipientName, phoneNumber, fullAddress, isDefault) "
                        << "VALUES ('" << addressId << "', '"
                        << userId << "', '"
                        << recipientName << "', '"
                        << phoneNumber << "', '"
                        << fullAddress << "', "
                        << isDefault << ")";

                    std::cout << "[用户地址接口] 执行 SQL: " << sql.str() << std::endl;

                    db->query(sql.str());
                    release_db_handler(std::move(db));

                    res.set_content("{\"status\":\"success\", \"message\":\"地址添加成功！\"}", "application/json");

                } catch (const std::exception& e) {
                    std::cout << "[用户地址接口] 错误：" << e.what() << std::endl;
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
        });

        server.Post("/comment/add", [&](const httplib::Request& req, httplib::Response& res)
        {
            threadPool.enqueue([this, req, &res] {
                try {
                    std::cout << "/comment/add request body: " << req.body << std::endl;

                    Json::Value comment = parse_json(req.body);

                    // ✅ 从 JSON 中读取字段（userId必填，其他可选）
                    const std::string commentId = generate_uuid();
                    const std::string userId = comment["userId"].asString();
                    const std::string dishId = comment.get("dishId", "").asString();
                    const int rating = comment.get("rating", 5).asInt();
                    const std::string content = comment.get("content", "").asString();

                    // ✅ 控制台打印，便于调试与追踪
                    std::cout << "[添加评论] commentId: " << commentId << std::endl;
                    std::cout << "[添加评论] userId: " << userId << std::endl;
                    std::cout << "[添加评论] dishId: " << dishId << std::endl;
                    std::cout << "[添加评论] rating: " << rating << std::endl;
                    std::cout << "[添加评论] content: " << content << std::endl;

                    auto db = acquire_db_handler();

                    // ✅ 构造 SQL 插入语句
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

                    res.set_content("{\"status\":\"success\"}", "application/json");

                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\", \"message\": \"" + std::string(e.what()) + "\"}", "application/json");
                }
            });
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


}
