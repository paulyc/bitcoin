//
//  mysqlinterface.cpp
//  bitcoin-import
//
//  Created by Paul Ciarlo on 12/27/17.
//

#include "mysqlinterface.hpp"

std::once_flag MySqlDbDriver::init;
std::once_flag MySqlDbDriver::shutdown;
