#include "Globals.h"
#include "Application.h"
#include "ModuleCar.h"
#include "PhysVehicle3D.h"
#include "glmath.h"

#include "ModuleResourceManager.h"
#include "ModuleGOManager.h"
#include "ComponentTransform.h"
#include "Editor.h"
#include "Assets.h"
#include "GameObject.h"
#include "RaycastHit.h"

#include "ModuleInput.h"

#include "glmath.h"

#include "imgui\imgui.h"

#define DISTANCE_FROM_GROUND 1.0

ModuleCar::ModuleCar(const char* name, bool start_enabled) : Module(name, start_enabled)
{
}

// Destructor
ModuleCar::~ModuleCar()
{
}

// Called before render is available
bool ModuleCar::Init(Data& config)
{
	bool ret = true;
	return ret;
}

bool ModuleCar::Start()
{
	return true;
}

// Called every draw update
update_status ModuleCar::PreUpdate()
{

	return UPDATE_CONTINUE;
}

update_status ModuleCar::Update()
{
	if (App->IsGameRunning())
	{
		if (vehicle != nullptr)
		{
			/*Plane_P p(0, 0, 0, 10);
			p.color = Black;
			p.Render();*/

			// Vehicle Input
			if (App->input->GetKey(SDL_SCANCODE_RETURN) == KEY_DOWN)
			{
				mat4x4 tmp_mat;
				tmp_mat.rotate(0, { 0, 1, 0 });
				vehicle->SetTransform(&tmp_mat);

				vehicle->SetAngularSpeed(0, 0, 0);
				vehicle->SetLinearSpeed(0, 0, 0);
				vehicle->SetPos(10, 4, 0);
			}

			turn = acceleration = brake = 0.0f;
			if (App->input->GetKey(SDL_SCANCODE_UP) == KEY_REPEAT)
				acceleration = MAX_ACCELERATION;

			if (App->input->GetKey(SDL_SCANCODE_LEFT) == KEY_REPEAT && turn < TURN_DEGREES)
				turn += TURN_DEGREES;

			if (App->input->GetKey(SDL_SCANCODE_RIGHT) == KEY_REPEAT && turn > -TURN_DEGREES)
				turn -= TURN_DEGREES;

			if (App->input->GetKey(SDL_SCANCODE_SPACE) == KEY_REPEAT)
				acceleration = -MAX_ACCELERATION;

			if (App->input->GetKey(SDL_SCANCODE_DOWN) == KEY_REPEAT)
				brake = BRAKE_POWER;

			vehicle->ApplyEngineForce(acceleration);
			vehicle->Turn(turn);
			vehicle->Brake(brake);
			//vehicle->Render();
			float4x4 vehicleTrs;
			float tmp[16];
			vehicle->GetTransform(tmp);
			vehicleTrs.Set(tmp);
			((ComponentTransform*)(chasis->GetComponent(C_TRANSFORM)))->SetPosition(vehicle->GetPos());
			((ComponentTransform*)(chasis->GetComponent(C_TRANSFORM)))->SetRotation(vehicleTrs.ToEulerXYZ());
		}
		else
		{
			AddCar();
		}
	}
	else
	{
		vehicle = nullptr;
	}
	return UPDATE_CONTINUE;
}

void ModuleCar::AddCar()
{
	chasis = App->go_manager->CreatePrimitive(PrimitiveType::P_CUBE);

	// Car properties ----------------------------------------
	VehicleInfo car;
	car.chassis_size.Set(2, 1, 2);
	car.chassis_offset.Set(0.1, 0.9, 0.1);

	car.mass = 5.0f;
	car.suspensionStiffness = 15.88f;
	car.suspensionCompression = 0.83f;
	car.suspensionDamping = 0.88f;
	car.maxSuspensionTravelCm = 2.0f;
	car.frictionSlip = 500.0f;
	car.maxSuspensionForce = 3.0f;

	// Wheel properties ---------------------------------------
	float connection_height = 1.2f;
	float wheel_radius = 0.6f;
	float wheel_width = 0.5f;
	float suspensionRestLength = 1.2f;

	// Don't change anything below this line ------------------

	float half_width = car.chassis_size.x*0.5f;
	float half_length = car.chassis_size.z*0.5f;

	vec direction(0, -1, 0);
	vec axis(-1, 0, 0);

	car.num_wheels = 4;
	car.wheels = new Wheel[4];

	// FRONT-LEFT ------------------------
	car.wheels[0].connection.Set(half_width - 0.3f * wheel_width, connection_height, half_length - wheel_radius);
	car.wheels[0].direction = direction;
	car.wheels[0].axis = axis;
	car.wheels[0].suspensionRestLength = suspensionRestLength;
	car.wheels[0].radius = wheel_radius;
	car.wheels[0].width = wheel_width;
	car.wheels[0].front = true;
	car.wheels[0].drive = true;
	car.wheels[0].brake = false;
	car.wheels[0].steering = true;

	// FRONT-RIGHT ------------------------
	car.wheels[1].connection.Set(-half_width + 0.3f * wheel_width, connection_height, half_length - wheel_radius);
	car.wheels[1].direction = direction;
	car.wheels[1].axis = axis;
	car.wheels[1].suspensionRestLength = suspensionRestLength;
	car.wheels[1].radius = wheel_radius;
	car.wheels[1].width = wheel_width;
	car.wheels[1].front = true;
	car.wheels[1].drive = true;
	car.wheels[1].brake = false;
	car.wheels[1].steering = true;

	// REAR-LEFT ------------------------
	car.wheels[2].connection.Set(half_width - 0.3f * wheel_width, connection_height, -half_length + wheel_radius);
	car.wheels[2].direction = direction;
	car.wheels[2].axis = axis;
	car.wheels[2].suspensionRestLength = suspensionRestLength;
	car.wheels[2].radius = wheel_radius;
	car.wheels[2].width = wheel_width;
	car.wheels[2].front = false;
	car.wheels[2].drive = false;
	car.wheels[2].brake = true;
	car.wheels[2].steering = false;

	// REAR-RIGHT ------------------------
	car.wheels[3].connection.Set(-half_width + 0.3f * wheel_width, connection_height, -half_length + wheel_radius);
	car.wheels[3].direction = direction;
	car.wheels[3].axis = axis;
	car.wheels[3].suspensionRestLength = suspensionRestLength;
	car.wheels[3].radius = wheel_radius;
	car.wheels[3].width = wheel_width;
	car.wheels[3].front = false;
	car.wheels[3].drive = false;
	car.wheels[3].brake = true;
	car.wheels[3].steering = false;

	vehicle = App->physics->AddVehicle(car);
	vehicle->SetPos(0, 1, 0);
}