/*
 *   Copyright (C) 2014 Pelagicore AB
 *   All rights reserved.
 */
#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include "dbusgateway.h"
#include "pelagicontain-common.h"

DBusGateway::DBusGateway(ProxyType type, const std::string &gatewayDir, const std::string &name) :
    Gateway(ID), m_type(type), m_pid(-1), m_infp(-1), m_outfp(-1),
    m_hasBeenConfigured(false), m_dbusProxyStarted(false)
{
    if (m_type == SessionProxy) {
        m_socket = gatewayDir
                + std::string("/sess_")
                + name
                + std::string(".sock");
    } else {
        m_socket = gatewayDir
                + std::string("/sys_")
                + name
                + std::string(".sock");
    }
}

DBusGateway::~DBusGateway()
{
    if (m_pid != INVALID_PID) {
        log_debug() << "Killing dbus proxy with pid " << m_pid;
        kill(m_pid, SIGTERM);
    }

    // TODO : fix dbus-proxy to delete the socket files when terminating
}

static constexpr const char *SESSION_CONFIG = "dbus-gateway-config-session";
static constexpr const char *SYSTEM_CONFIG = "dbus-gateway-config-system";

ReturnCode DBusGateway::readConfigElement(const JSonElement &element)
{
    auto sessionConfig = element[SESSION_CONFIG];
    if (sessionConfig.isValid() && sessionConfig.isArray()) {
        for(unsigned int i = 0; i < sessionConfig.elementCount(); i++) {
            JSonElement child = sessionConfig.arrayElementAt(i);
            if (m_sessionBusConfig.size() != 0) {
                m_sessionBusConfig += ",";
            }
            m_sessionBusConfig += child.dump();
        }
    }

    auto systemConfig = element[SYSTEM_CONFIG];
    if (systemConfig.isValid() && systemConfig.isArray()) {
        for(unsigned int i = 0; i < systemConfig.elementCount(); i++) {
            JSonElement child = systemConfig.arrayElementAt(i);
            if (m_systemBusConfig.size() != 0) {
                m_systemBusConfig += ",";
            }
            m_systemBusConfig += child.dump();
        }
    }

    m_hasBeenConfigured = true;
    return ReturnCode::SUCCESS;
}

bool DBusGateway::activate()
{
    std::string config = (logging::StringBuilder() << "{\"" << SESSION_CONFIG << "\"" << ": [" << m_sessionBusConfig << "]"
                                                   << " , \"" << SYSTEM_CONFIG << "\"" << ": [" << m_systemBusConfig << "]}");

    if (!m_hasBeenConfigured) {
        log_warning() << "'Activate' called on non-configured gateway " << id();
        return false;
    }

    // set DBUS ENV
    std::string variable = "DBUS_";
    if (m_type == SessionProxy) {
        variable += "SESSION";
    } else {
        variable += "SYSTEM";
    }
    variable += "_BUS_ADDRESS";

    std::string value = "unix:path=/gateways/";
    value += socketName();
    setEnvironmentVariable(variable, value);

    // Open pipe
    std::string command = "dbus-proxy ";
    command += m_socket + " " + typeString();
    log_debug() << command;
    auto code = makePopenCall(command, m_infp, m_outfp, m_pid);
    if (isError(code)) {
        log_error() << "Failed to launch " << command;
        return false;
    } else {
        log_debug() << "Started dbus-proxy: " << m_pid;
        m_dbusProxyStarted = true;
    }

    auto count = sizeof(char) * config.length();

    log_debug() << "Sending config " << config;

    auto written = write(m_infp,
            config.c_str(),
            count);

    // writing didn't work at all
    if (written == -1) {
        log_error() << "Failed to write to STDIN of dbus-proxy: " << strerror(errno);
        return false;
    }

    // writing has written exact amout of bytes
    if (written == (ssize_t)count) {
        close(m_infp);
        m_infp = -1;
        // dbus-proxy might take some time to create the bus socket
        if (isSocketCreated()) {
            log_debug() << "Found D-Bus socket: " << m_socket;
        } else {
            log_error() << "Did not find any D-Bus socket: " << m_socket;
            return false;
        }
        return true;
    }

    // something went wrong during the write
    log_error() << "Failed to write to STDIN of dbus-proxy!";

    return false;
}

bool DBusGateway::isSocketCreated() const
{
    int maxCount = 1000;
    int count = 0;
    do {
        if (count >= maxCount) {
            return false;
        }
        count++;
        usleep(1000 * 10);
    } while (access(m_socket.c_str(), F_OK) == -1);
    return true;
}

bool DBusGateway::teardown()
{
    bool success = true;

    if (m_dbusProxyStarted) {
        if (m_pid != INVALID_PID) {
            if (!makePcloseCall(m_pid, m_infp, m_outfp)) {
                log_error() << "makePcloseCall() returned error";
                success = false;
            }
        } else {
            log_error() << "Failed to close connection to dbus-proxy!";
            success = false;
        }

        if (unlink(m_socket.c_str()) == -1) {
            log_error() << "Could not remove " << m_socket << ": " << strerror(errno);
            success = false;
        }
    }

    return success;
}

const char *DBusGateway::typeString()
{
    if (m_type == SessionProxy) {
        return "session";
    } else {
        return "system";
    }
}

std::string DBusGateway::socketName()
{
    // Return the filename after stripping directory info
    std::string socket(m_socket.c_str());
    return socket.substr(socket.rfind('/') + 1);
}


ReturnCode DBusGateway::makePopenCall(const std::string &command, int &infp, int &outfp, pid_t &pid)
{
    static constexpr int READ = 0;
    static constexpr int WRITE = 1;

    int stdinfp[2], stdoutfp[2];

    if (pipe(stdinfp) != 0 || pipe(stdoutfp) != 0) {
        log_error() << "Failed to open STDIN and STDOUT to dbus-proxy";
        return ReturnCode::FAILURE;
    }

    pid = fork();

    if (pid < 0) {
        return ReturnCode::FAILURE;
    } else if (pid == 0) {
        close(stdinfp[WRITE]);
        dup2(stdinfp[READ], READ);
        close(stdoutfp[READ]);
        dup2(stdoutfp[WRITE], WRITE);

        // Set group id to the same as pid, that way we can kill the shells children on close.
        setpgid(0, 0);

        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        log_error() << "execl : " << strerror(errno);
        exit(1);
    }

    infp = stdinfp[WRITE];
    outfp = stdoutfp[READ];

    return ReturnCode::SUCCESS;
}

bool DBusGateway::makePcloseCall(pid_t pid, int infp, int outfp)
{
    if (infp > -1) {
        if (close(infp) == -1) {
            log_warning() << "Failed to close STDIN to dbus-proxy";
        }
    }
    if (outfp != -1) {
        if (close(outfp) == -1) {
            log_warning() << "Failed to close STDOUT to dbus-proxy";
        }
    }

    // the negative pid makes it kill the whole group, not only the shell
    int killed = kill(-pid, SIGKILL);

    return (killed == 0);
}