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

#ifndef _CMDSERVER_HPP_
   #define _CMDSERVER_HPP_

#include <ext/libcli/libcli.h>

#include <service/cfgapi/cfgvalue.hpp>


void debug_cli_params(struct cli_def *cli, const char *command, char *argv[], int argc);
void debug_cli_params(struct cli_def *cli, const char *command, std::vector<std::string> const& args);
void cli_loop(unsigned short port=50000);

// SL_NONE - no filtering
// SL_IO_OSBUF_NZ - sessions with non-empty OS buffers, or non-empty smithproxy write buffers
// SL_IO_EMPTY - sessions with no data received and/or sent


struct CliStrings {

    static std::vector<std::string> const& config_not_applied() {

        static std::vector<std::string> r =
                {
                        " ",
                        "  Something didn't go well: running config NOT changed !!!",
                        "    Change will be visible in show config, but not written to mapped variables",
                        "    therefore 'save config' won't write them to file.",
                        "    ",
                        "    Consider running 'execute reload'  ... sorry for inconvenience."
                };

        return r;
    }

    static void cli_print(libcli::cli_def* cli, std::vector<std::string> const& vec) {
        for( auto const& r: vec) {
            libcli::cli_print(cli, r.c_str());
        }
    }
};

int cli_show(struct cli_def *cli, const char *command, char **argv, int argc);

int cli_uni_set_cb(std::string const& confpath, struct cli_def *cli, const char *command, char *argv[], int argc);
int cli_generic_set_cb(struct cli_def *cli, const char *command, char *argv[], int argc);

void register_regular_callback(cli_def* cli);
void register_edit_command(cli_def* cli);

#endif