/*
	Networked Physics Demo
	Copyright © 2008-2015 Glenn Fiedler
	http://www.gafferongames.com/networking-for-game-programmers
*/

#include "Simulation.h"
#include <mutex>

// Define USE_JOLT_PHYSICS to use Jolt Physics, otherwise use ODE
#ifdef USE_JOLT_PHYSICS
	#include <Jolt/Jolt.h>
	#include <Jolt/RegisterTypes.h>
	#include <Jolt/Core/Factory.h>
	#include <Jolt/Core/TempAllocator.h>
	#include <Jolt/Core/JobSystemThreadPool.h>
	#include <Jolt/Physics/PhysicsSettings.h>
	#include <Jolt/Physics/PhysicsSystem.h>
	#include <Jolt/Physics/Collision/Shape/BoxShape.h>
	#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
	#include <Jolt/Physics/Body/BodyCreationSettings.h>
	#include <Jolt/Physics/Body/BodyActivationListener.h>
	#include <Jolt/Physics/Body/BodyLockInterface.h>

	JPH_SUPPRESS_WARNINGS
#else
	#define dSINGLE
	#include <ode/ode.h>
#endif

namespace cubes
{
	// simulation internal implementation

	const int MaxContacts = 16;

#ifdef USE_JOLT_PHYSICS
	// Jolt Physics Implementation
	using namespace JPH;

	// Layer that objects can be in, determines which other objects it can collide with
	namespace Layers
	{
		static constexpr ObjectLayer NON_MOVING = 0;
		static constexpr ObjectLayer MOVING = 1;
		static constexpr ObjectLayer NUM_LAYERS = 2;
	};

	// Broad phase layer interface
	class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
	{
	public:
		BPLayerInterfaceImpl()
		{
			mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayer(0);
			mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayer(1);
		}

		virtual uint GetNumBroadPhaseLayers() const override
		{
			return 2;
		}

		virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
		{
			return mObjectToBroadPhase[inLayer];
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char * GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
		{
			switch ((BroadPhaseLayer::Type)inLayer)
			{
			case 0: return "NON_MOVING";
			case 1: return "MOVING";
			default: return "INVALID";
			}
		}
#endif

	private:
		BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
	};

	// Filter for broad phase layer pairs
	class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
	{
	public:
		virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
		{
			// Everything collides with everything
			return true;
		}
	};

	// Filter for object layer pairs
	class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
	{
	public:
		virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
		{
			// Everything collides with everything
			return true;
		}
	};

	// Contact listener for tracking interactions
	class MyContactListener : public ContactListener
	{
	public:
		std::vector<std::vector<uint16_t>>* interactions;
		std::mutex interactions_mutex;

		virtual ValidateResult OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult) override
		{
			return ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		virtual void OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
		{
			uint64_t id1 = inBody1.GetUserData();
			uint64_t id2 = inBody2.GetUserData();

			// Lock mutex to protect shared interactions vector from concurrent access
			std::lock_guard<std::mutex> lock(interactions_mutex);

			if (id1 < interactions->size() && id2 < interactions->size())
			{
				(*interactions)[id1].push_back(id2);
				(*interactions)[id2].push_back(id1);
			}

			// Set friction and restitution (elasticity)
			// Note: These should ideally be set on the material, but we can override here
			// ioSettings.mCombinedFriction is already computed
			// ioSettings.mCombinedRestitution is already computed
		}

		virtual void OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
		{
		}

		virtual void OnContactRemoved(const SubShapeIDPair &inSubShapePair) override
		{
		}
	};

	struct SimulationImpl
	{
		SimulationImpl()
		{
			temp_allocator = nullptr;
			job_system = nullptr;
			physics_system = nullptr;
			contact_listener = nullptr;
			broad_phase_layer_interface = nullptr;
			object_vs_broadphase_layer_filter = nullptr;
			object_vs_object_layer_filter = nullptr;
		}

		~SimulationImpl()
		{
#ifdef USE_JOLT_PHYSICS
			// Clean up all bodies and planes before destroying the physics system
			if (physics_system)
			{
				printf("[DEBUG] SimulationImpl destructor: cleaning up bodies and planes\n");
				fflush(stdout);

				BodyInterface &body_interface = physics_system->GetBodyInterface();

				// Remove and destroy all object bodies
				printf("[DEBUG] Destroying %zu objects\n", objects.size());
				fflush(stdout);
				for (size_t i = 0; i < objects.size(); ++i)
				{
					if (objects[i].exists() && !objects[i].body_id.IsInvalid())
					{
						printf("[DEBUG] Destroying object %zu\n", i);
						fflush(stdout);
						body_interface.RemoveBody(objects[i].body_id);
						body_interface.DestroyBody(objects[i].body_id);
					}
				}

				// Remove and destroy all plane bodies
				printf("[DEBUG] Destroying %zu planes\n", planes.size());
				fflush(stdout);
				for (size_t i = 0; i < planes.size(); ++i)
				{
					if (!planes[i].IsInvalid())
					{
						printf("[DEBUG] Destroying plane %zu\n", i);
						fflush(stdout);
						body_interface.RemoveBody(planes[i]);
						body_interface.DestroyBody(planes[i]);
					}
				}
				printf("[DEBUG] Bodies and planes destroyed\n");
				fflush(stdout);
			}
#endif
			if (physics_system)
			{
				printf("[DEBUG] Deleting physics_system\n");
				fflush(stdout);
				delete physics_system;
				physics_system = nullptr;
				printf("[DEBUG] physics_system deleted\n");
				fflush(stdout);
			}
			if (contact_listener)
			{
				printf("[DEBUG] Deleting contact_listener\n");
				fflush(stdout);
				delete contact_listener;
				contact_listener = nullptr;
			}
			if (object_vs_object_layer_filter)
			{
				printf("[DEBUG] Deleting object_vs_object_layer_filter\n");
				fflush(stdout);
				delete object_vs_object_layer_filter;
				object_vs_object_layer_filter = nullptr;
			}
			if (object_vs_broadphase_layer_filter)
			{
				printf("[DEBUG] Deleting object_vs_broadphase_layer_filter\n");
				fflush(stdout);
				delete object_vs_broadphase_layer_filter;
				object_vs_broadphase_layer_filter = nullptr;
			}
			if (broad_phase_layer_interface)
			{
				printf("[DEBUG] Deleting broad_phase_layer_interface\n");
				fflush(stdout);
				delete broad_phase_layer_interface;
				broad_phase_layer_interface = nullptr;
			}
			if (job_system)
			{
				printf("[DEBUG] Deleting job_system\n");
				fflush(stdout);
				delete job_system;
				job_system = nullptr;
			}
			if (temp_allocator)
			{
				printf("[DEBUG] Deleting temp_allocator\n");
				fflush(stdout);
				delete temp_allocator;
				temp_allocator = nullptr;
			}
			printf("[DEBUG] SimulationImpl destructor complete\n");
			fflush(stdout);
		}

		TempAllocatorImpl* temp_allocator;
		JobSystemThreadPool* job_system;
		PhysicsSystem* physics_system;
		MyContactListener* contact_listener;
		BPLayerInterfaceImpl* broad_phase_layer_interface;
		ObjectVsBroadPhaseLayerFilterImpl* object_vs_broadphase_layer_filter;
		ObjectLayerPairFilterImpl* object_vs_object_layer_filter;

		struct ObjectData
		{
			BodyID body_id;
			float scale;
			float timeAtRest;

			ObjectData()
			{
				body_id = BodyID();
				scale = 1.0f;
				timeAtRest = 0.0f;
			}

			bool exists() const
			{
				return !body_id.IsInvalid();
			}
		};

		SimulationConfig config;
		std::vector<BodyID> planes;
		std::vector<ObjectData> objects;
		std::vector<std::vector<uint16_t>> interactions;
	};

#else
	// ODE Implementation
	struct SimulationImpl
	{
		SimulationImpl()
		{
			world = 0;
			space = 0;
			contacts = 0;
		}

		~SimulationImpl()
		{
			if ( contacts )
				dJointGroupDestroy( contacts );
			if ( world )
				dWorldDestroy( world );
			if ( space )
				dSpaceDestroy( space );

			contacts = 0;
			world = 0;
			space = 0;
		}

		dWorldID world;
		dSpaceID space;
		dJointGroupID contacts;

		struct ObjectData
		{
			dBodyID body;
			dGeomID geom;
			float scale;
			float timeAtRest;

			ObjectData()
			{
				body = 0;
				geom = 0;
				scale = 1.0f;
				timeAtRest = 0.0f;
			}

			bool exists() const
			{
				return body != 0;
			}
		};

		SimulationConfig config;
		std::vector<dGeomID> planes;
		std::vector<ObjectData> objects;
		std::vector< std::vector<uint16_t> > interactions;

	    dContact contact[MaxContacts];

		void UpdateInteractionPairs( dBodyID b1, dBodyID b2 )
		{
			if ( !b1 || !b2 )
				return;

			uint64_t objectId1 = reinterpret_cast<uint64_t>( dBodyGetData( b1 ) );
			uint64_t objectId2 = reinterpret_cast<uint64_t>( dBodyGetData( b2 ) );

			interactions[objectId1].push_back( objectId2 );
			interactions[objectId2].push_back( objectId1 );
		}

		static void NearCallback( void * data, dGeomID o1, dGeomID o2 )
		{
			SimulationImpl * simulation = (SimulationImpl*) data;

			assert( simulation );

		    dBodyID b1 = dGeomGetBody( o1 );
		    dBodyID b2 = dGeomGetBody( o2 );

			if ( int numc = dCollide( o1, o2, MaxContacts, &simulation->contact[0].geom, sizeof(dContact) ) )
			{
		        for ( int i = 0; i < numc; i++ )
		        {
		            dJointID c = dJointCreateContact( simulation->world, simulation->contacts, simulation->contact+i );
		            dJointAttach( c, b1, b2 );
		        }

				simulation->UpdateInteractionPairs( b1, b2 );
			}
		}
	};
#endif

	// ------------------------------------------

	int * Simulation::GetInitCount()
	{
		static int initCount = 0;
		return &initCount;
	}

	Simulation::Simulation()
	{
		printf("[DEBUG] Simulation constructor called\n");
		fflush(stdout);
		int * initCount = GetInitCount();
#ifdef USE_JOLT_PHYSICS
		if ( *initCount == 0 )
		{
			// Register all Jolt physics types
			printf("[DEBUG] Initializing Jolt Physics (initCount=0)\n");
			fflush(stdout);
			RegisterDefaultAllocator();
			Factory::sInstance = new Factory();
			RegisterTypes();
		}
#else
		if ( *initCount == 0 )
			dInitODE();
#endif
		(*initCount)++;
		printf("[DEBUG] Creating SimulationImpl (initCount=%d)\n", *initCount);
		fflush(stdout);
		impl = new SimulationImpl();
		printf("[DEBUG] Simulation constructor complete\n");
		fflush(stdout);
	}

	Simulation::~Simulation()
	{
		printf("[DEBUG] Simulation destructor called\n");
		fflush(stdout);
		delete impl;
		printf("[DEBUG] impl deleted\n");
		fflush(stdout);
		impl = NULL;
		int * initCount = GetInitCount();
		(*initCount)--;
		printf("[DEBUG] initCount decremented to %d\n", *initCount);
		fflush(stdout);
#ifdef USE_JOLT_PHYSICS
		if ( *initCount == 0 )
		{
			printf("[DEBUG] Deleting Factory (initCount=0)\n");
			fflush(stdout);
			delete Factory::sInstance;
			Factory::sInstance = nullptr;
			printf("[DEBUG] Factory deleted\n");
			fflush(stdout);
		}
#else
		if ( *initCount == 0 )
			dCloseODE();
#endif
		printf("[DEBUG] Simulation destructor complete\n");
		fflush(stdout);
	}

	void Simulation::Initialize( const SimulationConfig & config )
	{
		printf("[DEBUG] Simulation::Initialize called\n");
		fflush(stdout);
		impl->config = config;

#ifdef USE_JOLT_PHYSICS
		// Create Jolt Physics system
		printf("[DEBUG] Creating Jolt Physics system\n");
		fflush(stdout);
		const uint cMaxBodies = 1024;
		const uint cNumBodyMutexes = 0;
		const uint cMaxBodyPairs = 1024;
		const uint cMaxContactConstraints = 1024;

		printf("[DEBUG] Creating temp_allocator\n");
		fflush(stdout);
		impl->temp_allocator = new TempAllocatorImpl(10 * 1024 * 1024);
		printf("[DEBUG] Creating job_system\n");
		fflush(stdout);
		impl->job_system = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

		// Create layer interfaces
		printf("[DEBUG] Creating layer interfaces\n");
		fflush(stdout);
		impl->broad_phase_layer_interface = new BPLayerInterfaceImpl();
		impl->object_vs_broadphase_layer_filter = new ObjectVsBroadPhaseLayerFilterImpl();
		impl->object_vs_object_layer_filter = new ObjectLayerPairFilterImpl();

		printf("[DEBUG] Creating PhysicsSystem\n");
		fflush(stdout);
		impl->physics_system = new PhysicsSystem();
		printf("[DEBUG] Initializing PhysicsSystem\n");
		fflush(stdout);
		impl->physics_system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
			*impl->broad_phase_layer_interface, *impl->object_vs_broadphase_layer_filter, *impl->object_vs_object_layer_filter);

		// Set up contact listener
		printf("[DEBUG] Creating contact listener\n");
		fflush(stdout);
		impl->contact_listener = new MyContactListener();
		impl->contact_listener->interactions = &impl->interactions;
		impl->physics_system->SetContactListener(impl->contact_listener);

		// Configure gravity
		printf("[DEBUG] Setting gravity\n");
		fflush(stdout);
		impl->physics_system->SetGravity(Vec3(0, 0, -config.Gravity));

		// Note: Jolt uses different parameters than ODE. Some config values like ERP, CFM,
		// ContactSurfaceLayer don't have direct equivalents. Jolt's solver is different.

#else
		// Create ODE simulation
		impl->world = dWorldCreate();
	    impl->contacts = dJointGroupCreate( 0 );
	    dVector3 center = { 0,0,0 };
	    dVector3 extents = { 100,100,100 };
	    impl->space = dQuadTreeSpaceCreate( 0, center, extents, 10 );

		// Configure world
		dWorldSetERP( impl->world, config.ERP );
		dWorldSetCFM( impl->world, config.CFM );
		dWorldSetQuickStepNumIterations( impl->world, config.MaxIterations );
		dWorldSetGravity( impl->world, 0, 0, -config.Gravity );
		dWorldSetContactSurfaceLayer( impl->world, config.ContactSurfaceLayer );
		dWorldSetContactMaxCorrectingVel( impl->world, config.MaximumCorrectingVelocity );
		dWorldSetLinearDamping( impl->world, 0.01f );
		dWorldSetAngularDamping( impl->world, 0.01f );

		// Setup contacts
	    for ( int i = 0; i < MaxContacts; i++ )
	    {
			impl->contact[i].surface.mode = dContactBounce;
			impl->contact[i].surface.mu = config.Friction;
			impl->contact[i].surface.bounce = config.Elasticity;
			impl->contact[i].surface.bounce_vel = 0.001f;
	    }
#endif

		printf("[DEBUG] Resizing objects vector to 1024\n");
		fflush(stdout);
		impl->objects.resize( 1024 );
		printf("[DEBUG] Simulation::Initialize complete\n");
		fflush(stdout);
	}

	void Simulation::Update( float deltaTime, bool paused )
	{
		impl->interactions.clear();

		impl->interactions.resize( impl->objects.size() );

		if ( paused )
			return;

#ifdef USE_JOLT_PHYSICS
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();

		// IMPORTANT: do this *first* before updating simulation then at rest calculations
		// will work properly with rough quantization (quantized state is fed in prior to update)
		for ( int i = 0; i < (int) impl->objects.size(); ++i )
		{
			if ( impl->objects[i].exists() )
			{
				BodyID body_id = impl->objects[i].body_id;
				Vec3 linearVelocity = body_interface.GetLinearVelocity(body_id);
				Vec3 angularVelocity = body_interface.GetAngularVelocity(body_id);

				const float linearVelocityLengthSquared = linearVelocity.LengthSq();
				const float angularVelocityLengthSquared = angularVelocity.LengthSq();

				if ( linearVelocityLengthSquared > MaxLinearSpeed * MaxLinearSpeed )
				{
					const float linearSpeed = sqrt( linearVelocityLengthSquared );
					const float scale = MaxLinearSpeed / linearSpeed;
					linearVelocity *= scale;
					body_interface.SetLinearVelocity(body_id, linearVelocity);
				}

				if ( angularVelocityLengthSquared > MaxAngularSpeed * MaxAngularSpeed )
				{
					const float angularSpeed = sqrt( angularVelocityLengthSquared );
					const float scale = MaxAngularSpeed / angularSpeed;
					angularVelocity *= scale;
					body_interface.SetAngularVelocity(body_id, angularVelocity);
				}

				if ( linearVelocityLengthSquared < impl->config.LinearRestThresholdSquared &&
				     angularVelocityLengthSquared < impl->config.AngularRestThresholdSquared )
					impl->objects[i].timeAtRest += deltaTime;
				else
					impl->objects[i].timeAtRest = 0.0f;

				if ( impl->objects[i].timeAtRest >= impl->config.RestTime )
					body_interface.DeactivateBody(body_id);
				else
					body_interface.ActivateBody(body_id);
			}
		}

		// Step the physics world
		impl->physics_system->Update(deltaTime, 1, impl->temp_allocator, impl->job_system);

#else
		// IMPORTANT: do this *first* before updating simulation then at rest calculations
		// will work properly with rough quantization (quantized state is fed in prior to update)
		for ( int i = 0; i < (int) impl->objects.size(); ++i )
		{
			if ( impl->objects[i].exists() )
			{
				const dReal * linearVelocity = dBodyGetLinearVel( impl->objects[i].body );
				const dReal * angularVelocity = dBodyGetAngularVel( impl->objects[i].body );

				const float linearVelocityLengthSquared = linearVelocity[0]*linearVelocity[0] + linearVelocity[1]*linearVelocity[1] + linearVelocity[2]*linearVelocity[2];
				const float angularVelocityLengthSquared = angularVelocity[0]*angularVelocity[0] + angularVelocity[1]*angularVelocity[1] + angularVelocity[2]*angularVelocity[2];

				if ( linearVelocityLengthSquared > MaxLinearSpeed * MaxLinearSpeed )
				{
					const float linearSpeed = sqrt( linearVelocityLengthSquared );

					const float scale = MaxLinearSpeed / linearSpeed;

					dReal clampedLinearVelocity[3];

					clampedLinearVelocity[0] = linearVelocity[0] * scale;
					clampedLinearVelocity[1] = linearVelocity[1] * scale;
					clampedLinearVelocity[2] = linearVelocity[2] * scale;

					dBodySetLinearVel( impl->objects[i].body, clampedLinearVelocity[0], clampedLinearVelocity[1], clampedLinearVelocity[2] );

					linearVelocity = &clampedLinearVelocity[0];
				}

				if ( angularVelocityLengthSquared > MaxAngularSpeed * MaxAngularSpeed )
				{
					const float angularSpeed = sqrt( angularVelocityLengthSquared );

					const float scale = MaxAngularSpeed / angularSpeed;

					dReal clampedAngularVelocity[3];

					clampedAngularVelocity[0] = angularVelocity[0] * scale;
					clampedAngularVelocity[1] = angularVelocity[1] * scale;
					clampedAngularVelocity[2] = angularVelocity[2] * scale;

					dBodySetAngularVel( impl->objects[i].body, clampedAngularVelocity[0], clampedAngularVelocity[1], clampedAngularVelocity[2] );

					angularVelocity = &clampedAngularVelocity[0];
				}

				if ( linearVelocityLengthSquared < impl->config.LinearRestThresholdSquared && angularVelocityLengthSquared < impl->config.AngularRestThresholdSquared )
					impl->objects[i].timeAtRest += deltaTime;
				else
					impl->objects[i].timeAtRest = 0.0f;

				if ( impl->objects[i].timeAtRest >= impl->config.RestTime )
					dBodyDisable( impl->objects[i].body );
				else
					dBodyEnable( impl->objects[i].body );
			}
		}

		dJointGroupEmpty( impl->contacts );

		dSpaceCollide( impl->space, impl, SimulationImpl::NearCallback );

		if ( impl->config.QuickStep )
			dWorldQuickStep( impl->world, deltaTime );
		else
			dWorldStep( impl->world, deltaTime );
#endif
	}
	
	int Simulation::AddObject( const SimulationObjectState & initialObjectState )
	{
		printf("[DEBUG] AddObject called\n");
		fflush(stdout);
		// find free object slot

		uint64_t id = -1;
		for ( int i = 0; i < (int) impl->objects.size(); ++i )
		{
			if ( !impl->objects[i].exists() )
			{
				id = i;
				break;
			}
		}
		if ( id == -1 )
		{
			id = impl->objects.size();
			impl->objects.resize( id + 1 );
		}

#ifdef USE_JOLT_PHYSICS
		// Setup object body with Jolt Physics
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();

		// Create box shape
		BoxShapeSettings box_shape_settings(Vec3(initialObjectState.scale * 0.5f, initialObjectState.scale * 0.5f, initialObjectState.scale * 0.5f));
		box_shape_settings.SetDensity(1.0f);

		// Create body creation settings
		// Note: box_shape_settings.Create().Get() returns a ShapeRefC which is properly reference counted
		BodyCreationSettings body_settings(
			box_shape_settings.Create().Get(),
			RVec3(initialObjectState.position.x, initialObjectState.position.y, initialObjectState.position.z),
			Quat(initialObjectState.orientation.x, initialObjectState.orientation.y, initialObjectState.orientation.z, initialObjectState.orientation.w),
			EMotionType::Dynamic,
			Layers::MOVING
		);

		// Set friction and restitution
		body_settings.mFriction = impl->config.Friction;
		body_settings.mRestitution = impl->config.Elasticity;
		body_settings.mLinearDamping = 0.01f;
		body_settings.mAngularDamping = 0.01f;
		body_settings.mUserData = id;  // Set user data for tracking object ID

		// Create and add the body to the physics system
		Body *body = body_interface.CreateBody(body_settings);
		if (body == nullptr)
		{
			// Failed to create body
			return -1;
		}

		impl->objects[id].body_id = body->GetID();
		impl->objects[id].scale = initialObjectState.scale;

		// Add body to physics system
		body_interface.AddBody(impl->objects[id].body_id, EActivation::Activate);

		// Set object state
		SetObjectState( id, initialObjectState );

#else
		// Setup object body with ODE
		impl->objects[id].body = dBodyCreate( impl->world );

		assert( impl->objects[id].body );

		dMass mass;
		const float density = 1.0f;
		dMassSetBox( &mass, density, initialObjectState.scale, initialObjectState.scale, initialObjectState.scale );
		dBodySetMass( impl->objects[id].body, &mass );
		dBodySetData( impl->objects[id].body, (void*) id );

		// setup geom and attach to body

		impl->objects[id].scale = initialObjectState.scale;
		impl->objects[id].geom = dCreateBox( impl->space, initialObjectState.scale, initialObjectState.scale, initialObjectState.scale );

		dGeomSetBody( impl->objects[id].geom, impl->objects[id].body );

		// set object state

		SetObjectState( id, initialObjectState );
#endif

		// success!

		return id;
	}

	bool Simulation::ObjectExists( int id )
	{
		assert( id >= 0 && id < (int) impl->objects.size() );
		return impl->objects[id].exists();
	}

	float Simulation::GetObjectMass( int id )
	{
		assert( id >= 0 && id < (int) impl->objects.size() );
		assert( impl->objects[id].exists() );

#ifdef USE_JOLT_PHYSICS
		// Get the body using the locking interface
		const BodyLockInterface &lock_interface = impl->physics_system->GetBodyLockInterface();
		BodyID body_id = impl->objects[id].body_id;

		BodyLockRead lock(lock_interface, body_id);
		if (lock.Succeeded())
		{
			const Body &body = lock.GetBody();
			if (body.IsDynamic())
			{
				return 1.0f / body.GetMotionProperties()->GetInverseMass();
			}
		}
		return 0.0f;
#else
		dMass mass;
		dBodyGetMass( impl->objects[id].body, &mass );
		return mass.mass;
#endif
	}

	void Simulation::RemoveObject( int id )
	{
		assert( id >= 0 && id < (int) impl->objects.size() );
		assert( impl->objects[id].exists() );

#ifdef USE_JOLT_PHYSICS
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
		body_interface.RemoveBody(impl->objects[id].body_id);
		body_interface.DestroyBody(impl->objects[id].body_id);
		impl->objects[id].body_id = BodyID();
#else
		dBodyDestroy( impl->objects[id].body );
		dGeomDestroy( impl->objects[id].geom );
		impl->objects[id].body = 0;
		impl->objects[id].geom = 0;
#endif
	}

	void Simulation::GetObjectState( int id, SimulationObjectState & objectState )
	{
		assert( id >= 0 );
		assert( id < (int) impl->objects.size() );

		assert( impl->objects[id].exists() );

#ifdef USE_JOLT_PHYSICS
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
		BodyID body_id = impl->objects[id].body_id;

		RVec3 position = body_interface.GetPosition(body_id);
		Quat orientation = body_interface.GetRotation(body_id);
		Vec3 linearVelocity = body_interface.GetLinearVelocity(body_id);
		Vec3 angularVelocity = body_interface.GetAngularVelocity(body_id);

		objectState.position = math::Vector( position.GetX(), position.GetY(), position.GetZ() );
		objectState.orientation = math::Quaternion( orientation.GetW(), orientation.GetX(), orientation.GetY(), orientation.GetZ() );
		objectState.linearVelocity = math::Vector( linearVelocity.GetX(), linearVelocity.GetY(), linearVelocity.GetZ() );
		objectState.angularVelocity = math::Vector( angularVelocity.GetX(), angularVelocity.GetY(), angularVelocity.GetZ() );

		objectState.enabled = impl->objects[id].timeAtRest < impl->config.RestTime;
#else
		const dReal * position = dBodyGetPosition( impl->objects[id].body );
		const dReal * orientation = dBodyGetQuaternion( impl->objects[id].body );
		const dReal * linearVelocity = dBodyGetLinearVel( impl->objects[id].body );
		const dReal * angularVelocity = dBodyGetAngularVel( impl->objects[id].body );

		objectState.position = math::Vector( position[0], position[1], position[2] );
		objectState.orientation = math::Quaternion( orientation[0], orientation[1], orientation[2], orientation[3] );
		objectState.linearVelocity = math::Vector( linearVelocity[0], linearVelocity[1], linearVelocity[2] );
		objectState.angularVelocity = math::Vector( angularVelocity[0], angularVelocity[1], angularVelocity[2] );

		objectState.enabled = impl->objects[id].timeAtRest < impl->config.RestTime;
#endif
	}

	void Simulation::SetObjectState( int id, const SimulationObjectState & objectState, bool ignoreEnabledFlag )
	{
		assert( id >= 0 );
		assert( id < (int) impl->objects.size() );

		assert( impl->objects[id].exists() );

#ifdef USE_JOLT_PHYSICS
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
		BodyID body_id = impl->objects[id].body_id;

		RVec3 position(objectState.position.x, objectState.position.y, objectState.position.z);
		Quat orientation(objectState.orientation.x, objectState.orientation.y, objectState.orientation.z, objectState.orientation.w);
		Vec3 linearVelocity(objectState.linearVelocity.x, objectState.linearVelocity.y, objectState.linearVelocity.z);
		Vec3 angularVelocity(objectState.angularVelocity.x, objectState.angularVelocity.y, objectState.angularVelocity.z);

		body_interface.SetPositionAndRotation(body_id, position, orientation, EActivation::DontActivate);
		body_interface.SetLinearVelocity(body_id, linearVelocity);
		body_interface.SetAngularVelocity(body_id, angularVelocity);

		if ( !ignoreEnabledFlag )
		{
			if ( objectState.enabled )
			{
				impl->objects[id].timeAtRest = 0.0f;
				body_interface.ActivateBody(body_id);
			}
			else
			{
				impl->objects[id].timeAtRest = impl->config.RestTime;
				body_interface.DeactivateBody(body_id);
			}
		}
#else
		dQuaternion quaternion;
		quaternion[0] = objectState.orientation.w;
		quaternion[1] = objectState.orientation.x;
		quaternion[2] = objectState.orientation.y;
		quaternion[3] = objectState.orientation.z;

		dBodySetPosition( impl->objects[id].body, objectState.position.x, objectState.position.y, objectState.position.z );
		dBodySetQuaternion( impl->objects[id].body, quaternion );
		dBodySetLinearVel( impl->objects[id].body, objectState.linearVelocity.x, objectState.linearVelocity.y, objectState.linearVelocity.z );
		dBodySetAngularVel( impl->objects[id].body, objectState.angularVelocity.x, objectState.angularVelocity.y, objectState.angularVelocity.z );

		if ( !ignoreEnabledFlag )
		{
			if ( objectState.enabled )
			{
				impl->objects[id].timeAtRest = 0.0f;
				dBodyEnable( impl->objects[id].body );
			}
			else
			{
				impl->objects[id].timeAtRest = impl->config.RestTime;
				dBodyDisable( impl->objects[id].body );
			}
		}
#endif
	}

	const std::vector<uint16_t> & Simulation::GetObjectInteractions( int id ) const
	{
		assert( id >= 0 );
		assert( id < (int) impl->interactions.size() );
		return impl->interactions[id];
	}

	void Simulation::ApplyForce( int id, const math::Vector & force )
	{
		assert( id >= 0 );
		assert( id < (int) impl->objects.size() );
		assert( impl->objects[id].exists() );
		if ( force.length() > 0.001f )
		{
			impl->objects[id].timeAtRest = 0.0f;
#ifdef USE_JOLT_PHYSICS
			BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
			body_interface.ActivateBody(impl->objects[id].body_id);
			body_interface.AddForce(impl->objects[id].body_id, Vec3(force.x, force.y, force.z));
#else
			dBodyEnable( impl->objects[id].body );
			dBodyAddForce( impl->objects[id].body, force.x, force.y, force.z );
#endif
		}
	}

	void Simulation::ApplyTorque( int id, const math::Vector & torque )
	{
		assert( id >= 0 );
		assert( id < (int) impl->objects.size() );
		assert( impl->objects[id].exists() );
		if ( torque.length() > 0.001f )
		{
			impl->objects[id].timeAtRest = 0.0f;
#ifdef USE_JOLT_PHYSICS
			BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
			body_interface.ActivateBody(impl->objects[id].body_id);
			body_interface.AddTorque(impl->objects[id].body_id, Vec3(torque.x, torque.y, torque.z));
#else
			dBodyEnable( impl->objects[id].body );
			dBodyAddTorque( impl->objects[id].body, torque.x, torque.y, torque.z );
#endif
		}
	}

	void Simulation::AddPlane( const math::Vector & normal, float d )
	{
		printf("[DEBUG] AddPlane called (normal: %f,%f,%f, d: %f)\n", normal.x, normal.y, normal.z, d);
		fflush(stdout);
#ifdef USE_JOLT_PHYSICS
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();

		// Create infinite plane shape
		PlaneShapeSettings plane_settings(Plane(Vec3(normal.x, normal.y, normal.z), -d), nullptr, 1000.0f);
		ShapeSettings::ShapeResult plane_shape_result = plane_settings.Create();
		if (plane_shape_result.HasError())
		{
			return;
		}

		// Create static body for the plane
		BodyCreationSettings plane_body_settings(
			plane_shape_result.Get(),
			RVec3::sZero(),
			Quat::sIdentity(),
			EMotionType::Static,
			Layers::NON_MOVING
		);

		plane_body_settings.mFriction = impl->config.Friction;
		plane_body_settings.mRestitution = impl->config.Elasticity;

		Body *plane_body = body_interface.CreateBody(plane_body_settings);
		BodyID plane_id = plane_body->GetID();
		body_interface.AddBody(plane_id, EActivation::DontActivate);

		impl->planes.push_back(plane_id);
		printf("[DEBUG] AddPlane complete (plane_id valid: %d)\n", !plane_id.IsInvalid());
		fflush(stdout);
#else
		impl->planes.push_back( dCreatePlane( impl->space, normal.x, normal.y, normal.z, d ) );
#endif
		printf("[DEBUG] AddPlane returning\n");
		fflush(stdout);
	}

	void Simulation::Reset()
	{
		for ( int i = 0; i < (int) impl->objects.size(); ++i )
		{
			if ( impl->objects[i].exists() )
				RemoveObject( i );
		}

#ifdef USE_JOLT_PHYSICS
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
		for ( int i = 0; i < (int) impl->planes.size(); ++i )
		{
			body_interface.RemoveBody(impl->planes[i]);
			body_interface.DestroyBody(impl->planes[i]);
		}
#else
		for ( int i = 0; i < (int) impl->planes.size(); ++i )
			dGeomDestroy( impl->planes[i] );
#endif

		impl->planes.clear();
	}
}
