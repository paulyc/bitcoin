//
//  mysqlinterface.hpp
//  bitcoin-import
//
//  Created by Paul Ciarlo on 12/27/17.
//

#ifndef mysqlinterface_hpp
#define mysqlinterface_hpp

#include <stdio.h>
#include <thread>

class MySqlDbConnection;
class SqlDbDriver {

};

class MySqlDbDriver : public SqlDbDriver {
public:
    MySqlDbDriver() {
        std::unique_lock<std::mutex> lock(m);
        driver.reset(sql::mysql::get_mysql_driver_instance());
    }
    std::unique_ptr<MySqlDbConnection> getConnection();
private:
    static std::mutex m;
    std::unique_ptr<sql::mysql::MySQL_Driver> driver;
};

class MySqlDbConnection {
public:
    MySqlDbConnection(sql::Connection *ptr) : con(ptr) {
    }
private:
    sql::Connection *con;
};


#endif /* mysqlinterface_hpp */
