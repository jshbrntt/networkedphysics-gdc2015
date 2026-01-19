#ifndef NETWORKED_DEMO_H
#define NETWORKED_DEMO_H

#ifdef CLIENT

#include "Demo.h"
#include "Cubes.h"
#include "Snapshot.h"

struct NetworkedDemoInternal;
struct StateUpdateMessage;

class NetworkedDemo : public Demo
{
public:

    NetworkedDemo( core::Allocator & allocator );

    ~NetworkedDemo();

    virtual bool Initialize() override;

    virtual void Shutdown() override;

    virtual void Update() override;

    virtual bool Clear() override;

    virtual void Render() override;

    virtual bool KeyEvent( int key, int scancode, int action, int mods ) override;

    virtual bool CharEvent( unsigned int code ) override;

    virtual const char * GetName() const override { return "networked"; }

    virtual int GetNumModes() const override { return 0; }

    virtual const char * GetModeDescription( int mode ) const override { return ""; }

private:

    void ProcessServerMessages();
    void ApplyStateUpdate( const StateUpdateMessage & msg );

    core::Allocator * m_allocator;
    NetworkedDemoInternal * m_internal;
};

#endif // #ifdef CLIENT

#endif // #ifndef NETWORKED_DEMO_H
