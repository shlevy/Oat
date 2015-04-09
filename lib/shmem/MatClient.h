//******************************************************************************
//* Copyright (c) Jon Newman (jpnewman at mit snail edu) 
//* All right reserved.
//* This file is part of the Simple Tracker project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//******************************************************************************

#ifndef MATCLIENT_H
#define	MATCLIENT_H

#include "SharedMat.h"

#include <string>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>

namespace ip = boost::interprocess;

class MatClient {
public:
    //MatClient(void);
    MatClient(const std::string server_name);
    MatClient(const MatClient& orig);
    virtual ~MatClient();
    
    // Find cv::Mat object in shared memory
    void findSharedMat(void);
    
    // Condition variable manipulation
    //void notifyAll(void);
    void wait(void);
    //void notifyAllAndWait(void);
     
    // Accessors
    cv::Mat get_shared_mat(void);
    std::string get_name(void) { return name; }
    bool is_shared_mat_created(void) { return shared_mat_created; }
    void set_source(const std::string);
    
private:
    
    ip::sharable_lock<ip::interprocess_sharable_mutex> makeLock();

    std::string name;
    shmem::SharedMatHeader* shared_mat_header;
    bool shared_mat_created;
    void* shared_mat_data_ptr;
    int data_size; // Size of raw mat data in bytes

    // Shared mat object, constructed from the shared_mat_header
    cv::Mat mat;

    std::string shmem_name, shobj_name;
    ip::managed_shared_memory shared_memory;
    ip::sharable_lock<ip::interprocess_sharable_mutex> lock;
};

#endif	/* MATCLIENT_H */
