-- 用户表(user）
CREATE TABLE `USER` (
  `userId` VARCHAR(36) NOT NULL,
  `username` VARCHAR(50) NOT NULL,
  `passwordHash` VARCHAR(255) NOT NULL,
  `email` VARCHAR(100),
  `phoneNumber` VARCHAR(20),
  `registrationDate` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `status` VARCHAR(20) NOT NULL DEFAULT 'active',
  `avatarUrl` VARCHAR(255),
  `gender` VARCHAR(10),
  PRIMARY KEY (`userId`),
  UNIQUE KEY `idx_username` (`username`),
  UNIQUE KEY `idx_email` (`email`),
  UNIQUE KEY `idx_phoneNumber` (`phoneNumber`),
  INDEX `idx_registrationDate` (`registrationDate`),
  INDEX `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;


--用户地址表
CREATE TABLE `USER_ADDRESS` (
  `addressId` VARCHAR(36) NOT NULL,
  `userId` VARCHAR(36) NOT NULL,
  `recipientName` VARCHAR(50) NOT NULL,
  `phoneNumber` VARCHAR(20) NOT NULL,
  `fullAddress` VARCHAR(255) NOT NULL,
  `isDefault` TINYINT(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`addressId`),
  UNIQUE KEY `idx_addressId` (`addressId`),
  INDEX `idx_userId` (`userId`),
  CONSTRAINT `fk_user_address_user` FOREIGN KEY (`userId`) REFERENCES `USER` (`userId`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 商家表(MERCHANT)
CREATE TABLE `MERCHANT` (
  `merchantId` VARCHAR(36) NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `description` TEXT,
  `address` VARCHAR(255) NOT NULL,
  `phoneNumber` VARCHAR(20) NOT NULL,
  `logoUrl` VARCHAR(255),
  `isOpen` TINYINT(1) NOT NULL DEFAULT 0,
  `registrationDate` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `status` VARCHAR(20) NOT NULL DEFAULT 'pending',
  PRIMARY KEY (`merchantId`),
  UNIQUE KEY `idx_merchantId` (`merchantId`),
  UNIQUE KEY `idx_name` (`name`),
  INDEX `idx_isOpen` (`isOpen`),
  INDEX `idx_registrationDate` (`registrationDate`),
  INDEX `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 菜品分类表(DISH_CATEGORY)
CREATE TABLE `DISH_CATEGORY` (
  `categoryId` VARCHAR(36) NOT NULL,
  `merchantId` VARCHAR(36) NOT NULL,
  `categoryName` VARCHAR(50) NOT NULL,
  `sortOrder` INT NOT NULL DEFAULT 0,
  PRIMARY KEY (`categoryId`),
  UNIQUE KEY `idx_categoryId` (`categoryId`),
  INDEX `idx_merchantId` (`merchantId`),
  INDEX `idx_categoryName` (`categoryName`),
  CONSTRAINT `fk_dish_category_merchant` FOREIGN KEY (`merchantId`) REFERENCES `MERCHANT` (`merchantId`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 菜品表(dish)
CREATE TABLE `DISH` (
  `dishId` VARCHAR(36) NOT NULL,
  `merchantId` VARCHAR(36) NOT NULL,
  `categoryId` VARCHAR(36) NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `description` TEXT,
  `price` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
  `imageUrl` VARCHAR(255),
  `stock` INT NOT NULL DEFAULT 0,
  `sales` INT NOT NULL DEFAULT 0,
  `rating` DECIMAL(2,1) NOT NULL DEFAULT 0.0,
  `isOnSale` TINYINT(1) NOT NULL DEFAULT 1,
  PRIMARY KEY (`dishId`),
  UNIQUE KEY `idx_dishId` (`dishId`),
  INDEX `idx_merchantId` (`merchantId`),
  INDEX `idx_categoryId` (`categoryId`),
  INDEX `idx_name` (`name`),
  INDEX `idx_price` (`price`),
  INDEX `idx_stock` (`stock`),
  INDEX `idx_sales` (`sales`),
  INDEX `idx_rating` (`rating`),
  INDEX `idx_isOnSale` (`isOnSale`),
  CONSTRAINT `fk_dish_merchant` FOREIGN KEY (`merchantId`) REFERENCES `MERCHANT` (`merchantId`) ON DELETE CASCADE,
  CONSTRAINT `fk_dish_category` FOREIGN KEY (`categoryId`) REFERENCES `DISH_CATEGORY` (`categoryId`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 订单表(order）
CREATE TABLE `ORDER` (
  `orderId` VARCHAR(36) NOT NULL,
  `userId` VARCHAR(36) NOT NULL,
  `merchantId` VARCHAR(36) NOT NULL,
  `totalPrice` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
  `status` VARCHAR(30) NOT NULL DEFAULT 'PENDING_PAYMENT',
  `orderTime` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `paymentTime` DATETIME,
  `estimatedDeliveryTime` DATETIME,
  `actualDeliveryTime` DATETIME,
  `remark` TEXT,
  `addressId` VARCHAR(36) NOT NULL,
  PRIMARY KEY (`orderId`),
  UNIQUE KEY `idx_orderId` (`orderId`),
  INDEX `idx_userId` (`userId`),
  INDEX `idx_merchantId` (`merchantId`),
  INDEX `idx_status` (`status`),
  INDEX `idx_orderTime` (`orderTime`),
  INDEX `idx_paymentTime` (`paymentTime`),
  INDEX `idx_estimatedDeliveryTime` (`estimatedDeliveryTime`),
  INDEX `idx_actualDeliveryTime` (`actualDeliveryTime`),
  INDEX `idx_addressId` (`addressId`),
  CONSTRAINT `fk_order_user` FOREIGN KEY (`userId`) REFERENCES `USER` (`userId`),
  CONSTRAINT `fk_order_merchant` FOREIGN KEY (`merchantId`) REFERENCES `MERCHANT` (`merchantId`),
  CONSTRAINT `fk_order_address` FOREIGN KEY (`addressId`) REFERENCES `USER_ADDRESS` (`addressId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 订单项表(ORDER_ITEM)
CREATE TABLE `ORDER_ITEM` (
  `orderItemId` VARCHAR(36) NOT NULL,
  `orderId` VARCHAR(36) NOT NULL,
  `dishId` VARCHAR(36) NOT NULL,
  `dishName` VARCHAR(100) NOT NULL,
  `price` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
  `quantity` INT NOT NULL DEFAULT 1,
  PRIMARY KEY (`orderItemId`),
  UNIQUE KEY `idx_orderItemId` (`orderItemId`),
  INDEX `idx_orderId` (`orderId`),
  INDEX `idx_dishId` (`dishId`),
  CONSTRAINT `fk_order_item_order` FOREIGN KEY (`orderId`) REFERENCES `ORDER` (`orderId`) ON DELETE CASCADE,
  CONSTRAINT `fk_order_item_dish` FOREIGN KEY (`dishId`) REFERENCES `DISH` (`dishId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;


-- 支付记录表(PAYMENT_RECORD)
CREATE TABLE `PAYMENT_RECORD` (
  `paymentId` VARCHAR(36) NOT NULL,
  `orderId` VARCHAR(36) NOT NULL,
  `amount` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
  `paymentTime` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `paymentMethod` VARCHAR(50) NOT NULL,
  `transactionId` VARCHAR(100),
  `status` VARCHAR(20) NOT NULL DEFAULT 'SUCCESS',
  PRIMARY KEY (`paymentId`),
  UNIQUE KEY `idx_paymentId` (`paymentId`),
  UNIQUE KEY `idx_orderId` (`orderId`),
  UNIQUE KEY `idx_transactionId` (`transactionId`),
  INDEX `idx_paymentTime` (`paymentTime`),
  INDEX `idx_status` (`status`),
  CONSTRAINT `fk_payment_record_order` FOREIGN KEY (`orderId`) REFERENCES `ORDER` (`orderId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 配送信息表(DELIVERY_INFO)
CREATE TABLE `DELIVERY_INFO` (
  `deliveryId` VARCHAR(36) NOT NULL,
  `orderId` VARCHAR(36) NOT NULL,
  `deliveryStatus` VARCHAR(30) NOT NULL DEFAULT 'PENDING_PICKUP',
  `estimatedDeliveryTime` DATETIME,
  `actualDeliveryTime` DATETIME,
  `deliveryPersonId` VARCHAR(36),
  `deliveryPersonName` VARCHAR(50),
  `deliveryPersonPhone` VARCHAR(20),
  PRIMARY KEY (`deliveryId`),
  UNIQUE KEY `idx_deliveryId` (`deliveryId`),
  UNIQUE KEY `idx_orderId` (`orderId`),
  INDEX `idx_deliveryStatus` (`deliveryStatus`),
  INDEX `idx_estimatedDeliveryTime` (`estimatedDeliveryTime`),
  INDEX `idx_actualDeliveryTime` (`actualDeliveryTime`),
  INDEX `idx_deliveryPersonId` (`deliveryPersonId`),
  CONSTRAINT `fk_delivery_info_order` FOREIGN KEY (`orderId`) REFERENCES `ORDER` (`orderId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 用户评论表(USER_COMMENT)
CREATE TABLE `USER_COMMENT` (
  `commentId` VARCHAR(36) NOT NULL,
  `userId` VARCHAR(36) NOT NULL,
  `dishId` VARCHAR(36),
  `rating` INT NOT NULL DEFAULT 5,
  `content` TEXT,
  `commentTime` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`commentId`),
  UNIQUE KEY `idx_commentId` (`commentId`),
  INDEX `idx_userId` (`userId`),
  INDEX `idx_dishId` (`dishId`),
  INDEX `idx_rating` (`rating`),
  INDEX `idx_commentTime` (`commentTime`),
  CONSTRAINT `fk_user_comment_user` FOREIGN KEY (`userId`) REFERENCES `USER` (`userId`),
  CONSTRAINT `fk_user_comment_dish` FOREIGN KEY (`dishId`) REFERENCES `DISH` (`dishId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 商家评价表(MERCHANT_REVIEW)
CREATE TABLE `MERCHANT_REVIEW` (
  `reviewId` VARCHAR(36) NOT NULL,
  `userId` VARCHAR(36) NOT NULL,
  `merchantId` VARCHAR(36) NOT NULL,
  `rating` INT NOT NULL DEFAULT 5,
  `content` TEXT,
  `reviewTime` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`reviewId`),
  UNIQUE KEY `idx_reviewId` (`reviewId`),
  INDEX `idx_userId` (`userId`),
  INDEX `idx_merchantId` (`merchantId`),
  INDEX `idx_rating` (`rating`),
  INDEX `idx_reviewTime` (`reviewTime`),
  CONSTRAINT `fk_merchant_review_user` FOREIGN KEY (`userId`) REFERENCES `USER` (`userId`),
  CONSTRAINT `fk_merchant_review_merchant` FOREIGN KEY (`merchantId`) REFERENCES `MERCHANT` (`merchantId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 管理员表(ADMIN_USER)
CREATE TABLE `ADMIN_USER` (
  `adminId` VARCHAR(36) NOT NULL,
  `username` VARCHAR(50) NOT NULL,
  `passwordHash` VARCHAR(255) NOT NULL,
  `role` VARCHAR(50) NOT NULL DEFAULT 'operator',
  `lastLogin` DATETIME,
  PRIMARY KEY (`adminId`),
  UNIQUE KEY `idx_adminId` (`adminId`),
  UNIQUE KEY `idx_username` (`username`),
  INDEX `idx_role` (`role`),
  INDEX `idx_lastLogin` (`lastLogin`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
