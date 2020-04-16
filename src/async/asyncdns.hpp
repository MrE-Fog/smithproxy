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
#ifndef ASYNCDNS_HPP
#define ASYNCDNS_HPP

#include <atomic>

#include <async/asyncsocket.hpp>
#include <inspect/dns.hpp>


class AsyncDnsQuery : public AsyncSocket<std::pair<DNS_Response *, int>> {
public:
    explicit AsyncDnsQuery(baseHostCX* owner, callback_t callback = nullptr):
            AsyncSocket(owner, std::move(callback)),
            log(get_log()),
            id(counter()++)
            {}
    using dns_response_t = std::pair<DNS_Response *, int>;

    task_state_t update() override {
        response = DNSFactory::get().recv_dns_response(socket(),0);
        if(response.first) {
            _dia("AsyncDnsQuery::update[%u] finished request for %s", id, response.first->question_str_0().c_str());
            return task_state_t::FINISHED;
        }

        _dia("AsyncDnsQuery::update[%u] running request", id);
        return task_state_t::RUNNING;
    }

    dns_response_t& yield () override {
        return response;
    }

private:
    dns_response_t response {nullptr, -1};
    logan_lite& log;
    logan_lite& get_log() { static auto l = logan_lite("com.dns.async"); return l; }

    uint64_t id;
    static std::atomic_uint64_t& counter() { static std::atomic_uint64_t c; return c; };
};

#endif //ASYNCDNS_HPP
