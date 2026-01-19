#ifndef GAME_MESSAGES_H
#define GAME_MESSAGES_H

#include "protocol/Message.h"
#include "protocol/BlockMessage.h"
#include "protocol/MessageFactory.h"
#include "ClientServer/ClientServerEnums.h"
#include "GameContext.h"
#include "cubes/Game.h"
#include "Snapshot.h"

enum MessageType
{
    MESSAGE_BLOCK = protocol::BlockMessageType,
    MESSAGE_TEST,
    MESSAGE_INPUT,
    MESSAGE_STATE_UPDATE,
    NUM_MESSAGE_TYPES
};

static const int MaxCubesPerMessage = 63;

inline int GetNumBitsForMessage( uint16_t sequence )
{
    static int messageBitsArray[] = { 1, 320, 120, 4, 256, 45, 11, 13, 101, 100, 84, 95, 203, 2, 3, 8, 512, 5, 3, 7, 50 };
    const int modulus = sizeof( messageBitsArray ) / sizeof( int );
    const int index = sequence % modulus;
    return messageBitsArray[index];
}

struct TestMessage : public protocol::Message
{
    TestMessage() : Message( MESSAGE_TEST )
    {
        sequence = 0;
        value = 0;
    }

    PROTOCOL_SERIALIZE_OBJECT( stream )
    {
        serialize_bits( stream, sequence, 16 );

        int numBits = GetNumBitsForMessage( sequence ) / 2;
        int numWords = numBits / 32;
        uint32_t dummy = 0;
        for ( int i = 0; i < numWords; ++i )
            serialize_bits( stream, dummy, 32 );
        int numRemainderBits = numBits - numWords * 32;
        if ( numRemainderBits > 0 )
            serialize_bits( stream, dummy, numRemainderBits );

        auto gameContext = (const GameContext*) stream.GetContext( clientServer::CONTEXT_USER );
        CORE_ASSERT( gameContext );
        serialize_int( stream, value, gameContext->value_min, gameContext->value_max );

        CORE_CHECK( serialize_check( stream, 0xDEADBEEF ) );
    }

    uint16_t sequence;
    int value;
};

struct InputMessage : public protocol::Message
{
    InputMessage() : Message( MESSAGE_INPUT )
    {
        sequence = 0;
        input = game::Input();
    }

    PROTOCOL_SERIALIZE_OBJECT( stream )
    {
        serialize_bits( stream, sequence, 16 );
        serialize_bool( stream, input.left );
        serialize_bool( stream, input.right );
        serialize_bool( stream, input.up );
        serialize_bool( stream, input.down );
        serialize_bool( stream, input.push );
        serialize_bool( stream, input.pull );
    }

    uint16_t sequence;
    game::Input input;
};

struct StateUpdateMessage : public protocol::Message
{
    StateUpdateMessage() : Message( MESSAGE_STATE_UPDATE )
    {
        sequence = 0;
        num_cubes = 0;
    }

    PROTOCOL_SERIALIZE_OBJECT( stream )
    {
        serialize_uint16( stream, sequence );
        serialize_int( stream, num_cubes, 0, MaxCubesPerMessage );

        for ( int i = 0; i < num_cubes; ++i )
        {
            serialize_int( stream, cube_index[i], 0, NumCubes - 1 );

            serialize_int( stream, cube_state[i].position_x,
                -QuantizedPositionBoundXY_HighPrecision,
                +QuantizedPositionBoundXY_HighPrecision - 1 );
            serialize_int( stream, cube_state[i].position_y,
                -QuantizedPositionBoundXY_HighPrecision,
                +QuantizedPositionBoundXY_HighPrecision - 1 );
            serialize_int( stream, cube_state[i].position_z,
                0, QuantizedPositionBoundZ_HighPrecision - 1 );

            serialize_object( stream, cube_state[i].orientation );

            bool at_rest = Stream::IsWriting ? cube_state[i].AtRest() : false;
            serialize_bool( stream, at_rest );

            if ( !at_rest )
            {
                serialize_int( stream, cube_state[i].linear_velocity_x,
                    -QuantizedLinearVelocityBound_HighPrecision,
                    +QuantizedLinearVelocityBound_HighPrecision - 1 );
                serialize_int( stream, cube_state[i].linear_velocity_y,
                    -QuantizedLinearVelocityBound_HighPrecision,
                    +QuantizedLinearVelocityBound_HighPrecision - 1 );
                serialize_int( stream, cube_state[i].linear_velocity_z,
                    -QuantizedLinearVelocityBound_HighPrecision,
                    +QuantizedLinearVelocityBound_HighPrecision - 1 );
                serialize_int( stream, cube_state[i].angular_velocity_x,
                    -QuantizedAngularVelocityBound_HighPrecision,
                    +QuantizedAngularVelocityBound_HighPrecision - 1 );
                serialize_int( stream, cube_state[i].angular_velocity_y,
                    -QuantizedAngularVelocityBound_HighPrecision,
                    +QuantizedAngularVelocityBound_HighPrecision - 1 );
                serialize_int( stream, cube_state[i].angular_velocity_z,
                    -QuantizedAngularVelocityBound_HighPrecision,
                    +QuantizedAngularVelocityBound_HighPrecision - 1 );
            }
            else if ( Stream::IsReading )
            {
                cube_state[i].linear_velocity_x = 0;
                cube_state[i].linear_velocity_y = 0;
                cube_state[i].linear_velocity_z = 0;
                cube_state[i].angular_velocity_x = 0;
                cube_state[i].angular_velocity_y = 0;
                cube_state[i].angular_velocity_z = 0;
            }

            serialize_bool( stream, cube_state[i].interacting );
        }
    }

    uint16_t sequence;
    int num_cubes;
    int cube_index[MaxCubesPerMessage];
    QuantizedCubeState_HighPrecision cube_state[MaxCubesPerMessage];
};

class GameMessageFactory : public protocol::MessageFactory
{
    core::Allocator * m_allocator;

public:

    GameMessageFactory( core::Allocator & allocator )
        : MessageFactory( allocator, NUM_MESSAGE_TYPES )
    {
        m_allocator = &allocator;
    }

protected:

    protocol::Message * CreateInternal( int type )
    {
        switch ( type )
        {
            case MESSAGE_BLOCK:         return CORE_NEW( *m_allocator, protocol::BlockMessage );
            case MESSAGE_TEST:          return CORE_NEW( *m_allocator, TestMessage );
            case MESSAGE_INPUT:         return CORE_NEW( *m_allocator, InputMessage );
            case MESSAGE_STATE_UPDATE:  return CORE_NEW( *m_allocator, StateUpdateMessage );
            default:
                return nullptr;
        }
    }
};

#endif // #ifndef GAME_MESSAGES_H
