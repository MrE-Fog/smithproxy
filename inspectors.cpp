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

#include <inspectors.hpp>
#include <mitmhost.hpp>

DEFINE_LOGGING(DNS_Inspector)

std::string Inspector::remove_redundant_dots(std::string orig) {
    std::string norm;  

    int dot_mark = 1;
    for(unsigned int i = 0; i < orig.size(); i++) {
        if(orig[i] == '.') {
            if(dot_mark > 0) continue;
            
            norm +=orig[i];
            dot_mark++;
        } else {
            dot_mark = 0;
            norm +=orig[i];
        }
    }
    if(dot_mark > 0) {
        norm = norm.substr(0,norm.size()-dot_mark);
    }
    
    return norm;
}

std::vector< std::string > Inspector::split(std::string str, unsigned char delimiter) {
    std::vector<std::string> ret;
    
    bool empty_back = true;
    for(unsigned int i = 0; i < str.size() ; i++) {
        if(i == 0)
            ret.push_back("");
            
        if(str[i] == delimiter) {
            
            if(ret.size() > 0)
                if(ret.back().size() == 0) ret.pop_back();

            ret.push_back("");
            empty_back = true;
        } else {
            ret.back()+= str[i];
            empty_back = false;
        }
    }
    
    if(empty_back) {
        ret.pop_back();
    }
    
    return ret;    
}

std::pair<std::string,std::string> Inspector::split_fqdn_subdomain(std::string& fqdn) {
        std::string topdom;
        std::string subdom;
        std::vector<std::string> dotted_fqdn = split(fqdn,'.');
        
        if(dotted_fqdn.size() > 2 ) {
            
            unsigned int  i = 1;
            for(auto it = dotted_fqdn.begin(); it != dotted_fqdn.end(); ++it) {
                if(i <= dotted_fqdn.size() - 2) {
                    subdom += *it;
                    
                    if(i < dotted_fqdn.size() - 2) {
                        subdom += ".";
                    }
                } else {
                    topdom += *it;
                    if(i < dotted_fqdn.size()) {
                        topdom += ".";
                    }
                }
                
                i++;
            }
        }
        
        return std::pair<std::string,std::string>(topdom,subdom);
}

std::regex DNS_Inspector::wildcard = std::regex("[^.]+\\.(.*)$");

bool DNS_Inspector::interested(AppHostCX* cx) {
    if(cx->com()->nonlocal_dst_port() == 53)
        return true;
    return false;
}

void DNS_Inspector::update(AppHostCX* cx) {
  
    std::lock_guard<std::recursive_mutex> l(DatagramCom::lock);
    
    
    duplexFlow& f = cx->flow();
    _dia("DNS_Inspector::update[%s]: stage %d start (flow size %d, last flow entry data length %d)",cx->c_name(),stage, f.flow().size(),f.flow().back().second->size());
    
    /* INIT */
    
    if(!in_progress()) {
        baseCom* com = cx->com();
        TCPCom* tcp_com = dynamic_cast<TCPCom*>(com);
        if(tcp_com) 
            is_tcp = true;
        
        in_progress(true);
    }
    
    
    std::pair<char,buffer*> cur_pos = cx->flow().flow().back();
    
    DNS_Packet* ptr = nullptr;
    buffer *xbuf = cur_pos.second;
    buffer buf = xbuf->view(0,xbuf->size());

    // check if response is already available
    DNS_Response* cached_entry = nullptr;
    
    int mem_pos = 0;
    unsigned int red = 0;
    
    if(is_tcp) {
        unsigned short data_size = ntohs(buf.get_at<unsigned short>(0));
        if(buf.size() < data_size) {
            _dia("DNS_Inspector::update[%s]: not enough DNS data in TCP stream: expected %d, but having %d. Waiting to more.",cx->c_name(), data_size, buf.size());
            return;
        }
        red += 2;
    }
    
    int mem_len = buf.size();
    switch(cur_pos.first)  {
        case 'r':
            stage = 0;
            for(unsigned int it = 0; red < buf.size() && it < 10; it++) {
                ptr = new DNS_Request();
                buffer cur_buf = buf.view(red,buf.size()-red);
                int cur_red = ptr->load(&cur_buf);
                
                // because of non-standard return value from above load(), we need to adjust red bytes manually
                if(cur_red == 0) { cur_red = cur_buf.size(); }
                
                _dia("DNS_Inspector::update[%s]: red  %d, load returned %d", cx->c_name(), red, cur_red);
                _deb("DNS_Inspector::update[%s]: flow: %s", cx->c_name(), cx->flow().hr().c_str());
                
                // on success write to requests_
                if(cur_red >= 0) {
                    red += cur_red;
                    
                    if(requests_[ptr->id()] != nullptr) {
                        _not("DNS_Inspector::update[%s]: detected re-sent request",cx->c_name());
                        delete requests_[ptr->id()];
                        requests_.erase(ptr->id());
                    }
                    
                    _dia("DNS_Inspector::update[%s]: adding key 0x%x red=%d, buffer_size=%d, ptr=0x%x",cx->c_name(),ptr->id(),red,cur_buf.size(),ptr);
                    requests_[ptr->id()] = (DNS_Request*)ptr;
                    
                    _deb("DNS_Inspector::update[%s]: this 0x%x, requests size %d",cx->c_name(),this, requests_.size());
                    
                    cx->idle_delay(30);
                } else {
                    red = 0;
                    delete ptr;
                    ptr = (DNS_Packet*)0xCABA1A;
                    ERR_("BUG CAUGHT: buffer:\n%s",hex_dump(cur_buf).c_str());
                }
                
                // on failure or last data exit loop
                if(cur_red <= 0) {
                    _dia("DNS_Inspector::update[%s]: finishing reading from buffers: red=%d, buffer_size=%d",cx->c_name(),red,cur_buf.size());
                    break;
                }
            }
            
            if(ptr == (DNS_Packet*)0xCABA1A) {
	       ERRS_("BUG CAUGHT.");
	       
	       
	       goto fail;
	    }
            
            if(opt_cached_responses && ( ((DNS_Request*)ptr)->question_type_0() == A || ((DNS_Request*)ptr)->question_type_0() == AAAA ) ) {
                std::scoped_lock<std::recursive_mutex> l_(DNS::get_dns_lock());

                cached_entry = DNS::get_dns_cache().get(ptr->question_str_0());
                if(cached_entry != nullptr) {
                    _dia("DNS answer for %s is already in the cache",cached_entry->question_str_0().c_str());

                    
                    if(cached_entry->cached_packet != nullptr) {
                        
                        // do TTL check
                        _dia("cached entry TTL check");
                        
                        time_t now = time(nullptr);
                        bool ttl_check = true;
                        
                        for(auto idx: cached_entry->answer_ttl_idx) {
                            uint32_t ttl = ntohl(cached_entry->cached_packet->get_at<uint32_t>(idx));
                            _deb("cached response ttl byte index %d value %d",idx,ttl);
                            if(now > ttl + cached_entry->loaded_at) {
                                _deb("  %ds -- expired", now - (ttl + cached_entry->loaded_at));
                                ttl_check = false;
                            } else {
                                _deb("  %ds left to expiry", (ttl + cached_entry->loaded_at) - now);
                            }
                        }
                    
                        if(ttl_check) {
                            verdict(CACHED);
                            // this  will copy packet to our cached response
                            if(cached_response == nullptr) 
                                    cached_response = new buffer();
                            
                            cached_response->clear();
                            cached_response->append(cached_entry->cached_packet->data(),cached_entry->cached_packet->size());
                            cached_response_id = ptr->id();
                            cached_response_ttl_idx = cached_entry->answer_ttl_idx;
                            cached_response_decrement = now - cached_entry->loaded_at;
                        
                            _dia("cached entry TTL check: OK");
                            _deb("cached response prepared: size=%d, setting overwrite id=%d",cached_response->size(),cached_response_id);
                        } else {
                            _dia("cached entry TTL check: failed");
                        }

                    }
                } else {
                    _dia("DNS answer for %s is not in cache",ptr->question_str_0().c_str());
                }
            }
            
            break;
        case 'w':
            stage = 1;
            for(unsigned int it = 0; red < buf.size() && it < 10; it++) {
                ptr = new DNS_Response();
                
                buffer cur_buf = buf.view(red,buf.size()-red);
                int cur_red = ptr->load(&cur_buf);
                
                
                if(cur_red >= 0) {
                    if(opt_cached_responses) {
                        if(((DNS_Response*)ptr)->cached_packet != nullptr) {
                            delete ((DNS_Response*)ptr)->cached_packet;
                        }
                        ((DNS_Response*)ptr)->cached_packet = new buffer();
                        if(cur_red == 0) {
                            ((DNS_Response*)ptr)->cached_packet->append(cur_buf.data(),cur_buf.size());
                        } else {
                            ((DNS_Response*)ptr)->cached_packet->append(cur_buf.data(),cur_red);
                        }
                        
                        _deb("caching response packet: size=%d",((DNS_Response*)ptr)->cached_packet->size());
                    }
                    
                    mem_pos += cur_red;
                    red = cur_red;
                    
                    _dia("DNS_Inspector::update[%s]: loaded new response (at %d size %d out of %d)",cx->c_name(),red,mem_pos,mem_len);
                    if (!validate_response((DNS_Response*)ptr)) {
                        // invalid, delete

                        cx->writebuf()->clear();
                        cx->error(true);
                        _war("DNS inspection: cannot find corresponding DNS request id 0x%x: dropping connection.",ptr->id());
                        delete ptr;
                    }
                    else {
                        // DNS response is valid
                        responses_ ++;

                        _dia("DNS_Inspector::update[%s]: valid response",cx->c_name());

                        if(store((DNS_Response*)ptr)) {
                            stored_ = true;
                            // DNS response is interesting (A record present) - we stored it , ptr is VALID
                            _dia("DNS_Inspector::update[%s]: contains interesting info, stored",cx->c_name());
                            
                        } else {
                            delete ptr;
                            ptr = nullptr;
                            
                            _dia("DNS_Inspector::update[%s]: no interesting info there, deleted",cx->c_name());
                        }
                        
                        if(is_tcp)
                            cx->idle_delay(30);
                        else
                            cx->idle_delay(1);  
                    }
                } else {
                    red = 0;
                    delete ptr;
                }
                
                // on failure or last data exit loop
                if(red <= 0) break;
            }
            break;
    }
    
    fail:
    
    _dia("DNS_Inspector::update[%s]: stage %d end (flow size %d)",cx->c_name(),stage, f.flow().size());
}




bool DNS_Inspector::store(DNS_Response* ptr) {
    bool is_a_record = true;

    std::string ip = ptr->answer_str().c_str();
    if(ip.size() > 0) {
        _not("DNS inspection: %s is at%s",ptr->question_str_0().c_str(),ip.c_str()); //ip is already prepended with " "
    }
    else {
        _dia("DNS inspection: non-A response for %s",ptr->question_str_0().c_str());
        is_a_record = false;
    }
    _dia("DNS response: %s",ptr->to_string().c_str());


    if(is_a_record) {
        std::string question = ptr->question_str_0();

        {
            std::scoped_lock<std::recursive_mutex> l_(DNS::get_dns_lock());
            DNS::get_dns_cache().set(question, ptr);
        }
            _dia("DNS_Inspector::update: %s added to cache (%d elements of max %d)", ptr->question_str_0().c_str(),
                   DNS::get_dns_cache().cache().size(), DNS::get_dns_cache().max_size());

        
        std::pair<std::string,std::string> dom_pair = split_fqdn_subdomain(question);
        _deb("topdomain = %s, subdomain = %s",dom_pair.first.c_str(), dom_pair.second.c_str());    
        
        if(dom_pair.first.size() > 0 && dom_pair.second.size() > 0) {

            std::scoped_lock<std::recursive_mutex> ll_(DNS::get_domain_lock());

            auto subdom_cache = DNS::get_domain_cache().get(dom_pair.first);
            if(subdom_cache != nullptr) {

                std::lock_guard<std::recursive_mutex> sl_(subdom_cache->getlock());
                
                _dia("Top domain cache entry found for domain %s",dom_pair.first.c_str());
                if(subdom_cache->get(dom_pair.second) != nullptr) {
                    _dia("Sub domain cache entry found for subdomain %s",dom_pair.second.c_str());
                }
                
                
                if(LEV_(DEB)) {
                    for( auto subdomain: subdom_cache->cache()) {
                        std::string  s =  subdomain.first;
                        expiring_int* i = subdomain.second;
                        
                        _deb("Sub domain cache list: entry %s",s.c_str());
                    }
                }
                
                subdom_cache->set(dom_pair.second,new expiring_int(1,28000));
            }
            
            else {
                _dia("Top domain cache entry NOT found for domain %s",dom_pair.first.c_str());
                auto* subdom_cache = DNS::make_domain_entry(dom_pair.first);
                      subdom_cache -> set(dom_pair.second, new expiring_int(1, DNS::sub_ttl));
                
                DNS::get_domain_cache().set(dom_pair.first, subdom_cache);
            }
        }
    }    
    
    return is_a_record;
}

bool DNS_Inspector::validate_response(DNS_Response* ptr) {

    unsigned int id = ptr->id();
    DNS_Request* req = find_request(id);
    if(req) {
        _dia("DNS_Inspector::validate_response: request 0x%x found",id);
        return true;
      
    } else {
        _dia("DNS_Inspector::validate_response: request 0x%x not found",id);
        _err("validating DNS response for %s failed.",ptr->to_string().c_str());
        return false; // FIXME: for debug
    }
    
    return false;
}

std::string DNS_Inspector::to_string(int verbosity) {
    std::string r = Inspector::to_string()+"\n  ";
    
    r += string_format("tcp: %d requests: %d valid responses: %d stored: %d",is_tcp,requests_.size(),responses_,stored_);
    
    return r;
}

void Inspector::apply_verdict(AppHostCX* cx) {
}

void DNS_Inspector::apply_verdict(AppHostCX* cx) {
    _deb("DNS_Inspector::apply_verdict called");
    
    //TODO: dirty, make more generic
    if(cached_response != nullptr) {
        _deb("DNS_Inspector::apply_verdict: mangling response id=%d",cached_response_id);
        *((uint16_t*)cached_response->data()) = htons(cached_response_id);
        
        for(auto i: cached_response_ttl_idx) {
            uint32_t orig_ttl = ntohl(cached_response->get_at<uint32_t>(i));
            uint32_t new_ttl = orig_ttl - cached_response_decrement;
            _deb("DNS_Inspector::apply_verdict: mangling original ttl %d to %d at index %d",orig_ttl,new_ttl,i);
            
            uint8_t* ptr = cached_response->data();
            uint32_t* ptr_ttl  = (uint32_t*)&ptr[i];
            *ptr_ttl = htonl(new_ttl);
            
        }
        
        if(! is_tcp) {
            _deb("udp encapsulation"); 
            cx->to_write(cached_response->data(), cached_response->size());
            int w = cx->write();
            _dia("DNS_Inspector::apply_verdict: %d bytes written of cached response size %d",w,cached_response->size());
        } else {
            _deb("tcp encapsulation");
            uint16_t* ptr = (uint16_t*)cached_response->data();
            uint16_t len = htons(cached_response->size());
            buffer b;
            b.append(&len,sizeof(uint16_t));
            b.append(cached_response->data(),cached_response->size());
            cx->to_write(b);
            int w = cx->write();
            
            _dia("DNS_Inspector::apply_verdict: %d bytes written of cached response size %d",w,b.size());
        }
        
    } else {
        // what to do now?
        _err("cannot send cached response, original reply not found.");
    }
}