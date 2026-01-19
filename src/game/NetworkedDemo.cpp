#include "NetworkedDemo.h"

#ifdef CLIENT

#include "Global.h"
#include "GameClient.h"
#include "GameMessages.h"
#include "GameChannelStructure.h"
#include "protocol/Connection.h"
#include "protocol/ReliableMessageChannel.h"
#include <GLFW/glfw3.h>

struct SyncModeData
{
    float playout_delay = 5 * ( 1.0f / 60.0f );
};

template <typename T> struct StateJitterBuffer
{
    StateJitterBuffer( core::Allocator & allocator, const SyncModeData & mode_data )
        : state_updates( allocator, 64 )
    {
        stopped = true;
        start_time = 0.0;
        playout_delay = mode_data.playout_delay;
    }

    void AddStateUpdate( double time, uint16_t sequence, const T & state_update )
    {
        if ( stopped )
        {
            start_time = time;
            stopped = false;
        }

        auto entry = state_updates.Insert( sequence );

        if ( entry )
            memcpy( entry, &state_update, sizeof( state_update ) );
    }

    bool GetStateUpdate( double time, T & state_update )
    {
        if ( stopped )
            return false;

        time -= ( start_time + playout_delay );

        if ( time < 0 )
            return false;

        uint16_t sequence = (uint16_t) uint64_t( floor( time * 60.0f ) );

        auto entry = state_updates.Find( sequence );

        if ( entry )
        {
            memcpy( &state_update, entry, sizeof(state_update) );
            return true;
        }

        return false;
    }

    void Reset()
    {
        stopped = true;
        start_time = 0.0;
        state_updates.Reset();
    }

private:

    bool stopped;
    double start_time;
    float playout_delay;
    protocol::SequenceBuffer<T> state_updates;
};

struct NetworkedDemoInternal
{
    NetworkedDemoInternal( core::Allocator & allocator )
        : jitterBuffer( allocator, SyncModeData() )
    {
        this->allocator = &allocator;
        input = game::Input();
        for ( int i = 0; i < NumCubes; ++i )
        {
            positionError[i] = vectorial::vec3f(0,0,0);
            orientationError[i] = vectorial::quat4f(0,0,0,1);
        }
    }

    void Reset()
    {
        jitterBuffer.Reset();
        objects.Reset();
        input = game::Input();
        for ( int i = 0; i < NumCubes; ++i )
        {
            positionError[i] = vectorial::vec3f(0,0,0);
            orientationError[i] = vectorial::quat4f(0,0,0,1);
        }
    }

    core::Allocator * allocator;

    // View rendering (no local physics simulation)
    view::ObjectManager objects;
    view::Camera camera;
    view::Cubes cubes;
    CubesRender render;

    // Jitter buffer for state updates
    StateJitterBuffer<StateUpdateMessage> jitterBuffer;

    // Error smoothing
    vectorial::vec3f positionError[NumCubes];
    vectorial::quat4f orientationError[NumCubes];

    // Input handling
    game::Input input;
};

NetworkedDemo::NetworkedDemo( core::Allocator & allocator )
{
    m_allocator = &allocator;
    m_internal = nullptr;
}

NetworkedDemo::~NetworkedDemo()
{
    Shutdown();
}

bool NetworkedDemo::Initialize()
{
    if ( !m_internal )
    {
        m_internal = CORE_NEW( *m_allocator, NetworkedDemoInternal, *m_allocator );
    }
    else
    {
        m_internal->Reset();
    }

    return true;
}

void NetworkedDemo::Shutdown()
{
    if ( m_internal )
    {
        CORE_DELETE( *m_allocator, NetworkedDemoInternal, m_internal );
        m_internal = nullptr;
    }
}

void NetworkedDemo::Update()
{
    if ( !m_internal )
        return;

    // Send local input to server
    if ( global.client && global.client->GetState() == clientServer::CLIENT_STATE_CONNECTED )
    {
        global.client->SendInput( m_internal->input );
    }

    // Process incoming state messages
    ProcessServerMessages();

    // Apply state from jitter buffer
    StateUpdateMessage stateUpdate;
    if ( m_internal->jitterBuffer.GetStateUpdate( global.timeBase.time, stateUpdate ) )
    {
        ApplyStateUpdate( stateUpdate );
    }

    // Update view objects
    m_internal->objects.Update( global.timeBase.deltaTime );

    // Update camera - follow player cube (ID 1)
    view::Object * player = m_internal->objects.GetObject( 1 );
    if ( player )
    {
        vectorial::vec3f origin = player->position;
        vectorial::vec3f lookat = origin - vectorial::vec3f(0,0,1);
        vectorial::vec3f position = lookat + vectorial::vec3f(0,-11,5);
        m_internal->camera.EaseIn( lookat, position );
    }
}

void NetworkedDemo::ProcessServerMessages()
{
    if ( !global.client || global.client->GetState() != clientServer::CLIENT_STATE_CONNECTED )
        return;

    auto connection = global.client->GetConnection();
    if ( !connection )
        return;

    auto channel = (protocol::ReliableMessageChannel*) connection->GetChannel( 0 );
    if ( !channel )
        return;

    while ( auto msg = channel->ReceiveMessage() )
    {
        if ( msg->GetType() == MESSAGE_STATE_UPDATE )
        {
            auto stateMsg = (StateUpdateMessage*) msg;
            m_internal->jitterBuffer.AddStateUpdate( global.timeBase.time, stateMsg->sequence, *stateMsg );
        }

        auto channelStructure = (GameChannelStructure*) global.client->GetConfig().channelStructure;
        channelStructure->GetConfig().messageFactory->Release( msg );
    }
}

void NetworkedDemo::ApplyStateUpdate( const StateUpdateMessage & msg )
{
    // Build object updates from state message
    view::ObjectUpdate updates[MaxCubesPerMessage];

    for ( int i = 0; i < msg.num_cubes; ++i )
    {
        CubeState cube;
        msg.cube_state[i].Save( cube );

        updates[i].id = msg.cube_index[i] + 1;
        updates[i].position = cube.position;
        updates[i].orientation = cube.orientation;
        updates[i].scale = ( msg.cube_index[i] < MaxPlayers ) ?
            hypercube::PlayerCubeSize : hypercube::NonPlayerCubeSize;
        updates[i].authority = cube.interacting ? 0 : MaxPlayers;
        updates[i].visible = true;
    }

    m_internal->objects.UpdateObjects( updates, msg.num_cubes );
}

bool NetworkedDemo::Clear()
{
    if ( m_internal )
        m_internal->render.ClearScreen();
    return true;
}

void NetworkedDemo::Render()
{
    if ( !m_internal )
        return;

    m_internal->render.ResizeDisplay( global.displayWidth, global.displayHeight );

    m_internal->objects.GetRenderState( m_internal->cubes,
        m_internal->positionError, m_internal->orientationError );

    m_internal->render.BeginScene( 0, 0, global.displayWidth, global.displayHeight );
    m_internal->render.SetCamera( m_internal->camera.position,
        m_internal->camera.lookat, m_internal->camera.up );
    m_internal->render.SetLightPosition( m_internal->camera.lookat +
        vectorial::vec3f( 25.0f, -50.0f, 100.0f ) );
    m_internal->render.RenderCubes( m_internal->cubes );
    m_internal->render.RenderCubeShadows( m_internal->cubes );
    m_internal->render.RenderShadowQuad();
    m_internal->render.EndScene();
}

bool NetworkedDemo::KeyEvent( int key, int scancode, int action, int mods )
{
    if ( !m_internal )
        return false;

    if ( action == GLFW_PRESS || action == GLFW_REPEAT )
    {
        switch ( key )
        {
            case GLFW_KEY_LEFT:     m_internal->input.left = true;      return true;
            case GLFW_KEY_RIGHT:    m_internal->input.right = true;     return true;
            case GLFW_KEY_UP:       m_internal->input.up = true;        return true;
            case GLFW_KEY_DOWN:     m_internal->input.down = true;      return true;
            case GLFW_KEY_SPACE:    m_internal->input.push = true;      return true;
            case GLFW_KEY_Z:        m_internal->input.pull = true;      return true;
        }
    }
    else if ( action == GLFW_RELEASE )
    {
        switch ( key )
        {
            case GLFW_KEY_LEFT:     m_internal->input.left = false;     return true;
            case GLFW_KEY_RIGHT:    m_internal->input.right = false;    return true;
            case GLFW_KEY_UP:       m_internal->input.up = false;       return true;
            case GLFW_KEY_DOWN:     m_internal->input.down = false;     return true;
            case GLFW_KEY_SPACE:    m_internal->input.push = false;     return true;
            case GLFW_KEY_Z:        m_internal->input.pull = false;     return true;
        }
    }

    return false;
}

bool NetworkedDemo::CharEvent( unsigned int code )
{
    return false;
}

#endif // #ifdef CLIENT
