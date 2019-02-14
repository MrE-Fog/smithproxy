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

#include <sys/socket.h>

#include <cfgapi.hpp>
#include <sockshostcx.hpp>
#include <logger.hpp>
#include <dns.hpp>
#include <smithdnsupd.hpp>

std::string socksTCPCom::sockstcpcom_name_ = "sock5";
std::string socksSSLMitmCom::sockssslmitmcom_name_ = "s5+ssl+insp";

bool socksServerCX::global_async_dns = true;

DEFINE_LOGGING(socksServerCX);

socksServerCX::socksServerCX(baseCom* c, unsigned int s) : baseHostCX(c,s) {
    state_ = INIT;

    // copy setting from global/static variable - don't allow to change async
    // flag on the background during the object life
    async_dns = global_async_dns;
}

socksServerCX::~socksServerCX() {

    if(left)  { delete left; }
    if(right) { delete right; }
    if(async_dns_socket) { ::close(async_dns_socket); }
}


int socksServerCX::process() {
    switch(state_) {
        case INIT:
            return process_socks_hello();
        case HELLO_SENT:
            return 0; // we sent response to client hello, don't process anything
        case WAIT_REQUEST:
            return process_socks_request();
        default:
            break;
    }
    
    return 0;
}

int socksServerCX::process_socks_hello() {
        
    buffer* b = readbuf();
    if(b->size() < 3) {
        // minimal size of "client hello" is 3 bytes
        return 0;
    }
                   version = b->get_at<unsigned char>(0);
    unsigned char nmethods = b->get_at<unsigned char>(1);
    
    if(b->size() < (unsigned int)(2 + nmethods)) {
        return 0;
    }
    
    // at this stage we have full client hello received
    if(version == 5) {
        DIA_("socksServerCX::process_socks_init: version %d", version);
        
        unsigned char server_hello[2];
        server_hello[0] = 5; // version
        server_hello[1] = 0; // no authentication
        
        writebuf()->append(server_hello,2);
        state_ = HELLO_SENT;
        state_ = WAIT_REQUEST;
        
        // flush all data, assuming 
        return b->size();
    }
    else if(version == 4) {
        DIA_("socksServerCX::process_socks_init: version %d", version);
        return process_socks_request();
    } else {
        DIAS_("socksServerCX::process_socks_init: unsupported socks version");
        error(true);
    }

    return 0;
}

bool socksServerCX::choose_server_ip(std::vector<std::string>& target_ips) {

    if(target_ips.empty())
        return false;

    // for now use just first one (cleaned up from empty ones)
    std::string t = target_ips.at(0);

    DIA_("choose_server_ip: chosen target: %s (out of %d)",t.c_str(),target_ips.size());
    com()->nonlocal_dst_host() = t;
    com()->nonlocal_dst_resolved(true);

    return true;
}

bool socksServerCX::process_dns_response(DNS_Response* resp) {

    std::vector<std::string> target_ips;
    bool del_resp = true;
    bool ret = true;

    if (resp) {
        for (DNS_Answer &a: resp->answers()) {
            std::string a_ip = a.ip(false);
            if (a_ip.size()) {
                DIA_("process_dns_response: target candidate: %s", a_ip.c_str());
                target_ips.push_back(a_ip);
            }
        }

        if (target_ips.size()) {
            inspect_dns_cache.lock();
            DNS_Inspector di;
            del_resp = !di.store(resp);
            inspect_dns_cache.unlock();
        }

        if (del_resp) {
            delete resp;
        }
    }

    if(!target_ips.empty() && choose_server_ip(target_ips)) {
        setup_target();
        DIAS_("process_dns_response: waiting for policy check");

    } else {
        INFS___("process_dns_response: unable to find destination address for the request");
        ret = false;
    }

    return ret;
}

int socksServerCX::process_socks_request() {

    socks5_request_error e = NONE;
    
    if(readbuf()->size() < 5) {
        return 0; // wait for more complete request
    }
    
    DIAS_("socksServerCX::process_socks_request");
    DEB_("Request dump:\n%s",hex_dump(readbuf()->data(),readbuf()->size()).c_str());
    
              version = readbuf()->get_at<unsigned char>(0);
    unsigned char cmd = readbuf()->get_at<unsigned char>(1);
    //@2 is reserved
    
    if(version < 4 or version > 5) {
        e = UNSUPPORTED_VERSION;
        goto error;
    }
    
       
        if(version == 5) {
            
            if(readbuf()->size() < 10) {
                DIAS_("process_socks_request: socks5 request header too short");
                goto error;
            }
            
            unsigned char atype   = readbuf()->get_at<unsigned char>(3);
            
            if(atype != IPV4 && atype != FQDN) {
                e = UNSUPPORTED_ATYPE;
                goto error;
            }
            
            if(atype == FQDN) {
                req_atype = FQDN;
                state_ = REQ_RECEIVED;            
                
                unsigned char fqdn_sz = readbuf()->get_at<unsigned char>(4);
                if((unsigned int)fqdn_sz + 4 + 2 >= readbuf()->size()) {
                    ERRS_("protocol error: request header out of boundary.");
                    goto error;
                }
                
                DIA_("socks5 protocol: fqdn size: %d",fqdn_sz);
                std::string fqdn((const char*)&readbuf()->data()[5],fqdn_sz);
                DIA_("socks5 protocol: fqdn requested: %s",fqdn.c_str());
                req_str_addr = fqdn;
                
                req_port = ntohs(readbuf()->get_at<uint16_t>(5+fqdn_sz));
                DIA_("socks5 protocol: port requested: %d",req_port);

                com()->nonlocal_dst_port() = req_port;
                com()->nonlocal_src(true);
                DIA_("socksServerCX::process_socks_request: request (FQDN) for %s -> %s:%d",c_name(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());




                std::vector<std::string> target_ips;
                
                // Some implementations use atype FQDN eventhough the target is already IP
                CIDR* adr_as_fqdn = cidr_from_str(fqdn.c_str());
                if(adr_as_fqdn != nullptr) {
                    // hmm, it's an address
                    cidr_free(adr_as_fqdn);
                    
                    target_ips.push_back(fqdn);
                } else {
                    // really FQDN.
                    
                    inspect_dns_cache.lock();
                    DNS_Response* dns_resp = inspect_dns_cache.get("A:"+fqdn);
                    if(dns_resp) {
                        if (dns_resp->answers().size() > 0) {
                            int ttl = (dns_resp->loaded_at + dns_resp->answers().at(0).ttl_) - time(nullptr);                
                            if(ttl > 0) {
                                for( DNS_Answer& a: dns_resp->answers() ) {
                                    std::string a_ip = a.ip(false);
                                    if(a_ip.size()) {
                                        DIA_("cache candidate: %s",a_ip.c_str());
                                        target_ips.push_back(a_ip);
                                    }
                                }
                            }
                        }
                    }
                    inspect_dns_cache.unlock();
                }


                // cache is not populated - send out query
                if(target_ips.size() <= 0) {
                    // no targets, send DNS query
                    
                    std::string nameserver = "8.8.8.8";
                    if(cfgapi_obj_nameservers.size()) {
                        nameserver = cfgapi_obj_nameservers.at(0);
                    }

                    if(!async_dns) {

                        DNS_Response *resp = resolve_dns_s(fqdn, A, nameserver);

                        process_dns_response(resp);
                        setup_target();

                    } else {
                        async_dns_socket = send_dns_request(fqdn, A, nameserver);
                        if(async_dns_socket) {
                            DIA___("dns request sent: %s", fqdn.c_str());
                            com()->set_monitor(async_dns_socket);
                            com()->set_poll_handler(async_dns_socket,this);

                            com()->set_idle_watch(async_dns_socket);
                        } else {
                            ERR___("failed to send dns request: %s", fqdn.c_str());
                            error(true);
                        }
                    }
                } else {
                    if(!target_ips.empty() && choose_server_ip(target_ips)) {
                        setup_target();
                    } else {
                        INFS___("process_dns_response: unable to find destination address for the request");
                        error(true);
                    }
                }
            }
            else
            if(atype == IPV4) {
                req_atype = IPV4;
                state_ = REQ_RECEIVED;
                DIA_("socksServerCX::process_socks_request: request received, type %d", atype);
                
                uint32_t dst = readbuf()->get_at<uint32_t>(4);
                req_port = ntohs(readbuf()->get_at<uint16_t>(8));
                com()->nonlocal_dst_port() = req_port;
                com()->nonlocal_src(true);
                DIA_("socksServerCX::process_socks_request: request for %s -> %s:%d",c_name(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());

                
                req_addr.s_addr=dst;
                
                com()->nonlocal_dst_host() = string_format("%s",inet_ntoa(req_addr));
                com()->nonlocal_dst_port() = req_port;
                com()->nonlocal_src(true);
                DIA_("socksServerCX::process_socks_request: request (IPv4) for %s -> %s:%d",c_name(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());
                setup_target();
            }
        }
        
        else if (version == 4) {
            if(readbuf()->size() < 8) {
                DIAS_("process_socks_request: socks4 request header too short");
                goto error;
            }            
            
            req_atype = IPV4;
            state_ = REQ_RECEIVED;
            DIAS_("socksServerCX::process_socks_request: socks4 request received");
            
            req_port = ntohs(readbuf()->get_at<uint16_t>(2));
            uint32_t dst = readbuf()->get_at<uint32_t>(4);

            
            req_addr.s_addr=dst;
            
            com()->nonlocal_dst_host() = string_format("%s",inet_ntoa(req_addr));
            com()->nonlocal_dst_port() = req_port;
            com()->nonlocal_src(true);
            DIA_("socksServerCX::process_socks_request: request (SOCKSv4) for %s -> %s:%d",c_name(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());

            setup_target();
        }
        else {
            DIAS_("process_socks_request: unexpected socks version");
            goto error;
        }
        

        return readbuf()->size();

    error:
        DIA_("socksServerCX::process_socks_request: error %d",e);
        error(true);
        return readbuf()->size();
}

bool socksServerCX::setup_target() {
        // prepare a new CX!
        
        // LEFT
        int s = socket();
        
        baseCom* new_com = nullptr;
        switch(com()->nonlocal_dst_port()) {
            case 443:
            case 465:
            case 636:
            case 993:
            case 995:
                new_com = new baseSSLMitmCom<SSLCom>();
                handoff_as_ssl = true;
                break;
            default:
                new_com = new TCPCom();
        }
        
        MitmHostCX* n_cx = new MitmHostCX(new_com, s);
        n_cx->waiting_for_peercom(true);
        n_cx->com()->name();
        n_cx->name();
        n_cx->com()->nonlocal_dst(true);
        n_cx->com()->nonlocal_dst_host() = com()->nonlocal_dst_host();
        n_cx->com()->nonlocal_dst_port() = com()->nonlocal_dst_port();
        n_cx->com()->nonlocal_dst_resolved(true);
        
        
        // RIGHT


        MitmHostCX *target_cx = new MitmHostCX(n_cx->com()->slave(), n_cx->com()->nonlocal_dst_host().c_str(), 
                                            string_format("%d",n_cx->com()->nonlocal_dst_port()).c_str()
                                            );
        target_cx->waiting_for_peercom(true);
        
        std::string h;
        std::string p;
        n_cx->name();
        n_cx->com()->resolve_socket_src(n_cx->socket(),&h,&p);
        
        
        target_cx->com()->nonlocal_src(false); //FIXME
        target_cx->com()->nonlocal_src_host() = h;
        target_cx->com()->nonlocal_src_port() = std::stoi(p); 
        
        
        
        left = n_cx;
        DIA_("socksServerCX::setup_target: prepared left: %s",left->c_name());
        right = target_cx;
        DIA_("socksServerCX::setup_target: prepared right: %s",right->c_name());
        
        // peers are now prepared for handover. Owning proxy will wipe this CX (it will be empty)
        // and if policy allows, left and right will be set (also in proxy owning this cx).
        
        state_ = WAIT_POLICY;
        read_waiting_for_peercom(true);
        
        return true;
}

bool socksServerCX::new_message() {
    if(state_ == WAIT_POLICY && verdict_ == PENDING) {
        return true;
    }
    if(state_ == HANDOFF) {
        return true;
    }
    
    return false;
}

void socksServerCX::verdict(socks5_policy p) {
    verdict_ = p;
    state_ = POLICY_RECEIVED;
    
    if(verdict_ == ACCEPT || verdict_ == REJECT) {
        process_socks_reply();
    }
}

int socksServerCX::process_socks_reply() {
    if(version == 5) {
        
        unsigned char b[128];
        
        b[0] = 5;
        b[1] = 2; // denied
        if(verdict_ == ACCEPT) b[1] = 0; //accept
        b[2] = 0;
        b[3] = req_atype;
        
        int cur = 4;
        
        if(req_atype == IPV4) {
            *((uint32_t*)&b[cur]) = req_addr.s_addr;
            cur += sizeof(uint32_t);
        }
        else if(req_atype == FQDN) {
            
            b[cur] = (unsigned char)req_str_addr.size();
            cur++;
            
            for (char c: req_str_addr) {
                b[cur] = c;
                cur++;
            }
        }
            
        *((uint16_t*)&b[cur]) = htons(req_port);
        cur += sizeof(uint16_t);
        
        writebuf()->append(b,cur);
        state_ = REQRES_SENT;
        
        DEB_("socksServerCX::process_socks_reply: response dump:\n%s",hex_dump(b,cur).c_str());

        // response is about to be sent. In most cases client sends data on left,
        // but in case it's waiting ie. for banner, we must trigger proxy code to
        // actually connect the right side.
        // Because now are all data handled, there is no way how we get to proxy code,
        // unless:
        //      * new data appears on left
        //      * some error occurs on left
        //      * other way how socket appears in epoll result.
        //
        // we can acheive that to simply put left socket to write monitor.
        // This will make left socket writable (dummy - we don't have anything to write),
        // but also triggers proxy's on_message().

        com()->set_write_monitor(socket());
        return cur;
    } 
    else if(version == 4) {
        unsigned char b[8];
        
        b[0] = 0;
        b[1] = 91; // denied
        if(verdict_ == ACCEPT) b[1] = 90; //accept    
        
        *((uint16_t*)&b[2]) = htons(req_port);
        *((uint32_t*)&b[4]) = req_addr.s_addr;
        
        writebuf()->append(b,8);        
        state_ = REQRES_SENT;
        
        return 8;
    }
    
    return 0;
}

void socksServerCX::pre_write() {
    DEB_("socksServerCX::pre_write[%s]: writebuf=%d, readbuf=%d",c_name(),writebuf()->size(),readbuf()->size());
    if(state_ == REQRES_SENT ) {
        if(writebuf()->size() == 0) {
            DIA_("socksServerCX::pre_write[%s]: all flushed, state change to HANDOFF: writebuf=%d, readbuf=%d",c_name(),writebuf()->size(),readbuf()->size());
            waiting_for_peercom(true);
            state(HANDOFF);
        }
    }
}

void socksServerCX::handle_event (baseCom *xcom) {
    // we are handling only DNS, so this is easy
    if(async_dns_socket > 0) {


        if(com()->in_idleset(async_dns_socket)) {
            INF___("handle_event: idling dns socket %d, closing", async_dns_socket);
            error(true);

            return;
        }

        // timeout is zero - we won't wait
        std::pair<DNS_Response *, int> rresp = recv_dns_response(async_dns_socket,0);
        DNS_Response* resp = rresp.first;
        int red = rresp.second;

        if(red <= 0) {
            INF___("handle_event: socket read returned %d",red);
            error(true);
            if(resp) {
                // unlikely I will get result on error, but one never knows
                delete resp;
            }
        } else {
            INF___("handle_event: OK - socket read returned %d",red);
            if(process_dns_response(resp)) {
                INFS___("handle_event: OK, done");
            } else {
                ERRS___("handle_event: processing DNS response failed.");
            }
        }


        // at any rate, we got all we need. Unmonitor, unhandle and close socket
        com()->unset_monitor(async_dns_socket);
        com()->set_poll_handler(async_dns_socket,nullptr);

        ::close(async_dns_socket);
        async_dns_socket = 0;
    } else {
        WARS___("handle_event: should not be here. Socket %d, async enabled: %d", async_dns_socket, async_dns);
    }

    //provoke proxy to act.
    com()->set_write_monitor(socket());
}

