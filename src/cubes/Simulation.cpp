/*
	Networked Physics Demo
	Copyright © 2008-2015 Glenn Fiedler
	http://www.gafferongames.com/networking-for-game-programmers
*/

#include "Simulation.h"
#include <mutex>

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

namespace cubes
{
	// simulation internal implementation

	const int MaxContacts = 16;

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
			// Clean up all bodies and planes before destroying the physics system
			if (physics_system)
			{
				BodyInterface &body_interface = physics_system->GetBodyInterface();

				// Remove and destroy all object bodies
				for (size_t i = 0; i < objects.size(); ++i)
				{
					if (objects[i].exists() && !objects[i].body_id.IsInvalid())
					{
						body_interface.RemoveBody(objects[i].body_id);
						body_interface.DestroyBody(objects[i].body_id);
					}
				}

				// Remove and destroy all plane bodies
				for (size_t i = 0; i < planes.size(); ++i)
				{
					if (!planes[i].IsInvalid())
					{
						body_interface.RemoveBody(planes[i]);
						body_interface.DestroyBody(planes[i]);
					}
				}
			}

			delete physics_system;
			physics_system = nullptr;

			delete contact_listener;
			contact_listener = nullptr;

			delete object_vs_object_layer_filter;
			object_vs_object_layer_filter = nullptr;

			delete object_vs_broadphase_layer_filter;
			object_vs_broadphase_layer_filter = nullptr;

			delete broad_phase_layer_interface;
			broad_phase_layer_interface = nullptr;

			delete job_system;
			job_system = nullptr;

			delete temp_allocator;
			temp_allocator = nullptr;
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

	// ------------------------------------------

	int * Simulation::GetInitCount()
	{
		static int initCount = 0;
		return &initCount;
	}

	Simulation::Simulation()
	{
		int * initCount = GetInitCount();
		if ( *initCount == 0 )
		{
			// Register all Jolt physics types
			RegisterDefaultAllocator();
			Factory::sInstance = new Factory();
			RegisterTypes();
		}
		(*initCount)++;
		impl = new SimulationImpl();
	}

	Simulation::~Simulation()
	{
		delete impl;
		impl = NULL;
		int * initCount = GetInitCount();
		(*initCount)--;
		if ( *initCount == 0 )
		{
			delete Factory::sInstance;
			Factory::sInstance = nullptr;
		}
	}

	void Simulation::Initialize( const SimulationConfig & config )
	{
		impl->config = config;

		// Create Jolt Physics system
		const uint cMaxBodies = 1024;
		const uint cNumBodyMutexes = 0;
		const uint cMaxBodyPairs = 1024;
		const uint cMaxContactConstraints = 1024;

		impl->temp_allocator = new TempAllocatorImpl(10 * 1024 * 1024);
		impl->job_system = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

		// Create layer interfaces
		impl->broad_phase_layer_interface = new BPLayerInterfaceImpl();
		impl->object_vs_broadphase_layer_filter = new ObjectVsBroadPhaseLayerFilterImpl();
		impl->object_vs_object_layer_filter = new ObjectLayerPairFilterImpl();

		impl->physics_system = new PhysicsSystem();
		impl->physics_system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
			*impl->broad_phase_layer_interface, *impl->object_vs_broadphase_layer_filter, *impl->object_vs_object_layer_filter);

		// Set up contact listener
		impl->contact_listener = new MyContactListener();
		impl->contact_listener->interactions = &impl->interactions;
		impl->physics_system->SetContactListener(impl->contact_listener);

		// Configure gravity
		impl->physics_system->SetGravity(Vec3(0, 0, -config.Gravity));

		impl->objects.resize( 1024 );
	}

	void Simulation::Update( float deltaTime, bool paused )
	{
		impl->interactions.clear();

		impl->interactions.resize( impl->objects.size() );

		if ( paused )
			return;

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
	}

	int Simulation::AddObject( const SimulationObjectState & initialObjectState )
	{
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

		// Setup object body with Jolt Physics
		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();

		// Create box shape
		BoxShapeSettings box_shape_settings(Vec3(initialObjectState.scale * 0.5f, initialObjectState.scale * 0.5f, initialObjectState.scale * 0.5f));
		box_shape_settings.SetDensity(1.0f);

		// Create body creation settings
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
	}

	void Simulation::RemoveObject( int id )
	{
		assert( id >= 0 && id < (int) impl->objects.size() );
		assert( impl->objects[id].exists() );

		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
		body_interface.RemoveBody(impl->objects[id].body_id);
		body_interface.DestroyBody(impl->objects[id].body_id);
		impl->objects[id].body_id = BodyID();
	}

	void Simulation::GetObjectState( int id, SimulationObjectState & objectState )
	{
		assert( id >= 0 );
		assert( id < (int) impl->objects.size() );

		assert( impl->objects[id].exists() );

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
	}

	void Simulation::SetObjectState( int id, const SimulationObjectState & objectState, bool ignoreEnabledFlag )
	{
		assert( id >= 0 );
		assert( id < (int) impl->objects.size() );

		assert( impl->objects[id].exists() );

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
			BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
			body_interface.ActivateBody(impl->objects[id].body_id);
			body_interface.AddForce(impl->objects[id].body_id, Vec3(force.x, force.y, force.z));
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
			BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
			body_interface.ActivateBody(impl->objects[id].body_id);
			body_interface.AddTorque(impl->objects[id].body_id, Vec3(torque.x, torque.y, torque.z));
		}
	}

	void Simulation::AddPlane( const math::Vector & normal, float d )
	{
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
	}

	void Simulation::Reset()
	{
		for ( int i = 0; i < (int) impl->objects.size(); ++i )
		{
			if ( impl->objects[i].exists() )
				RemoveObject( i );
		}

		BodyInterface &body_interface = impl->physics_system->GetBodyInterface();
		for ( int i = 0; i < (int) impl->planes.size(); ++i )
		{
			body_interface.RemoveBody(impl->planes[i]);
			body_interface.DestroyBody(impl->planes[i]);
		}

		impl->planes.clear();
	}
}
