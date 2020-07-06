/*
    Smithproxy- transparent proxy with SSL inspection capabilities.
    Copyright (c) 2014, Ales Stibal <astib@mag0.net>, All rights reserved.

    Smithproxy is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Smithproxy is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Smithproxy.  If not, see <http://www.gnu.org/licenses/>.

    Linking Smithproxy statically or dynamically with other modules is
    making a combined work based on Smithproxy. Thus, the terms and
    conditions of the GNU General Public License cover the whole combination.

    In addition, as a special exception, the copyright holders of Smithproxy
    give you permission to combine Smithproxy with free software programs
    or libraries that are released under the GNU LGPL and with code
    included in the standard release of OpenSSL under the OpenSSL's license
    (or modified versions of such code, with unchanged license).
    You may copy and distribute such a system following the terms
    of the GNU GPL for Smithproxy and the licenses of the other code
    concerned, provided that you include the source code of that other code
    when and as the GNU GPL requires distribution of source code.

    Note that people who make modified versions of Smithproxy are not
    obligated to grant this special exception for their modified versions;
    it is their choice whether to do so. The GNU General Public License
    gives permission to release a modified version without this exception;
    this exception also makes it possible to release a modified version
    which carries forward this exception.
*/    

#ifndef SRVUTILS_HPP_
#define SRVUTILS_HPP_

#include <policy/authfactory.hpp>

class NetworkServiceFactory {
public:

    using proxy_type = threadedProxyWorker::proxy_type_t;

    static logan_lite& log() {
        static logan_lite l("service");
        return l;
    }

    template <class Listener, class Com>
    static Listener *
    prepare_listener (unsigned int port, std::string const &friendly_name, int sub_workers, proxy_type type);
    template <class Listener, class Com>
    static Listener* prepare_listener(std::string const& str_path, std::string const& friendly_name, std::string const& def_path, int sub_workers, proxy_type type);
};

template <class Listener, class Com>
Listener * NetworkServiceFactory::prepare_listener (unsigned int port, std::string const &friendly_name, int sub_workers,
                                                    proxy_type type) {

    auto log = NetworkServiceFactory::log();

    if(sub_workers < 0) {
        return nullptr;
    }

    _not("Entering %s mode on port %d", friendly_name.c_str(), port);
    auto listener = new Listener(new Com(), type);
    listener->com()->nonlocal_dst(true);
    listener->worker_count_preference(sub_workers);

    // bind with master proxy (.. and create child proxies for new connections)
    int sock = listener->bind(port, 'L');

    locks::fd().insert(sock);

    auto l_ = std::scoped_lock(*locks::fd().lock(sock));

    if (sock < 0) {
        _fat("Error binding %s on port %d, exiting", friendly_name.c_str(), sock);
        delete listener;
        return nullptr;
    };
    listener->com()->unblock(sock);
    
    listener->com()->set_monitor(sock);
    listener->com()->set_poll_handler(sock, listener);

    return listener;
}

template <class Listener, class Com>
Listener* NetworkServiceFactory::prepare_listener(std::string const& str_path, std::string const& friendly_name, std::string const& def_path, int sub_workers, proxy_type type) {

    auto log = NetworkServiceFactory::log();

    if(sub_workers < 0) {
        return nullptr;
    }
    
    std::string path = str_path;
    if( path.empty() ) {
        path = def_path;
    }
    
    _not("Entering %s mode on path %s", friendly_name.c_str(), path.c_str());
    auto listener = new Listener(new Com(), type);

    listener->com()->nonlocal_dst(true);
    listener->worker_count_preference(sub_workers);

    // bind with master proxy (.. and create child proxies for new connections)
    int sock = listener->bind(path.c_str(), 'L');
    if (sock < 0) {
        _fat("Error binding %s on path %s, exiting", friendly_name.c_str(), path.c_str());
        delete listener;
        return nullptr;
    };
    
    return listener;
}


#endif