#include "Memory.h"
#include "Connection.h"
#include "TestPackets.h"

using namespace protocol;

class FakeChannel : public ChannelAdapter 
{
public:
    FakeChannel() {}
};

class FakeChannelStructure : public ChannelStructure
{
public:
    FakeChannelStructure()
    {
        AddChannel( "fake channel", [] { return new FakeChannel(); }, [] { return nullptr; } );
        Lock();
    }
};

void test_connection()
{
    printf( "test_connection\n" );

    memory::initialize();
    {
        FakeChannelStructure channelStructure;

        TestPacketFactory packetFactory( &channelStructure );

        ConnectionConfig connectionConfig;
        connectionConfig.packetType = PACKET_CONNECTION;
        connectionConfig.maxPacketSize = 4 * 1024;
        connectionConfig.packetFactory = &packetFactory;
        connectionConfig.channelStructure = &channelStructure;

        Connection connection( connectionConfig );

        const int NumAcks = 100;

        for ( int i = 0; i < NumAcks*2; ++i )
        {
            auto packet = connection.WritePacket();

            assert( packet );

            connection.ReadPacket( packet );

            delete packet;

            if ( connection.GetCounter( CONNECTION_COUNTER_PACKETS_ACKED ) >= NumAcks )
                break;
        }

        assert( connection.GetCounter( CONNECTION_COUNTER_PACKETS_ACKED ) == NumAcks );
        assert( connection.GetCounter( CONNECTION_COUNTER_PACKETS_WRITTEN ) == NumAcks + 1 );
        assert( connection.GetCounter( CONNECTION_COUNTER_PACKETS_READ ) == NumAcks + 1 );
        assert( connection.GetCounter( CONNECTION_COUNTER_PACKETS_DISCARDED ) == 0 );
    }
    memory::shutdown();
}

class AckChannel : public ChannelAdapter
{
public:

    std::vector<bool> & ackedPackets;

    AckChannel( std::vector<bool> & _ackedPackets )
        : ackedPackets( _ackedPackets ) {}

    bool ProcessData( uint16_t sequence, ChannelData * data )
    {
        return rand() % 10 != 0;
    }

    void ProcessAck( uint16_t ack )
    {
//        printf( "acked %d\n", (int) ack );
        ackedPackets[ack] = true;
    }
};

class AckChannelStructure : public ChannelStructure
{
public:
    AckChannelStructure( std::vector<bool> & ackedPackets )
    {
        AddChannel( "ack channel", [&ackedPackets] { return new AckChannel( ackedPackets ); }, [] { return nullptr; } );
        Lock();
    }
};

void test_acks()
{
    printf( "test_acks\n" );

    memory::initialize();
    {
        const int NumIterations = 10*1024;

        std::vector<bool> receivedPackets;
        std::vector<bool> ackedPackets;

        receivedPackets.resize( NumIterations );
        ackedPackets.resize( NumIterations );

        AckChannelStructure channelStructure( ackedPackets );

        TestPacketFactory packetFactory( &channelStructure );

        ConnectionConfig connectionConfig;
        connectionConfig.packetType = PACKET_CONNECTION;
        connectionConfig.maxPacketSize = 4 * 1024;
        connectionConfig.packetFactory = &packetFactory;
        connectionConfig.channelStructure = &channelStructure;

        Connection connection( connectionConfig );

        for ( int i = 0; i < NumIterations; ++i )
        {
            auto packet = connection.WritePacket();

            assert( packet );

            if ( rand() % 100 == 0 )
            {
                connection.ReadPacket( packet );

                if ( packet )
                {
                    uint16_t sequence = packet->sequence;

        //            printf( "received %d\n", (int)sequence );

                    receivedPackets[sequence] = true;
                }
            }

            delete packet;
        }

        int numAckedPackets = 0;
        int numReceivedPackets = 0;
        for ( int i = 0; i < NumIterations; ++i )
        {
            if ( ackedPackets[i] )
                numAckedPackets++;

            if ( receivedPackets[i] )
                numReceivedPackets++;

            // an acked packet *must* have been received
            if ( ackedPackets[i] && !receivedPackets[i] )
                assert( false );
        }

        assert( numAckedPackets > 0 );
        assert( numReceivedPackets >= numAckedPackets );

    //    printf( "%d packets received, %d packets acked\n", numReceivedPackets, numAckedPackets );
    }
    memory::shutdown();
}
