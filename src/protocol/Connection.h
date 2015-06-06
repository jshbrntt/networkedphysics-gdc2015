// Protocol Library - Copyright (c) 2008-2015, Glenn Fiedler

#ifndef PROTOCOL_CONNECTION_H
#define PROTOCOL_CONNECTION_H

#include "core/Core.h"
#include "protocol/Stream.h"
#include "protocol/Channel.h"
#include "protocol/SequenceBuffer.h"
#include "protocol/ConnectionPacket.h"

namespace protocol
{
    class PacketFactory;
    class ChannelStructure;

    struct ConnectionConfig
    {
        core::Allocator * allocator;
        int packetType;
        int maxPacketSize;
        int slidingWindowSize;
        PacketFactory * packetFactory;
        ChannelStructure * channelStructure;
        const void ** context;

        ConnectionConfig()
        {
            allocator = NULL;
            packetType = protocol::CONNECTION_PACKET;
            maxPacketSize = 1024;
            slidingWindowSize = 256;
            packetFactory = NULL;
            channelStructure = NULL;
            context = NULL;
        }
    };

    struct SentPacketData { uint8_t acked; };
    struct ReceivedPacketData {};
    typedef SequenceBuffer<SentPacketData> SentPackets;
    typedef SequenceBuffer<ReceivedPacketData> ReceivedPackets;

    class Connection
    {
        ConnectionError m_error;

        const ConnectionConfig m_config;                            // const configuration data

        core::Allocator * m_allocator;                              // allocator for allocations matching life cycle of object.

        core::TimeBase m_timeBase;                                  // network time base
        SentPackets * m_sentPackets;                                // sliding window of recently sent packets
        ReceivedPackets * m_receivedPackets;                        // sliding window of recently received packets
        int m_numChannels;                                          // cached number of channels
        Channel * m_channels[MaxChannels];                          // array of channels created according to channel structure
        uint64_t m_counters[CONNECTION_COUNTER_NUM_COUNTERS];       // counters for unit testing, stats etc.

    public:

        Connection( const ConnectionConfig & config );

        ~Connection();

        Channel * GetChannel( int index );

        void Reset();

        void Update( const core::TimeBase & timeBase );

        ConnectionError GetError() const;

        int GetChannelError( int channelIndex ) const;

        const core::TimeBase & GetTimeBase() const;

        ConnectionPacket * WritePacket();

        bool ReadPacket( ConnectionPacket * packet );

        uint64_t GetCounter( int index ) const;

        void ProcessAcks( uint16_t ack, uint32_t ack_bits );

        void PacketAcked( uint16_t sequence );
    };
}

#endif
