#include "ModulePhysics3D.h"

#include "Glew\include\glew.h"

#include "Application.h"
#include "ModuleRenderer3D.h"
#include "ModuleFileSystem.h"
#include "ModuleInput.h"
#include "ModuleLighting.h"

#include "GameObject.h"
#include "ComponentMesh.h"
#include "ComponentTransform.h"
#include "ComponentCamera.h"

#include "PhysBody3D.h"
#include "PhysVehicle3D.h"
#include "Primitive.h"

#include "ComponentCar.h"
#include "ComponentCollider.h"

#include "Assets.h"
#include "RaycastHit.h"
#include "Time.h"

#include "Devil/include/il.h"
#include "Devil/include/ilut.h"

#include "Bullet\include\BulletCollision\CollisionShapes\btShapeHull.h"
#include "Bullet\include\BulletCollision\CollisionShapes\btHeightfieldTerrainShape.h"

#include "ResourceFileTexture.h"

#include "SDL\include\SDL_scancode.h"

#ifdef _DEBUG
	#pragma comment (lib, "Bullet/libx86/BulletDynamics_debug.lib")
	#pragma comment (lib, "Bullet/libx86/BulletCollision_debug.lib")
	#pragma comment (lib, "Bullet/libx86/LinearMath_debug.lib")
#else
	#pragma comment (lib, "Bullet/libx86/BulletDynamics.lib")
	#pragma comment (lib, "Bullet/libx86/BulletCollision.lib")
	#pragma comment (lib, "Bullet/libx86/LinearMath.lib")
#endif

ModulePhysics3D::ModulePhysics3D(const char* name, bool start_enabled) : Module(name, start_enabled)
{
	debug = true;

	collision_conf = new btDefaultCollisionConfiguration();
	dispatcher = new btCollisionDispatcher(collision_conf);
	broad_phase = new btDbvtBroadphase();
	solver = new btSequentialImpulseConstraintSolver();
	debug_draw = new DebugDrawer(); //DEBUG DISABLED
}

// Destructor
ModulePhysics3D::~ModulePhysics3D()
{
	//delete debug_draw;
	delete solver;
	delete broad_phase;
	delete dispatcher;
	delete collision_conf;
}


// Render not available yet----------------------------------
bool ModulePhysics3D::Init(Data& config)
{
	LOG("Creating 3D Physics simulation");
	bool ret = true;

	return ret;
}

bool ModulePhysics3D::Start()
{
	LOG("Creating Physics environment");

	world = new btDiscreteDynamicsWorld(dispatcher, broad_phase, solver, collision_conf);
	//world->setDebugDrawer(debug_draw);
	world->setGravity(GRAVITY);
	vehicle_raycaster = new btDefaultVehicleRaycaster(world);
	CreateGround();
	return true;
}

update_status ModulePhysics3D::PreUpdate()
{
	float dt = time->DeltaTime();
	if (App->IsGameRunning())
	{
		world->stepSimulation(dt, 15);

		int numManifolds = world->getDispatcher()->getNumManifolds();
		for (int i = 0; i < numManifolds; i++)
		{
			btPersistentManifold* contactManifold = world->getDispatcher()->getManifoldByIndexInternal(i);
			btCollisionObject* obA = (btCollisionObject*)(contactManifold->getBody0());
			btCollisionObject* obB = (btCollisionObject*)(contactManifold->getBody1());

			int numContacts = contactManifold->getNumContacts();
			if (numContacts > 0)
			{
				PhysBody3D* pbodyA = (PhysBody3D*)obA->getUserPointer();
				PhysBody3D* pbodyB = (PhysBody3D*)obB->getUserPointer();

				if (pbodyA && pbodyB)
				{
					if(ReadFlag(pbodyA->collisionOptions, PhysBody3D::co_isCar) &&
						ReadFlag(pbodyB->collisionOptions, PhysBody3D::co_isTrigger))
					{
						App->physics->OnCollision(pbodyA, pbodyB);
					}

					if (ReadFlag(pbodyB->collisionOptions, PhysBody3D::co_isCar) &&
						ReadFlag(pbodyA->collisionOptions, PhysBody3D::co_isTrigger))
					{
						App->physics->OnCollision(pbodyB, pbodyA);
					}
				}
			}
		}
	}
	return UPDATE_CONTINUE;
}

update_status ModulePhysics3D::Update()
{
	if(App->input->GetKey(SDL_SCANCODE_F1) == KEY_DOWN)
		debug = !debug;

	if(debug == true)
	{
		world->debugDrawWorld();
	}

	return UPDATE_CONTINUE;
}

update_status ModulePhysics3D::PostUpdate()
{
	
	return UPDATE_CONTINUE;
}

bool ModulePhysics3D::CleanUp()
{
	LOG("Destroying 3D Physics simulation");

	CleanWorld();

	DeleteHeightmap();

	if (heightMapImg)
	{
		heightMapImg->Unload();
	}
	for (uint n = 0; n < GetNTextures(); n++)
	{
		DeleteTexture(n);
	}

	delete vehicle_raycaster;
	delete world;

	return true;
}

void ModulePhysics3D::OnCollision(PhysBody3D * physCar, PhysBody3D * body)
{
	ComponentCar* car = physCar->GetCar();
	ComponentCollider* trigger = body->GetCollider();
	if (car != nullptr && trigger != nullptr)
	{
		if (ReadFlag(body->collisionOptions, PhysBody3D::co_isCheckpoint))
		{
			car->WentThroughCheckpoint(trigger);
		}
		if (ReadFlag(body->collisionOptions, PhysBody3D::co_isFinishLane))
		{
			car->WentThroughEnd(trigger);
		}
		if (ReadFlag(body->collisionOptions, PhysBody3D::co_isItem))
		{
			car->PickItem();
		}
		if (ReadFlag(body->collisionOptions, PhysBody3D::co_isOutOfBounds))
		{
			car->Reset();
		}
	}
}

void ModulePhysics3D::OnPlay()
{
	AddTerrain();
}

void ModulePhysics3D::OnStop()
{
	CleanWorld();
}

void ModulePhysics3D::CleanWorld()
{
	terrain = nullptr;

	// Remove from the world all collision bodies
	for (int i = world->getNumCollisionObjects() - 1; i >= 0; i--)
	{
		btCollisionObject* obj = world->getCollisionObjectArray()[i];
		world->removeCollisionObject(obj);
	}

	for (list<btTypedConstraint*>::iterator item = constraints.begin(); item != constraints.end(); item++)
	{
		world->removeConstraint(*item);
		delete *item;
	}

	constraints.clear();

	for (list<btDefaultMotionState*>::iterator item = motions.begin(); item != motions.end(); item++)
		delete *item;

	motions.clear();

	for (list<btCollisionShape*>::iterator item = shapes.begin(); item != shapes.end(); item++)
		delete *item;

	shapes.clear();

	for (list<PhysBody3D*>::iterator item = bodies.begin(); item != bodies.end(); item++)
		delete *item;

	bodies.clear();

	for (list<PhysVehicle3D*>::iterator item = vehicles.begin(); item != vehicles.end(); item++)
	{
		world->removeVehicle((*item)->vehicle);
		delete *item;
	}

	vehicles.clear();
	CreateGround();
	world->clearForces();
}


// ---------------------------------------------------------
void ModulePhysics3D::CreateGround()
{
	// Big plane as ground
	btCollisionShape* colShape = new btStaticPlaneShape(btVector3(0, 1, 0), 0);

	btDefaultMotionState* myMotionState = new btDefaultMotionState();
	btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, myMotionState, colShape);

	btRigidBody* body = new btRigidBody(rbInfo);
	world->addRigidBody(body);
}

bool ModulePhysics3D::GenerateHeightmap(string resLibPath)
{	
	bool ret = false;
	//Loading Heightmap Image
	if (resLibPath != GetHeightmapPath() && resLibPath != "" && resLibPath != " ")
	{
		ResourceFile* res = App->resource_manager->LoadResource(resLibPath, ResourceFileType::RES_TEXTURE);
		if (res != nullptr && res->GetType() == ResourceFileType::RES_TEXTURE)
		{
			if (heightMapImg != nullptr)
			{
				heightMapImg->Unload();
			}
			DeleteHeightmap();
			

			//If the file exists and is loaded succesfully, we need to reload it manually.
			//The Resource won't hold the pixel data
			//Don't worry, we'll be deleting soon
			char* buffer = nullptr;
			unsigned int size = App->file_system->Load(res->GetFile(), &buffer);
			if (size > 0)
			{
				heightMapImg = (ResourceFileTexture*)res;

				ILuint id;
				ilGenImages(1, &id);
				ilBindImage(id);
				if (ilLoadL(IL_DDS, (const void*)buffer, size))
				{
					int width = ilGetInteger(IL_IMAGE_WIDTH);
					int height = ilGetInteger(IL_IMAGE_HEIGHT);
					BYTE* tmp = new BYTE[width * height * 3];
					//Copying all RGB data of each pixel into a uchar (BYTE) array. We need to transform it into float numbers
					realTerrainData = new float[width * height];
					terrainData = new float[width * height];
					ilCopyPixels(0, 0, 0, width, height, 1, IL_RGB, IL_UNSIGNED_BYTE, tmp);

					float* R = new float[width * height];
					float* G = new float[width * height];
					float* B = new float[width * height];

					for (int n = 0; n < width * height; n++)
					{
						R[n] = tmp[n * 3];
						G[n] = tmp[n * 3 + 1];
						B[n] = tmp[n * 3 + 2];
					}
					InterpretHeightmapRGB(R, G, B);

					delete[] R;
					delete[] G;
					delete[] B;

					//Deleting the second loaded image
					ilBindImage(0);
					ilDeleteImages(1, &id);
					ret = true;

					delete[] tmp;
					GenerateTerrainMesh();
				}
			}
			if (buffer != nullptr)
			{
				delete[] buffer;
			}
		}
	}
	return ret;
}

void ModulePhysics3D::DeleteHeightmap()
{
	if (terrainData != nullptr)
	{
		delete[] terrainData;
		terrainData = nullptr;
	}
	if (realTerrainData != nullptr)
	{
		delete[] realTerrainData;
		realTerrainData = nullptr;
	}
	if (heightMapImg != nullptr)
	{
		heightMapImg->Unload();
		heightMapImg = nullptr;
	}
	for (uint n = 0; n < GetNTextures(); n++)
	{
		DeleteTexture(n);
	}
	DeleteTerrainMesh();
}

bool ModulePhysics3D::TerrainIsGenerated()
{
	return (terrainData != nullptr);
}


// ---------------------------------------------------------
PhysBody3D* ModulePhysics3D::AddBody(const Sphere_P& sphere, ComponentCollider* col, float mass, unsigned char flags)
{
	btCollisionShape* colShape = new btSphereShape(sphere.radius);
	shapes.push_back(colShape);

	btTransform startTransform;
	startTransform.setFromOpenGLMatrix(*sphere.transform.v);

	btVector3 localInertia(0, 0, 0);
	if(mass != 0.f)
		colShape->calculateLocalInertia(mass, localInertia);

	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	motions.push_back(myMotionState);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);

	btRigidBody* body = new btRigidBody(rbInfo);
	PhysBody3D* pbody = new PhysBody3D(body, col);
	pbody->collisionOptions = flags;

	if (ReadFlag(flags, PhysBody3D::co_isTransparent))
	{
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
	}

	body->setUserPointer(pbody);
	world->addRigidBody(body);
	bodies.push_back(pbody);

	return pbody;
}

PhysBody3D* ModulePhysics3D::AddBody(const Cube_P& cube, ComponentCollider* col, float mass, unsigned char flags)
{
	btCollisionShape* colShape = new btBoxShape(btVector3(cube.size.x*0.5f, cube.size.y*0.5f, cube.size.z*0.5f));
	shapes.push_back(colShape);

	btTransform startTransform;
	startTransform.setFromOpenGLMatrix(*cube.transform.v);

	btVector3 localInertia(0, 0, 0);
	if(mass != 0.f)
		colShape->calculateLocalInertia(mass, localInertia);

	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	motions.push_back(myMotionState);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);

	btRigidBody* body = new btRigidBody(rbInfo);
	PhysBody3D* pbody = new PhysBody3D(body, col);
	pbody->collisionOptions = flags;

	if (ReadFlag(flags, PhysBody3D::co_isTransparent))
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);

	body->setUserPointer(pbody);
	world->addRigidBody(body);
	bodies.push_back(pbody);

	return pbody;
}

PhysBody3D* ModulePhysics3D::AddBody(const Cylinder_P& cylinder, ComponentCollider* col, float mass, unsigned char flags)
{
	btCollisionShape* colShape = new btCylinderShapeX(btVector3(cylinder.height*0.5f, cylinder.radius, 0.0f));
	shapes.push_back(colShape);

	btTransform startTransform;
	startTransform.setFromOpenGLMatrix(*cylinder.transform.v);

	btVector3 localInertia(0, 0, 0);
	if(mass != 0.f)
		colShape->calculateLocalInertia(mass, localInertia);

	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	motions.push_back(myMotionState);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);

	btRigidBody* body = new btRigidBody(rbInfo);
	PhysBody3D* pbody = new PhysBody3D(body, col);
	pbody->collisionOptions = flags;

	if (ReadFlag(flags, PhysBody3D::co_isTransparent))
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);

	body->setUserPointer(pbody);
	world->addRigidBody(body);
	bodies.push_back(pbody);

	return pbody;
}

PhysBody3D* ModulePhysics3D::AddBody(const ComponentMesh& mesh, ComponentCollider* col, float mass, unsigned char flags, btConvexHullShape** out_shape)
{
	btConvexHullShape* colShape = new btConvexHullShape();

	float3* vertices = (float3*)mesh.GetMesh()->vertices;
	uint nVertices = mesh.GetMesh()->num_vertices;

	for (uint n = 0; n < nVertices; n++)
	{
		colShape->addPoint(btVector3(vertices[n].x, vertices[n].y, vertices[n].z));
	}

	btShapeHull* hull = new btShapeHull(colShape);
	hull->buildHull(colShape->getMargin());
	btConvexHullShape* simplifiedColShape = new btConvexHullShape((btScalar*)hull->getVertexPointer(), hull->numVertices());



	shapes.push_back(simplifiedColShape);

	if (out_shape != nullptr)
	{
		*out_shape = simplifiedColShape;
	}

	btTransform startTransform;
	float4x4 go_trs = mesh.GetGameObject()->transform->GetGlobalMatrix().Transposed();
	startTransform.setFromOpenGLMatrix(go_trs.ptr());

	btVector3 localInertia(0, 0, 0);
	if (mass != 0.f)
		simplifiedColShape->calculateLocalInertia(mass, localInertia);

	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	motions.push_back(myMotionState);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, simplifiedColShape, localInertia);

	btRigidBody* body = new btRigidBody(rbInfo);
	PhysBody3D* pbody = new PhysBody3D(body, col);
	pbody->collisionOptions = flags;

	if (ReadFlag(flags, PhysBody3D::co_isTransparent))
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);

	delete hull;
	delete colShape;

	body->setUserPointer(pbody);
	world->addRigidBody(body);
	bodies.push_back(pbody);

	return pbody;
}


// ---------------------------------------------------------
PhysVehicle3D* ModulePhysics3D::AddVehicle(const VehicleInfo& info, ComponentCar* col)
{
	btCompoundShape* comShape = new btCompoundShape();
	shapes.push_back(comShape);

	//Base
	btCollisionShape* colBase = new btBoxShape(btVector3(info.chassis_size.x*0.5f, info.chassis_size.y*0.5f, info.chassis_size.z*0.5f));
	shapes.push_back(colBase);

	btCollisionShape* colNose = new btBoxShape(btVector3(info.nose_size.x * 0.5f, info.nose_size.y* 0.5f, info.nose_size.z*0.5f));
	shapes.push_back(colNose);

	btTransform transBase;
	transBase.setIdentity();
	transBase.setOrigin(btVector3(info.chassis_offset.x, info.chassis_offset.y, info.chassis_offset.z));

	comShape->addChildShape(transBase, colBase);

	btTransform transNose;
	transNose.setIdentity();
	transNose.setOrigin(btVector3(info.nose_offset.x, info.nose_offset.y, info.nose_offset.z));

	comShape->addChildShape(transNose, colNose);

	btTransform startTransform;
	startTransform.setIdentity();

	btVector3 localInertia(0, 0, 0);
	comShape->calculateLocalInertia(info.mass, localInertia);

	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(info.mass, myMotionState, comShape, localInertia);

	btRigidBody* body = new btRigidBody(rbInfo);
	body->setContactProcessingThreshold(BT_LARGE_FLOAT);
	body->setActivationState(DISABLE_DEACTIVATION);

	world->addRigidBody(body);

	btRaycastVehicle::btVehicleTuning tuning;
	tuning.m_frictionSlip = info.frictionSlip;
	tuning.m_maxSuspensionForce = info.maxSuspensionForce;
	tuning.m_maxSuspensionTravelCm = info.maxSuspensionTravelCm;
	tuning.m_suspensionCompression = info.suspensionCompression;
	tuning.m_suspensionDamping = info.suspensionDamping;
	tuning.m_suspensionStiffness = info.suspensionStiffness;

	btRaycastVehicle* vehicle = new btRaycastVehicle(tuning, body, vehicle_raycaster);

	vehicle->setCoordinateSystem(0, 1, 2);

	for(int i = 0; i < info.num_wheels; ++i)
	{
		btVector3 conn(info.wheels[i].connection.x, info.wheels[i].connection.y, info.wheels[i].connection.z);
		btVector3 dir(info.wheels[i].direction.x, info.wheels[i].direction.y, info.wheels[i].direction.z);
		btVector3 axis(info.wheels[i].axis.x, info.wheels[i].axis.y, info.wheels[i].axis.z);

		vehicle->addWheel(conn, dir, axis, info.wheels[i].suspensionRestLength, info.wheels[i].radius, tuning, info.wheels[i].front);
	}
	// ---------------------

	PhysVehicle3D* pvehicle = new PhysVehicle3D(body, vehicle, info, col);
	world->addVehicle(vehicle);
	vehicles.push_back(pvehicle);

	pvehicle->SetTransform(info.transform.Transposed().ptr());

	pvehicle->collisionOptions = SetFlag(pvehicle->collisionOptions, PhysVehicle3D::co_isCar, true);

	return pvehicle;
}


// ---------------------------------------------------------
void ModulePhysics3D::AddTerrain()
{
	if (heightMapImg != nullptr)
	{
		terrain = new btHeightfieldTerrainShape(heightMapImg->GetWidth(), heightMapImg->GetHeight(), terrainData, 1.0f, -300, 300, 1, PHY_ScalarType::PHY_FLOAT, false);
		shapes.push_back(terrain);

		btDefaultMotionState* myMotionState = new btDefaultMotionState();
		motions.push_back(myMotionState);
		btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, myMotionState, terrain);

		btRigidBody* body = new btRigidBody(rbInfo);

		world->addRigidBody(body);
	}
}

void ModulePhysics3D::GenerateIndices()
{
	if (heightMapImg)
	{
		DeleteIndices();

		int w = heightMapImg->GetWidth();
		int h = heightMapImg->GetHeight();

		//Interior vertices all need 6 indices
		//limit vertices all need 3 indices
		//Except corner ones. Two need 1 and two need 2
		numIndices = ((w - 2) * (h - 2)) * 6 + (w * 2 + h * 2) * 3 - 2 - 1 - 2 - 1;
		indices = new uint[numIndices];

		int n = 0;
		for (int z = 0; z < h - 1; z++)
		{
			for (int x = 0; x < w - 1; x++)
			{
				indices[n] = (z + 1) * w + x;
				n++;
				indices[n] = z * w + x + 1;
				n++;
				indices[n] = z * w + x;
				n++;

				indices[n] = z * w + x + 1;
				n++;
				indices[n] = (z + 1) * w + x;
				n++;
				indices[n] = (z + 1) * w + x + 1;
				n++;
			}
		}

		//Load indices buffer to VRAM
		glGenBuffers(1, (GLuint*) &(terrainIndicesBuffer));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrainIndicesBuffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint) * numIndices, indices, GL_STATIC_DRAW);
	}
}

void ModulePhysics3D::DeleteIndices()
{
	if (terrainIndicesBuffer != 0)
	{
		glDeleteBuffers(1, (GLuint*)&terrainIndicesBuffer);
		terrainIndicesBuffer = 0;
	}
	if (indices != nullptr)
	{
		delete[] indices;
		indices = nullptr;
	}
	numIndices = 0;
}

void ModulePhysics3D::RenderTerrain()
{
	if (numIndices != 0 && terrainData != nullptr)
	{
		if (renderWiredTerrain)
		{
			glLineWidth(1.0f);
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}

		uint shader_id = App->resource_manager->GetDefaultTerrainShaderId();
		//Use shader
		glUseProgram(shader_id);

		//Set uniforms

		//Matrices
		GLint model_location = glGetUniformLocation(shader_id, "model");
		glUniformMatrix4fv(model_location, 1, GL_FALSE, *(float4x4::identity).v);
		GLint projection_location = glGetUniformLocation(shader_id, "projection");
		glUniformMatrix4fv(projection_location, 1, GL_FALSE, *App->renderer3D->camera->GetProjectionMatrix().v);
		GLint view_location = glGetUniformLocation(shader_id, "view");
		glUniformMatrix4fv(view_location, 1, GL_FALSE, *App->renderer3D->camera->GetViewMatrix().v);

		int count = 0;
		if (textures.size() > 0)
		{
			GLint tex_distributor_location = glGetUniformLocation(shader_id, "_TextureDistributor");
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, edgeTexId);
			glUniform1i(tex_distributor_location, 0);

			uint nTextures = textures.size();
			GLint texture_location = 0;
			//TEXTURE 0
			GLint has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_0");
			glUniform1i(has_tex_location, 0<nTextures);
			if (0 < nTextures)
			{
				GLint texture_location = glGetUniformLocation(shader_id, "_Texture_0");
				glUniform1i(texture_location, 1);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, textures[0]->GetTexture());
			}

			//TEXTURE 1
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_1");
			glUniform1i(has_tex_location, 1 < nTextures);

			if (1 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_1");
				glUniform1i(texture_location, 2);
				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, textures[1]->GetTexture());
			}

			//TEXTURE 2
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_2");
			glUniform1i(has_tex_location, 2  < nTextures);

			if (2 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_2");
				glUniform1i(texture_location, 3);
				glActiveTexture(GL_TEXTURE3);
				glBindTexture(GL_TEXTURE_2D, textures[2]->GetTexture());
			}

			//TEXTURE 3
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_3");
			glUniform1i(has_tex_location, 3 < nTextures);
			if (3 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_3");
				glUniform1i(texture_location, 4);
				glActiveTexture(GL_TEXTURE4);
				glBindTexture(GL_TEXTURE_2D, textures[3]->GetTexture());
			}

			//TEXTURE 4
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_4");
			glUniform1i(has_tex_location, 4 < nTextures);
			if (4 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_4");
				glUniform1i(texture_location, 5);
				glActiveTexture(GL_TEXTURE5);
				glBindTexture(GL_TEXTURE_2D, textures[4]->GetTexture());
			}
			//TEXTURE 5
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_5");
			glUniform1i(has_tex_location, 5 < nTextures);
			if (5 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_5");
				glUniform1i(texture_location, 6);
				glActiveTexture(GL_TEXTURE6);
				glBindTexture(GL_TEXTURE_2D, textures[5]->GetTexture());
			}
			//TEXTURE 6
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_7");
			glUniform1i(has_tex_location, 6 < nTextures);
			if (6 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_7");
				glUniform1i(texture_location, 7);
				glActiveTexture(GL_TEXTURE7);
				glBindTexture(GL_TEXTURE_2D, textures[7]->GetTexture());
			}

			//TEXTURE 7
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_7");
			glUniform1i(has_tex_location, 7 < nTextures);
			if (7 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_7");
				glUniform1i(texture_location, 8);
				glActiveTexture(GL_TEXTURE8);
				glBindTexture(GL_TEXTURE_2D, textures[7]->GetTexture());
			}
			//TEXTURE 8
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_8");
			glUniform1i(has_tex_location, 8  < nTextures);
			if (8 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_8");
				glUniform1i(texture_location, 9);
				glActiveTexture(GL_TEXTURE9);
				glBindTexture(GL_TEXTURE_2D, textures[8]->GetTexture());
			}
			//TEXTURE 9
			has_tex_location = glGetUniformLocation(shader_id, "_HasTexture_9");
			glUniform1i(has_tex_location, 9 < nTextures);
			if (9 < nTextures)
			{
				texture_location = glGetUniformLocation(shader_id, "_Texture_9");
				glUniform1i(texture_location, 10);
				glActiveTexture(GL_TEXTURE10);
				glBindTexture(GL_TEXTURE_2D, textures[9]->GetTexture());
			}
			
		}
		else
		{
			GLint has_tex_location = glGetUniformLocation(shader_id, "_HasTexture");
			glUniform1i(has_tex_location, 1);
			GLint texture_location = glGetUniformLocation(shader_id, "_Texture");
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, edgeTexId);
			glUniform1i(texture_location, 0);
			//GLint has_tex_location = glGetUniformLocation(shader_id, "_HasTexture");
			//glUniform1i(has_tex_location, 0);
		}


		GLint colorLoc = glGetUniformLocation(shader_id, "material_color");
		if (colorLoc != -1)
		{
			float4 color(1.0f, 1.0f, 1.0f, 1.0f);
			glUniform4fv(colorLoc, 1, color.ptr());
		}


		//Lighting
		LightInfo light = App->lighting->GetLightInfo();
		//Ambient
		GLint ambient_intensity_location = glGetUniformLocation(shader_id, "_AmbientIntensity");
		if (ambient_intensity_location != -1)
			glUniform1f(ambient_intensity_location, light.ambient_intensity);
		GLint ambient_color_location = glGetUniformLocation(shader_id, "_AmbientColor");
		if (ambient_color_location != -1)
			glUniform3f(ambient_color_location, light.ambient_color.x, light.ambient_color.y, light.ambient_color.z);

		//Directional
		GLint has_directional_location = glGetUniformLocation(shader_id, "_HasDirectional");
		glUniform1i(has_directional_location, light.has_directional);

		if (light.has_directional)
		{
			GLint directional_intensity_location = glGetUniformLocation(shader_id, "_DirectionalIntensity");
			if (directional_intensity_location != -1)
				glUniform1f(directional_intensity_location, light.directional_intensity);
			GLint directional_color_location = glGetUniformLocation(shader_id, "_DirectionalColor");
			if (directional_color_location != -1)
				glUniform3f(directional_color_location, light.directional_color.x, light.directional_color.y, light.directional_color.z);
			GLint directional_direction_location = glGetUniformLocation(shader_id, "_DirectionalDirection");
			if (directional_direction_location != -1)
				glUniform3f(directional_direction_location, light.directional_direction.x, light.directional_direction.y, light.directional_direction.z);
		}
		

		//Buffer vertices == 0
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, terrainVerticesBuffer);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

		//Buffer uvs == 1
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, terrainUvBuffer);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

		//Buffer normals == 2
		glEnableVertexAttribArray(2);
		glBindBuffer(GL_ARRAY_BUFFER, terrainNormalBuffer);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_TRUE, 0, (GLvoid*)0);

		//Buffer terrainUVs == 3
		glEnableVertexAttribArray(3);
		glBindBuffer(GL_ARRAY_BUFFER, terrainOriginalUvBuffer);
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

		//Index buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrainIndicesBuffer);
		glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, (void*)0);

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);
		glDisableVertexAttribArray(3);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}

void ModulePhysics3D::GenerateTerrainMesh()
{
	GenerateVertices();
	GenerateIndices();
	GenerateUVs();
}

void ModulePhysics3D::DeleteNormals()
{
	if (terrainNormalBuffer != 0)
	{
		glDeleteBuffers(1, (GLuint*)&terrainNormalBuffer);
		terrainNormalBuffer = 0;
	}
}

void ModulePhysics3D::GenerateUVs()
{
	if (heightMapImg)
	{
		int w = heightMapImg->GetWidth();
		int h = heightMapImg->GetHeight();

		float2* uvs = new float2[w*h];
		float2* originalUvs = new float2[w*h];

		for (int z = 0; z < h; z++)
		{
			for (int x = 0; x < w; x++)
			{
				float uv_x = ((float)x / (float)w) / textureScaling;
				float uv_y = 1 - (((float)z / (float)h) / textureScaling);
				uvs[z * w + x] = float2(uv_x, uv_y);
				originalUvs[z * w + x] = float2(((float)x / (float)w),( 1 - ((float)z / (float)h)));
			}
		}

		DeleteUVs();

		//Load UVs -----------------------------------------------------------------------------------------------------------------------
		glGenBuffers(1, (GLuint*)&(terrainUvBuffer));
		glBindBuffer(GL_ARRAY_BUFFER, terrainUvBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float2) * w*h, uvs, GL_STATIC_DRAW);

		//Load Original UVs -----------------------------------------------------------------------------------------------------------------------
		glGenBuffers(1, (GLuint*)&(terrainOriginalUvBuffer));
		glBindBuffer(GL_ARRAY_BUFFER, terrainOriginalUvBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float2) * w*h, originalUvs, GL_STATIC_DRAW);

		delete[] uvs;
		delete[] originalUvs;
	}
}

void ModulePhysics3D::DeleteUVs()
{
	if (terrainUvBuffer != 0)
	{
		glDeleteBuffers(1, (GLuint*)&terrainUvBuffer);
		terrainUvBuffer = 0;
	}
	if (terrainOriginalUvBuffer != 0)
	{
		glDeleteBuffers(1, (GLuint*)&terrainOriginalUvBuffer);
		terrainOriginalUvBuffer = 0;
	}
}

void ModulePhysics3D::DeleteTerrainMesh()
{
	DeleteVertices();
	DeleteNormals();
	DeleteUVs();
	DeleteIndices();
}

void ModulePhysics3D::GenerateVertices()
{
	if (heightMapImg)
	{
		int w = heightMapImg->GetWidth();
		int h = heightMapImg->GetHeight();
		uint numVertices = w * h;

		float3* vertices = new float3[numVertices];

		for (int z = 0; z < h; z++)
		{
			for (int x = 0; x < w; x++)
			{
				vertices[z * w + x] = float3(x - w / 2, realTerrainData[z * w + x] * terrainMaxHeight, z - h / 2);
			}
		}

		DeleteVertices();

		//Load vertices buffer to VRAM
		glGenBuffers(1, (GLuint*)&(terrainVerticesBuffer));
		glBindBuffer(GL_ARRAY_BUFFER, terrainVerticesBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float3) * numVertices, vertices, GL_STATIC_DRAW);

		GenerateNormals(vertices);

		delete[] vertices;
	}
}

void ModulePhysics3D::DeleteVertices()
{
	if (terrainVerticesBuffer != 0)
	{
		glDeleteBuffers(1, (GLuint*)&terrainVerticesBuffer);
		terrainVerticesBuffer = 0;
	}
}

void ModulePhysics3D::GenerateNormals(float3* vertices)
{
	if (heightMapImg)
	{
		int w = heightMapImg->GetWidth();
		int h = heightMapImg->GetHeight();
		uint numVertices = w * h;

		float3* normals = new float3[numVertices];

		for (int z = 0; z < h; z++)
		{
			for (int x = 0; x < w; x++)
			{
				Triangle t;
				float3 norm = float3::zero;

				//Top left
				if (x - 1 > 0 && z - 1 > 0)
				{
					t.a = vertices[(z)* w + x];
					t.b = vertices[(z - 1)* w + x];
					t.c = vertices[(z)* w + x - 1];
					norm += t.NormalCCW();
				}
				//Top right
				if (x + 1 < w && z - 1 > 0)
				{
					t.a = vertices[(z)* w + x];
					t.b = vertices[(z)* w + x + 1];
					t.c = vertices[(z - 1)* w + x];
					norm += t.NormalCCW();
				}
				//Bottom left
				if (x - 1 > 0 && z + 1 < h)
				{
					t.a = vertices[(z)* w + x];
					t.b = vertices[(z)* w + x - 1];
					t.c = vertices[(z + 1)* w + x];
					norm += t.NormalCCW();
				}
				//Bottom right
				if (x + 1 < w && z + 1 < h)
				{
					t.a = vertices[(z)* w + x];
					t.b = vertices[(z + 1)* w + x];
					t.c = vertices[(z)* w + x + 1];
					norm += t.NormalCCW();
				}
				norm.Normalize();
				normals[z * w + x] = norm;
			}
		}

		DeleteNormals();

		//Load Normalss -----------------------------------------------------------------------------------------------------------------------
		glGenBuffers(1, (GLuint*)&(terrainNormalBuffer));
		glBindBuffer(GL_ARRAY_BUFFER, terrainNormalBuffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float3) * numVertices, normals, GL_STATIC_DRAW);

		delete[] normals;
	}
}

void ModulePhysics3D::InterpretHeightmapRGB(float * R, float * G, float * B)
{
	int w = heightMapImg->GetWidth();
	int h = heightMapImg->GetHeight();

	float* edgeH = new float[(w*h) * 3];
	float* edgeV = new float[(w*h) * 3];

	if (edgeDetectionImage != nullptr)
	{
		delete[] edgeDetectionImage;
	}
	edgeDetectionImage = new float[w*h];

	float* buf = new float[w*h];
	float maxVal = 0;

	//Setting the buffer content to the max value of RGB, to get a single matrix instead of three (R, G, B)
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			buf[y*w + x] = (max(max(R[y*w + x], G[y*w + x]), B[y*w + x]));
		}
	}

	float value = 0.0f;
	int n = 0;
	//Iterating all image pixels
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
#pragma region GaussianBlur
			value = 0.0f;
			n = 0;
			//Iterating all nearby pixels and checking they actually exist in the image
			for (int _y = y - terrainSmoothLevels; _y <= y + terrainSmoothLevels; _y++)
			{
				if (_y > 0 && _y < h)
				{
					for (int _x = x - terrainSmoothLevels; _x <= x + terrainSmoothLevels; _x++)
					{
						if (_x > 0 && _x < w)
						{
							n++;
							value += buf[_y * w + _x];
						}
					}
				}
			}
			value /= n;
			value /= 255;
			realTerrainData[y*w + x] = value;
			terrainData[y*w + x] = value * terrainMaxHeight;
#pragma endregion

#pragma region Edge detection
			int hk[3][3] = { {-1,0,1}, {-2,0,2}, {-1,0,1} };
			int vk[3][3] = { { -1,-2,-1 },{ 0,0,0 },{ 1,2,1 } };
			edgeH[y * w + x] = 0;
			edgeV[y * w + x] = 0;
			if (x - 1 >= 0 && x + 1 < w && y - 1 >= 0 && y + 1 < h)
			{
				for (int _y = -1; _y <= 1; _y++)
				{
					for (int _x = -1; _x <= 1; _x++)
					{
						edgeH[y * w + x] += buf[(y + _y) * w + x + _x] * hk[_y+1][_x + 1];

						edgeV[y * w + x] += buf[(y + _y) * w + x + _x] * vk[_y + 1][_x + 1];
					}
				}
			}

			edgeDetectionImage[(h - y - 1) * w + x] = math::Sqrt(edgeH[y * w + x] * edgeH[y * w + x] + edgeV[y * w + x] * edgeV[y * w + x]);
			if (edgeDetectionImage[(h - y - 1) * w + x] > maxVal)
			{
				maxVal = edgeDetectionImage[(h - y - 1) * w + x];
			}
#pragma endregion
		}
	}	

	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			edgeDetectionImage[y * w + x] /= maxVal + maxVal/2;
			if (edgeDetectionImage[y * w + x] < 0.02f)
			{
				edgeDetectionImage[y * w + x] = 0.2f;
			}
			else
			{
				edgeDetectionImage[y * w + x] = 1.0f;
			}
		}
	}

	glEnable(GL_TEXTURE_2D);
	if (edgeTexId == 0)
	{
		glGenTextures(1, &edgeTexId);
	}
	glBindTexture(GL_TEXTURE_2D, edgeTexId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_FLOAT, edgeDetectionImage);

	delete[] edgeH;
	delete[] edgeV;
	delete[] buf;
}

void ModulePhysics3D::SetTerrainMaxHeight(float height)
{
	if (height > 0.1f)
	{
		if (heightMapImg)
		{
			for (unsigned int n = 0; n < heightMapImg->GetWidth() * heightMapImg->GetHeight(); n++)
			{
				terrainData[n] = realTerrainData[n]  * height;
			}
		}
		terrainMaxHeight = height;
		GenerateTerrainMesh();
	}
}

void ModulePhysics3D::SetTextureScaling(float scale, bool doNotUse)
{
	if (scale > 0)
	{
		float tmp = 0.0f;
		if (doNotUse) { tmp = textureScaling; }
		textureScaling = scale;
		GenerateUVs();
		if (doNotUse) { textureScaling = tmp; }
	}
}

void ModulePhysics3D::LoadTexture(string resLibPath)
{
	//Loading Heightmap Image
	if (resLibPath != "" && resLibPath != " ")
	{
		ResourceFile* res = App->resource_manager->LoadResource(resLibPath, ResourceFileType::RES_TEXTURE);
		if (res != nullptr && res->GetType() == ResourceFileType::RES_TEXTURE)
		{
			textures.push_back((ResourceFileTexture*)res);
			if (textures.size() == 1)
			{
				GenerateUVs();
			}
		}
	}
}

void ModulePhysics3D::DeleteTexture(uint n)
{
	if (n >= 0 && n < textures.size())
	{
		int x = 0;
		for (std::vector<ResourceFileTexture*>::iterator it = textures.begin(); it != textures.end(); it++)
		{
			if (x == n)
			{
				textures[n]->Unload();
				textures.erase(it);
				break;
			}
			x++;
		}
	}

	if (textures.size() == 0)
	{
		SetTextureScaling(1.0f, true);
	}
}

uint ModulePhysics3D::GetCurrentTerrainUUID()
{
	if (heightMapImg)
	{
		return heightMapImg->GetUUID();
	}
	return 0;
}

const char *ModulePhysics3D::GetHeightmapPath()
{
	if (heightMapImg)
	{
		return heightMapImg->GetFile();
	}
	char ret[5] = " ";
	return ret;
}

int ModulePhysics3D::GetHeightmap()
{
	if (heightMapImg != nullptr)
	{
		return heightMapImg->GetTexture();
	}
	return 0;
}

int ModulePhysics3D::GetTexture(uint n)
{
	if (n >= 0 && n < textures.size())
	{
		return textures[n]->GetTexture();
	}
	return 0;
}

uint ModulePhysics3D::GetTextureUUID(uint n)
{
	if (n >= 0 && n < textures.size())
	{
		return textures[n]->GetUUID();
	}
	return 0;
}

const char * ModulePhysics3D::GetTexturePath(uint n)
{
	if (n >= 0 && n < textures.size())
	{
		return textures[n]->GetFile();
	}
	char ret[5] = " ";
	return ret;
}

uint ModulePhysics3D::GetNTextures()
{
	return textures.size();
}

float2 ModulePhysics3D::GetHeightmapSize()
{
	if (heightMapImg)
	{
		return float2(heightMapImg->GetWidth(), heightMapImg->GetHeight());
	}
	return float2::zero;
}


// ---------------------------------------------------------
void ModulePhysics3D::AddConstraintP2P(PhysBody3D& bodyA, PhysBody3D& bodyB, const vec& anchorA, const vec& anchorB)
{
	btTypedConstraint* p2p = new btPoint2PointConstraint(
		*(bodyA.body), 
		*(bodyB.body), 
		btVector3(anchorA.x, anchorA.y, anchorA.z), 
		btVector3(anchorB.x, anchorB.y, anchorB.z));
	world->addConstraint(p2p);
	constraints.push_back(p2p);
	p2p->setDbgDrawSize(2.0f);
}

void ModulePhysics3D::AddConstraintHinge(PhysBody3D& bodyA, PhysBody3D& bodyB, const vec& anchorA, const vec& anchorB, const vec& axisA, const vec& axisB, bool disable_collision)
{
	btHingeConstraint* hinge = new btHingeConstraint(
		*(bodyA.body), 
		*(bodyB.body), 
		btVector3(anchorA.x, anchorA.y, anchorA.z),
		btVector3(anchorB.x, anchorB.y, anchorB.z),
		btVector3(axisA.x, axisA.y, axisA.z), 
		btVector3(axisB.x, axisB.y, axisB.z));

	world->addConstraint(hinge, disable_collision);
	constraints.push_back(hinge);
	hinge->setDbgDrawSize(2.0f);
}


// =============================================
void DebugDrawer::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
	line.origin.Set(from.getX(), from.getY(), from.getZ());
	line.destination.Set(to.getX(), to.getY(), to.getZ());
	line.color.Set(color.getX(), color.getY(), color.getZ());
	line.Render();
}

void DebugDrawer::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
{
	point.transform.SetTranslatePart(PointOnB.getX(), PointOnB.getY(), PointOnB.getZ());
	point.color.Set(color.getX(), color.getY(), color.getZ());
	point.Render();
}

void DebugDrawer::reportErrorWarning(const char* warningString)
{
	LOG("Bullet warning: %s", warningString);
}

void DebugDrawer::draw3dText(const btVector3& location, const char* textString)
{
	LOG("Bullet draw text: %s", textString);
}

void DebugDrawer::setDebugMode(int debugMode)
{
	mode = (DebugDrawModes) debugMode;
}

int	 DebugDrawer::getDebugMode() const
{
	return mode;
}
