#include <utility>

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

#ifndef ADDROBJ_HPP_
#define ADDROBJ_HPP_

#include <ext/cidr.hpp>
#include <display.hpp>
#include <logger.hpp>
#include <sobject.hpp>

class AddressObject : public socle::sobject {
public:
    virtual bool match(CIDR* c) = 0;
    std::string to_string(int=iINF) override = 0;
    ~AddressObject() override = default;

    std::string prof_name;
};


class CidrAddress : public AddressObject {
public:
    explicit CidrAddress(CIDR* c) : c_(c) { }
    CIDR* cidr() { return c_; }

    int contains(CIDR *other);
    bool match(CIDR* c) override { return (contains(c) >= 0); };
    bool ask_destroy() override { return false; };
    
    std::string to_string(int verbosity=iINF) override {
        char* temp = cidr_to_str(c_);

        std::string ret = string_format("CidrAddress: %s",temp);

        if(! prof_name.empty()) {
            ret += string_format(" (name=%s)", prof_name.c_str());
        }

        delete temp; return ret;
    }
    
    
    ~CidrAddress() override { cidr_free(c_); };
protected:
    CIDR* c_;

DECLARE_C_NAME("CidrAddress");
};

class FqdnAddress : public AddressObject {
public:
    explicit FqdnAddress(std::string s) : fqdn_(std::move(s)) { }
    std::string fqdn() const { return fqdn_; }
    
    bool match(CIDR* c) override;
    bool ask_destroy() override { return false; };
    std::string to_string(int verbosity=iINF) override;
protected:
    std::string fqdn_;

DECLARE_C_NAME("FqdnAddress");
};

#endif //ADDROBJ_HPP_