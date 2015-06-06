// Client Server Library - Copyright (c) 2008-2015, Glenn Fiedler

#ifndef PROTOCOL_CLIENT_SERVER_CONTEXT_H
#define PROTOCOL_CLIENT_SERVER_CONTEXT_H

#include "core/Core.h"
#include "network/Address.h"

namespace clientServer
{
    struct ClientServerContext
    {
        enum Dummy { ClassId = 0x12345 };

        int classId;

        struct ClientInfo
        {
            network::Address address;
            uint16_t clientId;
            uint16_t serverId;
            bool connected;

            ClientInfo()
            {
                clientId = 0;
                serverId = 0;
                connected = false;
            }
        };

        int numClients;

        ClientInfo * clientInfo;

        ClientServerContext()
        {
            numClients = 0;
            clientInfo = NULL;
        }

        void Initialize( core::Allocator & allocator, int numClients );

        void Free( core::Allocator & allocator );

        void AddClient( int clientIndex, const network::Address & address, uint16_t clientId, uint16_t serverId );

        void RemoveClient( int clientIndex );

        int FindClient( const network::Address & address ) const;

        int FindClient( const network::Address & address, uint16_t clientId ) const;

        int FindClient( const network::Address & address, uint16_t clientId, uint16_t serverId ) const;

        bool ClientPotentiallyExists( uint16_t clientId, uint16_t serverId ) const;

        int FindFreeSlot() const;
    };
}

#endif
