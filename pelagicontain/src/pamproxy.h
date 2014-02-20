/*
 *   Copyright (C) 2014 Pelagicore AB
 *   All rights reserved.
 */
#ifndef __dbusxx__pamproxy_h__PROXY_MARSHAL_H
#define __dbusxx__pamproxy_h__PROXY_MARSHAL_H

#include <dbus-c++/dbus.h>
#include <cassert>

namespace com {
namespace pelagicore {

class PAM_proxy
: public ::DBus::InterfaceProxy
{
public:

    PAM_proxy()
    : ::DBus::InterfaceProxy("com.pelagicore.PAM")
    {
    }

public:

    /* properties exported by this interface */
public:

    /* methods exported by this interface,
     * this functions will invoke the corresponding methods on the remote objects
     */
    std::string Echo(const std::string& argument)
    {
        ::DBus::CallMessage call;
        ::DBus::MessageIter wi = call.writer();

        wi << argument;
        call.member("Echo");
        ::DBus::Message ret = invoke_method (call);
        ::DBus::MessageIter ri = ret.reader();

        std::string argout;
        ri >> argout;
        return argout;
    }

    void RegisterClient(const std::string& cookie, const std::string& appId)
    {
        ::DBus::CallMessage call;
        ::DBus::MessageIter wi = call.writer();

        wi << cookie;
        wi << appId;
        call.member("RegisterClient");

	// This method call has been changed manually to be non-blocking
	invoke_method_noreply (call);
    }

    void UnregisterClient(const std::string& appId)
    {
        ::DBus::CallMessage call;
        ::DBus::MessageIter wi = call.writer();

        wi << appId;
        call.member("UnregisterClient");

	// This method call has been changed manually to be non-blocking
	invoke_method_noreply (call);
    }

    void UpdateFinished(const std::string& appId)
    {
        ::DBus::CallMessage call;
        ::DBus::MessageIter wi = call.writer();

        wi << appId;
        call.member("UpdateFinished");

	// This method call has been changed manually to be non-blocking
	invoke_method_noreply (call);
    }


public:

    /* signal handlers for this interface
     */

private:

    /* unmarshalers (to unpack the DBus message before calling the actual signal handler)
     */
};

} } 
#endif //__dbusxx__pamproxy_h__PROXY_MARSHAL_H