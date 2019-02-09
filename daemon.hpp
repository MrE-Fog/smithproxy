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

#ifndef __DAEMON_HPP
#define __DAEMON_HPP

#include <string>

#define PID_FILE_DEFAULT "/var/run/smithproxy.0.pid"

extern "C" std::string PID_FILE;
extern "C" void daemon_set_tenant(const std::string& name, const std::string& tenant_id);

extern "C" void daemonize(void);
extern "C" void daemon_write_pidfile(void);
extern "C" bool daemon_exists_pidfile(void);
extern "C" void daemon_unlink_pidfile(void);
extern "C" int daemon_get_limit_fd();
extern "C" void daemon_set_limit_fd(int max);
extern "C" void set_signal(unsigned int sig, void (*sig_handler)(int));
extern "C" void set_daemon_signals(void (*terminate_handler)(int),void (*reload_handler)(int));
extern "C" void set_crashlog(const char* file);

#endif