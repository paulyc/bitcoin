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
#include <memory>
#include <map>
#include <exception>
#include <iostream>
using std::cout;
using std::cerr;
using std::endl;

#include <boost/thread.hpp>

#include "mysql/mysql_driver.h"
#include "mysql/mysql_connection.h"
#include "mysql/mysql_error.h"
#include "mysql/cppconn/prepared_statement.h"
#include "mysql/cppconn/exception.h"

class MySqlDbConnection;
class MySqlPreparedStatement;

class MySqlDbConnection /*: public sql::mysql::MySQL_Prepared_Statement*/ {
public:
    MySqlDbConnection(sql::Connection *ptr) : con(std::unique_ptr<sql::Connection>(ptr)) {
        con->setAutoCommit(false);
        con->setSchema("bitcoin");
        con->nativeSQL("USE bitcoin");
        
        con->nativeSQL("START TRANSACTION");
        con->nativeSQL("truncate table Transaction");
        con->nativeSQL("truncate table BlockHeader");
        con->nativeSQL("truncate table TxInput");
        con->nativeSQL("truncate table TxOutput");
        con->commit();
    }
    
    void startTransaction() {
        cout << "START TRANSACTION" << endl;
        con->nativeSQL("START TRANSACTION");
    }
    void rollbackTransaction() {
        cout << "ROLLBACK" <<endl;
        con->rollback();
    }
    void commitTransaction() {
        cout << "COMMIT" << endl;
        con->commit();
    }
    
    void nativeSQL(sql::SQLString &sql) {
        con->nativeSQL(sql);
    }
    
    uint64_t getLastInsertId() {
        // this is so fucked up. fuck you mysql
        sql::Statement *s = con->createStatement();
        sql::ResultSet *rs = s->executeQuery("SELECT LAST_INSERT_ID()");
        rs->next();
        uint64_t id =rs->getUInt64(1);
        delete rs;
        delete s;
        return id;
    }
    
    std::unique_ptr<sql::PreparedStatement> prepareStatement(const sql::SQLString &sql) {
        return std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql));
    }
    
    bool tryTransaction(std::function<void(MySqlDbConnection&)> fun) {
        try {
            this->startTransaction();
            fun(*this);
            this->commitTransaction();
        } catch (sql::SQLException &ex) {
            cerr << ex.what() << endl;
            this->rollbackTransaction();
            return false;
        } catch (std::exception &ex) {
            cerr << ex.what() << endl;
            this->rollbackTransaction();
            return false;
        }
        return true;
    }
protected:
//todo delete these
    std::unique_ptr<sql::Connection> con;
    //std::unique_ptr<MySqlPreparedStatement> getLastInsertId;
    //std::map<const std::string, std::unique_ptr<MySqlPreparedStatement> > statements;
};

class MySqlPreparedStatement {
public:
    MySqlPreparedStatement(std::unique_ptr<MySqlDbConnection> &conn, const sql::SQLString &sql) :
    sql(sql),
    stmt(std::unique_ptr<sql::PreparedStatement>(conn->prepareStatement(sql))) {
    }
    
    void setBlob(unsigned int parameterIndex, std::istream * blob) {
        stmt->setBlob(parameterIndex, blob);
    }
    void setString(unsigned int parameterIndex, const sql::SQLString &value) {
        stmt->setString(parameterIndex, value);
    }
    bool execute() {
        cout << "MySqlPreparedStatement::execute " << sql << endl;
        return stmt->execute();
    }
    int executeUpdate() {
        cout << "MySqlPreparedStatement::executeUpdate " << sql << endl;
        return stmt->executeUpdate();
    }
    
protected:
    const sql::SQLString sql;
    std::unique_ptr<sql::PreparedStatement> stmt;
};



class MySqlDbDriver {
public:
    MySqlDbDriver() : driver(sql::mysql::get_mysql_driver_instance()) {
        std::call_once(init, [this]() {
            driver->threadInit();
        });
    }
    virtual ~MySqlDbDriver() {
        std::call_once(shutdown, [this]() {
            driver->threadEnd();
        });
    }
    std::unique_ptr<MySqlDbConnection> getConnection() {
        return std::unique_ptr<MySqlDbConnection>(new MySqlDbConnection(driver->connect("tcp://127.0.0.1:3306", "root", "")));
    }
private:
    static std::once_flag init;
    static std::once_flag shutdown;
    sql::mysql::MySQL_Driver *driver;
};

#endif /* mysqlinterface_hpp */
