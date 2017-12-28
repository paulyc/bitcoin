//
//  mysqlinterface.cpp
//  bitcoin-import
//
//  Created by Paul Ciarlo on 12/27/17.
//

#include "mysqlinterface.hpp"
#include "mysql/mysql_driver.h"
#include "mysql/mysql_connection.h"
#include "mysql/mysql_error.h"

std::mutex MySqlDbDriver::m;

std::unique_ptr<MySqlDbConnection> MySqlDbDriver::getConnection() {
    return make_unique(new MySqlDbConnection(driver->connect("tcp://127.0.0.1:3306", "root", "")));    
}
