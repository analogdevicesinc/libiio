// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - C++ API usage
 *
 * This example libiio program shows the usage of the C++ API for enumerating
 * devices, channels and attributes.
 *
 * Copyright (c) 2023, DIFITEC GmbH
 * Author: Tilman Blumhagen <tilman.blumhagen@difitec.de>
 */

#include <iiopp.h>

#include <iostream>
#include <iomanip>

using namespace iiopp;
using namespace std;

string get(IAttr const & att)
{
    char value[1024] = {0}; // Flawfinder: ignore
    att.read(value, sizeof(value)); // Flawfinder: ignore
    return value;
}

void enumerateIioEntities()
{
    cout << boolalpha;

    shared_ptr<Context> context = create_default_context();

    for (Device device : *context)
    {
        cout << "Device:" << endl;
        cout << "  id: " << quoted(string(device.id())) << endl;

        cout << "  name: ";
        if (auto name = device.name())
            cout << quoted(string(*name));
        cout << endl;

        cout << "  is trigger: " << device.is_trigger() << endl;

        for (auto att : device.attrs)
            cout << "  attribute " << att.name() << " = " << quoted(get(att)) << endl;

        for (auto att : device.debug_attrs)
            cout << "  debug attribute " << att.name() << " = " << quoted(get(att)) << endl;

        for (auto att : device.buffer_attrs)
            cout << "  buffer attribute " << att.name() << " = " << quoted(get(att)) << endl;

        for (Channel channel : device)
        {
            cout << "  Channel: " << channel.id() << endl;

            cout << "    name: ";
            if (auto name = channel.name())
                cout << quoted(string(*name));
            cout << endl;

            cout << "    is output: " << channel.is_output() << endl;
            cout << "    is enabled: " << channel.is_enabled() << endl;

            for (auto att : channel.attrs)
                cout << "    attribute " << quoted(att.name().c_str()) << " = " << quoted(get(att)) << endl;
        }
    }

    shared_ptr<ScanContext> scanCtx = create_scan_context({}, 0);
    shared_ptr<ScanContext::InfoList> infoList = scanCtx->info_list();

    cout << "scan context info list:" << endl;
    for (ContextInfo info : *infoList)
    {
        cout << "  uri: " << quoted(info.uri().c_str()) << endl;
        cout << "  description: " << quoted(info.description().c_str()) << endl;
    }

    cout << "scan block:" << endl;
    shared_ptr<ScanBlock> blk = create_scan_block({}, 0);
    for (ContextInfo info : *blk)
    {
        cout << "  uri: " << quoted(info.uri().c_str()) << endl;
        cout << "  description: " << quoted(info.description().c_str()) << endl;
    }
}

int main(int argc, char ** argv)
{
    try
    {
        enumerateIioEntities();
    }
    catch (error & e)
    {
        cerr << "ERROR " << e.code().value() << ": " << e.what() << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
