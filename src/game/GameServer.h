#ifndef GAME_SERVER_H
#define GAME_SERVER_H

#include "ClientServer/Server.h"
#include "GameContext.h"
#include "GamePackets.h"
#include "GameMessages.h"
#include "GameChannelStructure.h"
#include "network/BSDSocket.h"
#include "network/Simulator.h"
#include "Cubes.h"
#include "Snapshot.h"
#include "protocol/ReliableMessageChannel.h"
#include <algorithm>

struct CubePriorityInfo
{
    int index;
    double accum;
};

struct ServerClientData
{
    int playerId = -1;
    game::Input lastInput;
    uint16_t inputSequence = 0;
    CubePriorityInfo priorityInfo[NumCubes];

    void Reset()
    {
        playerId = -1;
        lastInput = game::Input();
        inputSequence = 0;
        for ( int i = 0; i < NumCubes; ++i )
        {
            priorityInfo[i].index = i;
            priorityInfo[i].accum = 0.0;
        }
    }
};

class GameServer : public clientServer::Server
{
public:

    GameServer( const clientServer::ServerConfig & config ) : Server( config )
    {
        CORE_ASSERT( config.serverData );

        SetContext( clientServer::CONTEXT_USER, config.serverData->GetData() );

        m_gameInstance = nullptr;
        m_frame = 0;
        m_sendSequence = 0;

        for ( int i = 0; i < MaxClients; ++i )
            m_clientData[i].Reset();
    }

    ~GameServer()
    {
        if ( m_gameInstance )
        {
            CORE_DELETE( core::memory::default_allocator(), GameInstance, m_gameInstance );
            m_gameInstance = nullptr;
        }
    }

    void InitializePhysics()
    {
        auto & allocator = core::memory::default_allocator();

        game::Config game_config;
        game_config.maxObjects = CubeSteps * CubeSteps + MaxPlayers + 1;
        game_config.deactivationTime = 0.5f;
        game_config.cellSize = 2.0f;
        game_config.cellWidth = CubeSteps / game_config.cellSize + 2 * 2;
        game_config.cellHeight = game_config.cellWidth;
        game_config.activationDistance = 100.0f;

        game_config.simConfig.ERP = 0.25f;
        game_config.simConfig.CFM = 0.001f;
        game_config.simConfig.MaxIterations = 64;
        game_config.simConfig.MaximumCorrectingVelocity = 250.0f;
        game_config.simConfig.ContactSurfaceLayer = 0.01f;
        game_config.simConfig.Elasticity = 0.0f;
        game_config.simConfig.LinearDrag = 0.001f;
        game_config.simConfig.AngularDrag = 0.001f;
        game_config.simConfig.Friction = 200.0f;

        m_gameInstance = CORE_NEW( allocator, GameInstance, game_config );

        m_gameInstance->InitializeBegin();
        m_gameInstance->AddPlane( math::Vector(0,0,1), 0 );

        // Add player cubes (one per potential player)
        // Position them in a grid pattern within the activation bounds
        const float playerSpacing = 2.0f;
        const int playersPerRow = 4;
        for ( int p = 0; p < MaxPlayers; ++p )
        {
            int row = p / playersPerRow;
            int col = p % playersPerRow;
            float x = (col - playersPerRow / 2.0f + 0.5f) * playerSpacing;
            float y = (row - (MaxPlayers / playersPerRow) / 2.0f + 0.5f) * playerSpacing;
            AddPlayerCube( p, vectorial::vec3f( x, y, 10 ) );
        }

        // Add environment cubes
        const float origin = -CubeSteps / 2.0f;
        const float z = hypercube::NonPlayerCubeSize / 2.0f;
        for ( int y = 0; y < CubeSteps; ++y )
            for ( int x = 0; x < CubeSteps; ++x )
                AddEnvironmentCube( vectorial::vec3f(x+origin+0.5f,y+origin+0.5f,z) );

        m_gameInstance->InitializeEnd();

        m_gameInstance->SetFlag( game::FLAG_Push );
        m_gameInstance->SetFlag( game::FLAG_Pull );

        printf( "%.3f: Physics initialized with %d environment cubes and %d player slots\n",
            GetTime(), CubeSteps * CubeSteps, MaxPlayers );
    }

    void UpdatePhysics( float deltaTime )
    {
        if ( !m_gameInstance )
            return;

        // Apply input from all connected clients
        for ( int clientIndex = 0; clientIndex < GetConfig().maxClients; ++clientIndex )
        {
            if ( GetClientState( clientIndex ) == clientServer::SERVER_CLIENT_STATE_CONNECTED )
            {
                int playerId = m_clientData[clientIndex].playerId;
                if ( playerId >= 0 && playerId < MaxPlayers )
                {
                    m_gameInstance->SetPlayerInput( playerId, m_clientData[clientIndex].lastInput );
                }
            }
        }

        // Run physics simulation
        m_gameInstance->Update( deltaTime );
        m_frame++;
    }

    void BroadcastState()
    {
        if ( !m_gameInstance )
            return;

        // Get current physics state
        QuantizedSnapshot_HighPrecision snapshot;
        GetQuantizedSnapshot_HighPrecision( m_gameInstance, snapshot );

        // Broadcast to each connected client
        for ( int clientIndex = 0; clientIndex < GetConfig().maxClients; ++clientIndex )
        {
            if ( GetClientState( clientIndex ) == clientServer::SERVER_CLIENT_STATE_CONNECTED )
            {
                SendStateToClient( clientIndex, snapshot );
            }
        }

        m_sendSequence++;
    }

    void ProcessClientMessages()
    {
        for ( int clientIndex = 0; clientIndex < GetConfig().maxClients; ++clientIndex )
        {
            if ( GetClientState( clientIndex ) != clientServer::SERVER_CLIENT_STATE_CONNECTED )
                continue;

            auto connection = GetClientConnection( clientIndex );
            if ( !connection )
                continue;

            auto channel = (protocol::ReliableMessageChannel*) connection->GetChannel( 0 );
            if ( !channel )
                continue;

            while ( auto msg = channel->ReceiveMessage() )
            {
                if ( msg->GetType() == MESSAGE_INPUT )
                {
                    auto inputMsg = (InputMessage*) msg;

                    // Only accept if sequence is newer
                    if ( core::sequence_greater_than( inputMsg->sequence,
                            m_clientData[clientIndex].inputSequence ) )
                    {
                        m_clientData[clientIndex].lastInput = inputMsg->input;
                        m_clientData[clientIndex].inputSequence = inputMsg->sequence;
                    }
                }

                auto channelStructure = (GameChannelStructure*) GetConfig().channelStructure;
                channelStructure->GetConfig().messageFactory->Release( msg );
            }
        }
    }

    double GetTime() const
    {
        return GetTimeBase().time;
    }

protected:

    void AddPlayerCube( int playerId, const vectorial::vec3f & position )
    {
        hypercube::DatabaseObject object;
        math::Vector pos( position.x(), position.y(), position.z() );
        cubes::CompressPosition( pos, object.position );
        cubes::CompressOrientation( math::Quaternion(1,0,0,0), object.orientation );
        object.enabled = 1;
        object.session = 0;
        object.player = 1;
        activation::ObjectId id = m_gameInstance->AddObject( object, position.x(), position.y() );
        m_gameInstance->DisableObject( id );
    }

    void AddEnvironmentCube( const vectorial::vec3f & position )
    {
        hypercube::DatabaseObject object;
        math::Vector pos( position.x(), position.y(), position.z() );
        cubes::CompressPosition( pos, object.position );
        cubes::CompressOrientation( math::Quaternion(1,0,0,0), object.orientation );
        object.enabled = 0;
        object.session = 0;
        object.player = 0;
        m_gameInstance->AddObject( object, position.x(), position.y() );
    }

    void SendStateToClient( int clientIndex, const QuantizedSnapshot_HighPrecision & snapshot )
    {
        auto connection = GetClientConnection( clientIndex );
        if ( !connection )
            return;

        auto channel = (protocol::ReliableMessageChannel*) connection->GetChannel( 0 );
        if ( !channel )
            return;

        // Calculate priorities for this client
        float priority[NumCubes];
        CalculateCubePriorities( clientIndex, snapshot, priority );

        // Accumulate priorities
        ServerClientData & clientData = m_clientData[clientIndex];
        for ( int i = 0; i < NumCubes; ++i )
            clientData.priorityInfo[i].accum += (1.0f/60.0f) * priority[i];

        // Sort by priority
        CubePriorityInfo sortedPriority[NumCubes];
        memcpy( sortedPriority, clientData.priorityInfo, sizeof(sortedPriority) );
        std::sort( sortedPriority, sortedPriority + NumCubes,
            []( const CubePriorityInfo & a, const CubePriorityInfo & b ) {
                return a.accum > b.accum;
            });

        // Create state update message
        auto channelStructure = (GameChannelStructure*) GetConfig().channelStructure;
        auto messageFactory = channelStructure->GetConfig().messageFactory;
        auto msg = (StateUpdateMessage*) messageFactory->Create( MESSAGE_STATE_UPDATE );

        msg->sequence = m_sendSequence;
        msg->num_cubes = 0;

        // Fill in highest priority cubes that fit
        for ( int i = 0; i < MaxCubesPerMessage && i < NumCubes; ++i )
        {
            int cubeIndex = sortedPriority[i].index;
            msg->cube_index[msg->num_cubes] = cubeIndex;
            msg->cube_state[msg->num_cubes] = snapshot.cubes[cubeIndex];
            msg->num_cubes++;

            // Reset priority accumulator for sent cube
            clientData.priorityInfo[cubeIndex].accum = 0.0;
        }

        channel->SendMessage( msg );
    }

    void CalculateCubePriorities( int clientIndex, const QuantizedSnapshot_HighPrecision & snapshot, float * priority )
    {
        const float BasePriority = 1.0f;
        const float PlayerPriority = 1000000.0f;
        const float InteractingPriority = 100.0f;

        int clientPlayerId = m_clientData[clientIndex].playerId;

        for ( int i = 0; i < NumCubes; ++i )
        {
            priority[i] = BasePriority;

            // Player's own cube gets highest priority
            if ( clientPlayerId >= 0 && i == clientPlayerId )
                priority[i] += PlayerPriority;

            // Interacting cubes get higher priority
            if ( snapshot.cubes[i].interacting )
                priority[i] += InteractingPriority;
        }
    }

    int AssignPlayerId()
    {
        // Find first unused player ID
        bool used[MaxPlayers] = { false };
        for ( int i = 0; i < GetConfig().maxClients; ++i )
        {
            int pid = m_clientData[i].playerId;
            if ( pid >= 0 && pid < MaxPlayers )
                used[pid] = true;
        }
        for ( int i = 0; i < MaxPlayers; ++i )
        {
            if ( !used[i] )
                return i;
        }
        return -1;
    }

    void OnClientStateChange( int clientIndex, clientServer::ServerClientState previous,
                              clientServer::ServerClientState current ) override
    {
        printf( "%.3f: Client %d state change: %s -> %s\n",
            GetTime(), clientIndex,
            GetServerClientStateName( previous ),
            GetServerClientStateName( current ) );

        if ( current == clientServer::SERVER_CLIENT_STATE_CONNECTED )
        {
            // Assign player ID to newly connected client
            int playerId = AssignPlayerId();
            m_clientData[clientIndex].Reset();
            m_clientData[clientIndex].playerId = playerId;

            if ( playerId >= 0 && m_gameInstance )
            {
                m_gameInstance->OnPlayerJoined( playerId );
                m_gameInstance->SetPlayerFocus( playerId, playerId + 1 );
                printf( "%.3f: Client %d assigned player %d\n", GetTime(), clientIndex, playerId );
            }
        }
        else if ( previous == clientServer::SERVER_CLIENT_STATE_CONNECTED )
        {
            // Client disconnected - release player ID
            int playerId = m_clientData[clientIndex].playerId;
            if ( playerId >= 0 && m_gameInstance )
            {
                m_gameInstance->OnPlayerLeft( playerId );
                printf( "%.3f: Client %d released player %d\n", GetTime(), clientIndex, playerId );
            }
            m_clientData[clientIndex].Reset();
        }
    }

    void OnClientDataReceived( int clientIndex, const protocol::Block & block ) override
    {
        printf( "%.3f: Client %d received client data: %d bytes\n", GetTime(), clientIndex, block.GetSize() );
    }

    void OnClientTimedOut( int clientIndex ) override
    {
        printf( "%.3f: Client %d timed out\n", GetTime(), clientIndex );
    }

private:
    GameInstance * m_gameInstance;
    uint32_t m_frame;
    uint16_t m_sendSequence;
    ServerClientData m_clientData[MaxClients];
};

GameServer * CreateGameServer( core::Allocator & allocator, int serverPort, int maxClients )
{
    auto packetFactory = CORE_NEW( allocator, GamePacketFactory, allocator );

    auto messageFactory = CORE_NEW( allocator, GameMessageFactory, allocator );

    auto channelStructure = CORE_NEW( allocator, GameChannelStructure, *messageFactory );

    network::BSDSocketConfig bsdSocketConfig;
    bsdSocketConfig.port = serverPort;
    bsdSocketConfig.maxPacketSize = 1200;
    bsdSocketConfig.packetFactory = packetFactory;
    bsdSocketConfig.ipv6 = false;
    auto networkInterface = CORE_NEW( allocator, network::BSDSocket, bsdSocketConfig );

    network::SimulatorConfig networkSimulatorConfig;
    networkSimulatorConfig.packetFactory = packetFactory;
    auto networkSimulator = CORE_NEW( allocator, network::Simulator, networkSimulatorConfig );

    const int serverDataSize = sizeof(GameContext) + 10 * 1024 + 11;
    auto serverData = CORE_NEW( allocator, protocol::Block, allocator, serverDataSize );
    {
        uint8_t * data = serverData->GetData();
        for ( int i = 0; i < serverDataSize; ++i )
            data[i] = ( 10 + i ) % 256;

        auto gameContext = (GameContext*) data;
        gameContext->value_min = -1 - ( rand() % 100000000 );
        gameContext->value_max = rand() % 1000000000;
    }

    clientServer::ServerConfig serverConfig;
    serverConfig.serverData = serverData;
    serverConfig.maxClients = maxClients;
    serverConfig.channelStructure = channelStructure;
    serverConfig.networkInterface = networkInterface;
    serverConfig.networkSimulator = networkSimulator;

    return CORE_NEW( allocator, GameServer, serverConfig );
}

void DestroyGameServer( core::Allocator & allocator, GameServer * server )
{
    CORE_ASSERT( server );

    clientServer::ServerConfig config = server->GetConfig();

    typedef network::Interface NetworkInterface;
    typedef network::Simulator NetworkSimulator;

    CORE_DELETE( allocator, GameServer, server );
    CORE_DELETE( allocator, ChannelStructure, config.channelStructure );
    CORE_DELETE( allocator, NetworkInterface, config.networkInterface );
    CORE_DELETE( allocator, NetworkSimulator, config.networkSimulator );
}

#endif // #ifndef GAME_SERVER_H
